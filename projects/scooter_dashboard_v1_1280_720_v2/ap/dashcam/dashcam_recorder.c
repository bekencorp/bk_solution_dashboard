#include "dashcam_recorder.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk_video_recorder_ctlr.h"
#include "cache.h"
#include "components/bk_frame_buffer.h"
#include "components/log.h"
#include "dashcam_config.h"
#include "dashcam_storage.h"
#include "doorbell_img_manager.h"
#include "modules/vcenc/vcenc_types.h"
#include "os/os.h"

#define TAG "d_recorder"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

avdk_err_t bk_video_recorder_new(bk_video_recorder_handle_t *handle,
                                 bk_video_recorder_config_t *config);
avdk_err_t bk_video_recorder_open(bk_video_recorder_handle_t handler);
avdk_err_t bk_video_recorder_start(bk_video_recorder_handle_t handler,
                                   char *file_path,
                                   uint32_t record_type);
avdk_err_t bk_video_recorder_stop(bk_video_recorder_handle_t handler);
avdk_err_t bk_video_recorder_close(bk_video_recorder_handle_t handler);
avdk_err_t bk_video_recorder_delete(bk_video_recorder_handle_t handler);

/*
 * The recorder object (and, underneath it, the worker thread plus its control
 * semaphores) is created once and kept alive across segment boundaries. A stop
 * only finalizes the current MP4 (bk_video_recorder_stop) and a start re-arms it
 * (bk_video_recorder_start); the heavy teardown (close + delete, which destroys
 * the worker thread and thread_sem/stop_sem) is deferred to
 * dashcam_recorder_destroy(). This keeps segment rotation from destroying
 * thread_sem while the recorder thread is still parked on it (a use-after-free
 * that crashed in spinlock_take on the freed queue).
 */
static bk_video_recorder_handle_t s_recorder = NULL;
static bool s_recording = false;
static char s_current_path[DASHCAM_STORAGE_MAX_PATH];
static uint32_t s_frame_count = 0;
static uint32_t s_frame_miss_count = 0;

/* One-shot: dump the leading bytes of the first encoded key frame as hex so the
 * host can parse the H.264 SPS offline (validates encoder output vs. muxing). */
static bool s_h264_head_dumped = false;

static void dashcam_recorder_dump_h264_head(const uint8_t *data, uint32_t len)
{
    static const char hexd[] = "0123456789abcdef";
    uint32_t n = (len < 128U) ? len : 128U;
    char line[16 * 2 + 8];

    LOGI("h264 head len=%u:\n", (unsigned)len);
    for (uint32_t off = 0; off < n; off += 16U)
    {
        int p = 0;
        for (uint32_t i = 0; i < 16U && (off + i) < n; i++)
        {
            uint8_t b = data[off + i];
            line[p++] = hexd[b >> 4];
            line[p++] = hexd[b & 0x0F];
        }
        line[p] = '\0';
        LOGI("h264[%u]: %s\n", (unsigned)off, line);
    }
}

static int dashcam_recorder_get_frame_cb(void *user_data,
                                         video_recorder_frame_data_t *frame_data)
{
    frame_buffer_t *frame;

    (void)user_data;
    if (frame_data == NULL)
    {
        return -1;
    }

    frame = (frame_buffer_t *)bk_encoded_complete_data_request(50);
    if (frame == NULL || frame->frame == NULL || frame->length == 0)
    {
        s_frame_miss_count++;
        return -1;
    }

    flush_dcache(frame->frame, (long)frame->length);
    frame_data->data = frame->frame;
    frame_data->length = frame->length;
    frame_data->width = frame->width;
    frame_data->height = frame->height;
    frame_data->frame_buffer = frame;
    frame_data->is_key_frame = (frame->h264_type == (uint32_t)VCENC_OUT_IFRAME);

    if (!s_h264_head_dumped && frame_data->is_key_frame)
    {
        s_h264_head_dumped = true;
        LOGI("h264 keyframe %ux%u type=%u\n",
             (unsigned)frame->width, (unsigned)frame->height,
             (unsigned)frame->h264_type);
        dashcam_recorder_dump_h264_head((const uint8_t *)frame->frame, frame->length);
    }

    s_frame_count++;
    return 0;
}

static void dashcam_recorder_release_frame_cb(void *user_data,
                                              video_recorder_frame_data_t *frame_data)
{
    (void)user_data;

    if (frame_data == NULL)
    {
        return;
    }

    if (frame_data->frame_buffer != NULL)
    {
        bk_encoded_complete_data_free_request((uint8_t *)frame_data->frame_buffer);
    }

    frame_data->data = NULL;
    frame_data->length = 0;
    frame_data->frame_buffer = NULL;
}

