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

#define TAG "dashcam_rec"
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

static bk_video_recorder_handle_t s_recorder = NULL;
static char s_current_path[DASHCAM_STORAGE_MAX_PATH];
static uint32_t s_frame_count = 0;
static uint32_t s_frame_miss_count = 0;

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

bk_err_t dashcam_recorder_start(const char *path)
{
    bk_video_recorder_config_t config = {0};
    avdk_err_t ret;

    LOGD("start req: %s\n", path != NULL ? path : "(null)");

    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }

    if (s_recorder != NULL)
    {
        LOGD("recorder already running\n");
        return BK_OK;
    }

    snprintf(s_current_path, sizeof(s_current_path), "%s", path);
    s_frame_count = 0;
    s_frame_miss_count = 0;

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

    ret = bk_video_recorder_start(s_recorder, s_current_path, VIDEO_RECORDER_TYPE_MP4);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_video_recorder_start failed: %d\n", ret);
        bk_video_recorder_close(s_recorder);
        bk_video_recorder_delete(s_recorder);
        s_recorder = NULL;
        return BK_FAIL;
    }

    LOGI("recording started: %s\n", s_current_path);
    return BK_OK;
}

bk_err_t dashcam_recorder_stop(void)
{
    avdk_err_t ret = AVDK_ERR_OK;

    LOGD("stop req (running=%d)\n", (int)(s_recorder != NULL));

    if (s_recorder == NULL)
    {
        return BK_OK;
    }

    ret = bk_video_recorder_stop(s_recorder);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("bk_video_recorder_stop failed: %d\n", ret);
    }

    (void)bk_video_recorder_close(s_recorder);
    (void)bk_video_recorder_delete(s_recorder);
    s_recorder = NULL;

    LOGI("recording stopped: %s frames=%u missed=%u\n",
         s_current_path,
         (unsigned)s_frame_count,
         (unsigned)s_frame_miss_count);
    return (ret == AVDK_ERR_OK) ? BK_OK : BK_FAIL;
}

bool dashcam_recorder_is_running(void)
{
    return s_recorder != NULL;
}

const char *dashcam_recorder_current_path(void)
{
    return s_current_path;
}

uint32_t dashcam_recorder_frame_count(void)
{
    return s_frame_count;
}