/*
 * Create + open the recorder once. On the first start this allocates the object
 * and brings up the worker thread; subsequent segments reuse it. No-op if it is
 * already created.
 */
static bk_err_t dashcam_recorder_ensure_created(void)
{
    bk_video_recorder_config_t config = {0};
    avdk_err_t ret;

    if (s_recorder != NULL)
    {
        return BK_OK;
    }

    config.record_type = VIDEO_RECORDER_TYPE_MP4;
    config.record_format = VIDEO_RECORDER_FORMAT_H264;
    config.record_framerate = DASHCAM_RECORD_FPS;
    config.video_width = DASHCAM_RECORD_WIDTH;
    config.video_height = DASHCAM_RECORD_HEIGHT;
    config.audio_channels = 0;
    config.get_frame_cb = dashcam_recorder_get_frame_cb;
    config.release_frame_cb = dashcam_recorder_release_frame_cb;

    ret = bk_video_recorder_new(&s_recorder, &config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_video_recorder_new failed: %d\n", ret);
        s_recorder = NULL;
        return BK_FAIL;
    }

    ret = bk_video_recorder_open(s_recorder);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_video_recorder_open failed: %d\n", ret);
        bk_video_recorder_delete(s_recorder);
        s_recorder = NULL;
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t dashcam_recorder_start(const char *path)
{
    avdk_err_t ret;

    LOGD("start req: %s\n", path != NULL ? path : "(null)");

    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }

    if (s_recording)
    {
        LOGD("recorder already recording\n");
        return BK_OK;
    }

    if (dashcam_recorder_ensure_created() != BK_OK)
    {
        return BK_FAIL;
    }

    snprintf(s_current_path, sizeof(s_current_path), "%s", path);
    s_frame_count = 0;
    s_frame_miss_count = 0;
    s_h264_head_dumped = false;

    /* Re-arm the (already open) recorder for the next segment. The ctlr accepts
     * start from both OPENED (first segment) and STOPPED (rotation) states. */
    ret = bk_video_recorder_start(s_recorder, s_current_path, VIDEO_RECORDER_TYPE_MP4);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_video_recorder_start failed: %d\n", ret);
        /* Keep the object alive: only the per-segment start failed. Full teardown
         * is done in dashcam_recorder_destroy(). */
        return BK_FAIL;
    }

    s_recording = true;
    LOGI("recording started: %s\n", s_current_path);
    return BK_OK;
}

bk_err_t dashcam_recorder_stop(void)
{
    avdk_err_t ret = AVDK_ERR_OK;

    LOGD("stop req (recording=%d)\n", (int)s_recording);

    if (s_recorder == NULL || !s_recording)
    {
        return BK_OK;
    }

    /* Finalize the current MP4 only. Deliberately no close/delete here: the
     * worker thread and thread_sem/stop_sem stay alive so the next segment just
     * re-arms via bk_video_recorder_start(). Destroying thread_sem on every stop
     * is what raced the recorder thread and caused the use-after-free. */
    ret = bk_video_recorder_stop(s_recorder);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("bk_video_recorder_stop failed: %d\n", ret);
    }

    s_recording = false;

    LOGI("recording stopped: %s frames=%u missed=%u\n",
         s_current_path,
         (unsigned)s_frame_count,
         (unsigned)s_frame_miss_count);
    return (ret == AVDK_ERR_OK) ? BK_OK : BK_FAIL;
}

/*
 * Full teardown: stop (if recording), then close + delete, which destroys the
 * worker thread and its semaphores. Only safe to call when no segment rotation
 * is in flight (dashcam_app joins its rotation worker before shutdown), so this
 * is the single, serialized place the thread_sem lifetime is torn down.
 */
void dashcam_recorder_destroy(void)
{
    if (s_recorder == NULL)
    {
        return;
    }

    if (s_recording)
    {
        (void)bk_video_recorder_stop(s_recorder);
        s_recording = false;
    }

    (void)bk_video_recorder_close(s_recorder);
    (void)bk_video_recorder_delete(s_recorder);
    s_recorder = NULL;

    LOGI("recorder destroyed\n");
}

bool dashcam_recorder_is_running(void)
{
    return s_recording;
}

const char *dashcam_recorder_current_path(void)
{
    return s_current_path;
}

uint32_t dashcam_recorder_frame_count(void)
{
    return s_frame_count;
}
