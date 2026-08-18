#include "dashcam_video.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <components/bk_display.h>
#include "components/bk_frame_buffer.h"
#include "components/log.h"
#include "app_display.h"
#include "dashcam_config.h"
#include "dashcam_player.h"
#include "dashcam_storage.h"
#include "hpdma/lv_hpdma.h"
#include "lv_vendor.h"
#include "os/mem.h"
#include "os/os.h"

#define TAG "d_video"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

extern void lv_gpu_init(uint32_t tess_width, uint32_t tess_height);
extern void lv_gpu_deinit(void);

#define DASHCAM_VIDEO_BUF_COUNT               3U
#define DASHCAM_VIDEO_QUEUE_DEPTH             4U
#define DASHCAM_VIDEO_WORKER_STACK_SIZE       (4U * 1024U)
#define DASHCAM_VIDEO_WORKER_PRIORITY         BEKEN_DEFAULT_WORKER_PRIORITY
#define DASHCAM_VIDEO_RECLAIM_WAIT_MS         500U
#define DASHCAM_VIDEO_STATS_INTERVAL_MS        3000U

typedef enum
{
    DASHCAM_VIDEO_MSG_START = 0,
    DASHCAM_VIDEO_MSG_SWITCH_FILE,
    DASHCAM_VIDEO_MSG_FRAME,
    DASHCAM_VIDEO_MSG_STOP,
} dashcam_video_msg_type_t;

typedef enum
{
    DASHCAM_VIDEO_BUF_UNUSED = 0,
    DASHCAM_VIDEO_BUF_FREE,
    DASHCAM_VIDEO_BUF_QUEUED,
    DASHCAM_VIDEO_BUF_DPU,
} dashcam_video_buf_state_t;

typedef struct
{
    dashcam_video_msg_type_t type;
    void *frame;
    uint32_t format;
    char path[DASHCAM_STORAGE_MAX_PATH];
} dashcam_video_msg_t;

typedef struct
{
    volatile bool active;
    volatile bool running;
    volatile bool stopping;
    volatile bool stop_queued;
    bool lvgl_gpu_released;
    beken_queue_t queue;
    beken_thread_t worker;
    beken_semaphore_t stop_complete;
    bk_err_t stop_result;
    bk_display_ctlr_handle_t display;
    void *bufs[DASHCAM_VIDEO_BUF_COUNT];
    dashcam_video_buf_state_t buf_state[DASHCAM_VIDEO_BUF_COUNT];
    uint32_t width;
    uint32_t height;
    uint32_t stats_start_ms;
    uint32_t stats_frames;
    uint32_t input_stats_start_ms;
    uint32_t input_stats_frames;
    dashcam_video_ready_cb_t ready_callback;
    void *ready_user_data;
} dashcam_video_ctx_t;

static dashcam_video_ctx_t s_dashcam_video;

static bool dashcam_video_all_buffers_reclaimed(void);

static int32_t dashcam_video_buf_index(const void *buf)
{
    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        if (s_dashcam_video.bufs[i] == buf)
        {
            return (int32_t)i;
        }
    }

    return -1;
}

static avdk_err_t dashcam_video_display_free_cb(void *frame)
{
    uint32_t flags = rtos_enter_critical();
    int32_t index = dashcam_video_buf_index(frame);

    if (index >= 0)
    {
        s_dashcam_video.bufs[index] = NULL;
        s_dashcam_video.buf_state[index] = DASHCAM_VIDEO_BUF_UNUSED;
    }
    rtos_exit_critical(flags);

    if (index >= 0)
    {
        bk_frame_buffer_free(frame);

        if (s_dashcam_video.worker == NULL &&
            dashcam_video_all_buffers_reclaimed())
        {
            s_dashcam_video.active = false;
            s_dashcam_video.stopping = false;
        }
    }

    return AVDK_ERR_OK;
}

static bool dashcam_video_buf_track(void *frame)
{
    bool tracked = false;
    uint32_t flags = rtos_enter_critical();

    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        if (s_dashcam_video.buf_state[i] == DASHCAM_VIDEO_BUF_UNUSED)
        {
            s_dashcam_video.bufs[i] = frame;
            s_dashcam_video.buf_state[i] = DASHCAM_VIDEO_BUF_QUEUED;
            tracked = true;
            break;
        }
    }

    rtos_exit_critical(flags);
    return tracked;
}

static bool dashcam_video_buf_untrack(void *buf)
{
    bool untracked = false;
    uint32_t flags = rtos_enter_critical();
    int32_t index = dashcam_video_buf_index(buf);

    if (index >= 0)
    {
        s_dashcam_video.bufs[index] = NULL;
        s_dashcam_video.buf_state[index] = DASHCAM_VIDEO_BUF_UNUSED;
        untracked = true;
    }
    rtos_exit_critical(flags);
    return untracked;
}

static void dashcam_video_buf_release(void *buf)
{
    if (dashcam_video_buf_untrack(buf))
    {
        bk_frame_buffer_free(buf);
    }
}

static bk_err_t dashcam_video_set_display_format(bk_pixel_format_t format, bool decompress)
{
    if (s_dashcam_video.display == NULL)
    {
        return BK_FAIL;
    }

    const bk_display_pixel_format_config_t cfg =
    {
        .format = format,
        .decompress = decompress,
    };
    return (bk_display_pixel_format_set(s_dashcam_video.display, &cfg) == AVDK_ERR_OK) ?
           BK_OK : BK_FAIL;
}

static void dashcam_video_stats_update(void)
{
    uint32_t now_ms = rtos_get_time();
    uint32_t elapsed_ms = now_ms - s_dashcam_video.stats_start_ms;

    s_dashcam_video.stats_frames++;
    if (elapsed_ms < DASHCAM_VIDEO_STATS_INTERVAL_MS)
    {
        return;
    }

    uint32_t fps_x10 = s_dashcam_video.stats_frames * 10000U / elapsed_ms;
    LOGI("DPU playback: %u.%u fps (%u frames/%u ms)\n",
         (unsigned)(fps_x10 / 10U),
         (unsigned)(fps_x10 % 10U),
         (unsigned)s_dashcam_video.stats_frames,
         (unsigned)elapsed_ms);
    s_dashcam_video.stats_start_ms = now_ms;
    s_dashcam_video.stats_frames = 0;
}

static void dashcam_video_input_stats_update(void)
{
    uint32_t now_ms = rtos_get_time();
    uint32_t elapsed_ms = now_ms - s_dashcam_video.input_stats_start_ms;

    s_dashcam_video.input_stats_frames++;
    if (elapsed_ms < DASHCAM_VIDEO_STATS_INTERVAL_MS)
    {
        return;
    }

    uint32_t fps_x10 = s_dashcam_video.input_stats_frames * 10000U / elapsed_ms;
    LOGI("on_frame input: %u.%u fps, avg interval=%u ms (%u frames/%u ms)\n",
         (unsigned)(fps_x10 / 10U),
         (unsigned)(fps_x10 % 10U),
         (unsigned)(elapsed_ms / s_dashcam_video.input_stats_frames),
         (unsigned)s_dashcam_video.input_stats_frames,
         (unsigned)elapsed_ms);
    s_dashcam_video.input_stats_start_ms = now_ms;
    s_dashcam_video.input_stats_frames = 0;
}

static void dashcam_video_process_frame(void *frame, uint32_t format)
{
    int32_t index = dashcam_video_buf_index(frame);
    if (index < 0 ||
        format != DASHCAM_VIDEO_FRAME_FORMAT_ARGB8888 ||
        s_dashcam_video.stopping)
    {
        dashcam_video_buf_release(frame);
        return;
    }

    avdk_err_t ret = bk_display_flush(s_dashcam_video.display,
                                      frame,
                                      dashcam_video_display_free_cb);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("display flush failed: %d\n", ret);
        dashcam_video_buf_release(frame);
    }
    else
    {
        uint32_t flags = rtos_enter_critical();
        index = dashcam_video_buf_index(frame);
        if (index >= 0)
        {
            s_dashcam_video.buf_state[index] = DASHCAM_VIDEO_BUF_DPU;
        }
        rtos_exit_critical(flags);
        dashcam_video_stats_update();
    }
}

static void dashcam_video_free_idle_buffers(void)
{
    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        void *buf = NULL;
        uint32_t flags = rtos_enter_critical();
        if (s_dashcam_video.buf_state[i] != DASHCAM_VIDEO_BUF_DPU)
        {
            buf = s_dashcam_video.bufs[i];
            s_dashcam_video.bufs[i] = NULL;
            s_dashcam_video.buf_state[i] = DASHCAM_VIDEO_BUF_UNUSED;
        }
        rtos_exit_critical(flags);

        if (buf != NULL)
        {
            bk_frame_buffer_free(buf);
        }
    }
}

static bool dashcam_video_all_buffers_reclaimed(void)
{
    bool reclaimed = true;
    uint32_t flags = rtos_enter_critical();
    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        if (s_dashcam_video.bufs[i] != NULL)
        {
            reclaimed = false;
            break;
        }
    }
    rtos_exit_critical(flags);
    return reclaimed;
}

static void dashcam_video_restore_lvgl(void)
{
    s_dashcam_video.running = false;
    s_dashcam_video.stopping = true;
    dashcam_video_free_idle_buffers();

    if (s_dashcam_video.display != NULL)
    {
        if (dashcam_video_set_display_format(BK_PIXEL_FORMAT_ARGB8888, true) != BK_OK)
        {
            LOGE("restore ARGB8888 display format failed\n");
        }
    }

    if (s_dashcam_video.lvgl_gpu_released)
    {
        lv_gpu_init(0U, 0U);
        s_dashcam_video.lvgl_gpu_released = false;
    }
    lv_vendor_keypad_reset();
    lv_vendor_start();

    for (uint32_t waited = 0;
         waited < DASHCAM_VIDEO_RECLAIM_WAIT_MS && !dashcam_video_all_buffers_reclaimed();
         waited += 10U)
    {
        rtos_delay_milliseconds(10U);
    }

    if (!dashcam_video_all_buffers_reclaimed())
    {
        LOGW("DPU still owns a playback buffer after resume\n");
    }
}

static void dashcam_video_notify_ready(bk_err_t result)
{
    dashcam_video_ready_cb_t callback;
    void *user_data;
    uint32_t flags = rtos_enter_critical();

    callback = s_dashcam_video.ready_callback;
    user_data = s_dashcam_video.ready_user_data;
    s_dashcam_video.ready_callback = NULL;
    s_dashcam_video.ready_user_data = NULL;
    rtos_exit_critical(flags);

    if (callback != NULL)
    {
        callback(result, user_data);
    }
}

static void dashcam_video_worker(void *arg)
{
    (void)arg;
    bool exit_worker = false;

    while (!exit_worker)
    {
        dashcam_video_msg_t msg;
        if (rtos_pop_from_queue(&s_dashcam_video.queue,
                                &msg,
                                BEKEN_WAIT_FOREVER) != BK_OK)
        {
            continue;
        }

        switch (msg.type)
        {
            case DASHCAM_VIDEO_MSG_START:
                lv_vendor_stop();
                if (lv_hpdma_memcpy_wait_finish(3000U) != BK_OK)
                {
                    LOGW("wait LVGL HPDMA finish failed, stopping transfer\n");
                    if (lv_hpdma_memcpy_stop() != BK_OK)
                    {
                        LOGE("stop LVGL HPDMA failed\n");
                        dashcam_video_restore_lvgl();
                        exit_worker = true;
                        break;
                    }
                }
                lv_gpu_deinit();
                s_dashcam_video.lvgl_gpu_released = true;
                s_dashcam_video.display =
                    (bk_display_ctlr_handle_t)app_mipi_lcd_handle_get();
                if (s_dashcam_video.display == NULL ||
                    dashcam_video_set_display_format(BK_PIXEL_FORMAT_ARGB8888, true) != BK_OK)
                {
                    LOGE("prepare direct DPU playback failed\n");
                    dashcam_video_restore_lvgl();
                    exit_worker = true;
                    break;
                }
                s_dashcam_video.stats_start_ms = rtos_get_time();
                s_dashcam_video.stats_frames = 0;
                s_dashcam_video.running = true;
                if (dashcam_player_play(msg.path) != BK_OK)
                {
                    LOGE("start player failed: %s\n", msg.path);
                    s_dashcam_video.running = false;
                    (void)dashcam_player_close();
                    dashcam_video_notify_ready(BK_FAIL);
                    dashcam_video_restore_lvgl();
                    exit_worker = true;
                    break;
                }
                LOGI("direct DPU playback ready %ux%u, rotation=%u\n",
                     (unsigned)s_dashcam_video.width,
                     (unsigned)s_dashcam_video.height,
                     (unsigned)DASHCAM_PLAYBACK_ROTATION);
                dashcam_video_notify_ready(BK_OK);
                break;

            case DASHCAM_VIDEO_MSG_SWITCH_FILE:
                if (!s_dashcam_video.running ||
                    dashcam_player_play(msg.path) != BK_OK)
                {
                    LOGE("switch player file failed: %s\n", msg.path);
                }
                break;

            case DASHCAM_VIDEO_MSG_FRAME:
                dashcam_video_process_frame(msg.frame, msg.format);
                break;

            case DASHCAM_VIDEO_MSG_STOP:
                (void)dashcam_player_close();
                dashcam_video_restore_lvgl();
                exit_worker = true;
                break;

            default:
                break;
        }
    }

    dashcam_video_msg_t pending;
    while (rtos_pop_from_queue(&s_dashcam_video.queue,
                               &pending,
                               BEKEN_NO_WAIT) == BK_OK)
    {
        if (pending.type == DASHCAM_VIDEO_MSG_FRAME)
        {
            dashcam_video_buf_release(pending.frame);
        }
    }

    dashcam_video_free_idle_buffers();
    rtos_deinit_queue(&s_dashcam_video.queue);
    s_dashcam_video.queue = NULL;
    s_dashcam_video.worker = NULL;
    s_dashcam_video.display = NULL;
    s_dashcam_video.width = 0;
    s_dashcam_video.height = 0;
    s_dashcam_video.stop_queued = false;
    s_dashcam_video.stop_result =
        dashcam_video_all_buffers_reclaimed() ? BK_OK : BK_FAIL;
    s_dashcam_video.active = false;
    s_dashcam_video.stopping = false;
    if (s_dashcam_video.stop_complete != NULL)
    {
        rtos_set_semaphore(&s_dashcam_video.stop_complete);
    }
    rtos_delete_thread(NULL);
}

static void dashcam_video_start_cleanup(void)
{
    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        if (s_dashcam_video.bufs[i] != NULL)
        {
            bk_frame_buffer_free(s_dashcam_video.bufs[i]);
            s_dashcam_video.bufs[i] = NULL;
        }
        s_dashcam_video.buf_state[i] = DASHCAM_VIDEO_BUF_UNUSED;
    }

    if (s_dashcam_video.queue != NULL)
    {
        rtos_deinit_queue(&s_dashcam_video.queue);
        s_dashcam_video.queue = NULL;
    }
    if (s_dashcam_video.stop_complete != NULL)
    {
        rtos_deinit_semaphore(&s_dashcam_video.stop_complete);
        s_dashcam_video.stop_complete = NULL;
    }

    s_dashcam_video.active = false;
}

bk_err_t dashcam_video_start_sink(lv_obj_t *parent, const char *path)
{
    (void)parent;

    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }
    if (s_dashcam_video.active)
    {
        return BK_ERR_BUSY;
    }
    if (!dashcam_video_all_buffers_reclaimed())
    {
        LOGW("previous DPU playback buffer has not been reclaimed\n");
        return BK_ERR_BUSY;
    }
    if (s_dashcam_video.stop_complete != NULL)
    {
        rtos_deinit_semaphore(&s_dashcam_video.stop_complete);
        s_dashcam_video.stop_complete = NULL;
    }

    s_dashcam_video.active = true;
    s_dashcam_video.stopping = false;
    s_dashcam_video.stop_queued = false;
    s_dashcam_video.lvgl_gpu_released = false;
    s_dashcam_video.width = DASHCAM_RECORD_WIDTH;
    s_dashcam_video.height = DASHCAM_RECORD_HEIGHT;
    s_dashcam_video.input_stats_start_ms = rtos_get_time();
    s_dashcam_video.input_stats_frames = 0;
    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        s_dashcam_video.bufs[i] = NULL;
        s_dashcam_video.buf_state[i] = DASHCAM_VIDEO_BUF_UNUSED;
    }

    if (rtos_init_semaphore(&s_dashcam_video.stop_complete, 1) != BK_OK)
    {
        dashcam_video_start_cleanup();
        return BK_FAIL;
    }
    if (rtos_init_queue(&s_dashcam_video.queue,
                        "dcam_disp_q",
                        sizeof(dashcam_video_msg_t),
                        DASHCAM_VIDEO_QUEUE_DEPTH) != BK_OK)
    {
        dashcam_video_start_cleanup();
        return BK_FAIL;
    }

    if (rtos_create_thread(&s_dashcam_video.worker,
                           DASHCAM_VIDEO_WORKER_PRIORITY,
                           "dcam_dpu",
                           dashcam_video_worker,
                           DASHCAM_VIDEO_WORKER_STACK_SIZE,
                           NULL) != BK_OK)
    {
        s_dashcam_video.worker = NULL;
        dashcam_video_start_cleanup();
        return BK_FAIL;
    }

    dashcam_video_msg_t msg =
    {
        .type = DASHCAM_VIDEO_MSG_START,
        .frame = NULL,
        .format = 0U,
    };
    snprintf(msg.path, sizeof(msg.path), "%s", path);
    if (rtos_push_to_queue(&s_dashcam_video.queue, &msg, BEKEN_NO_WAIT) != BK_OK)
    {
        dashcam_video_stop();
        return BK_FAIL;
    }

    LOGI("starting direct DPU playback %ux%u\n",
         (unsigned)s_dashcam_video.width,
         (unsigned)s_dashcam_video.height);
    return BK_OK;
}

bk_err_t dashcam_video_switch_file(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }
    if (!s_dashcam_video.active ||
        !s_dashcam_video.running ||
        s_dashcam_video.stop_queued ||
        s_dashcam_video.queue == NULL)
    {
        return BK_ERR_BUSY;
    }

    dashcam_video_msg_t msg =
    {
        .type = DASHCAM_VIDEO_MSG_SWITCH_FILE,
        .frame = NULL,
        .format = 0U,
    };
    snprintf(msg.path, sizeof(msg.path), "%s", path);
    return rtos_push_to_queue(&s_dashcam_video.queue, &msg, BEKEN_NO_WAIT);
}

void dashcam_video_set_ready_callback(dashcam_video_ready_cb_t callback,
                                      void *user_data)
{
    uint32_t flags = rtos_enter_critical();
    s_dashcam_video.ready_callback = callback;
    s_dashcam_video.ready_user_data = user_data;
    rtos_exit_critical(flags);
}

void dashcam_video_stop(void)
{
    if (!s_dashcam_video.active || s_dashcam_video.stop_queued)
    {
        return;
    }

    s_dashcam_video.running = false;
    s_dashcam_video.stop_queued = true;

    dashcam_video_msg_t msg =
    {
        .type = DASHCAM_VIDEO_MSG_STOP,
        .frame = NULL,
        .format = 0U,
    };
    if (s_dashcam_video.queue == NULL ||
        rtos_push_to_queue(&s_dashcam_video.queue, &msg, BEKEN_WAIT_FOREVER) != BK_OK)
    {
        LOGE("queue DPU playback stop failed\n");
        s_dashcam_video.stop_queued = false;
    }
}

bk_err_t dashcam_video_stop_sync(uint32_t timeout_ms)
{
    if (!s_dashcam_video.active)
    {
        return BK_OK;
    }

    dashcam_video_stop();
    if (s_dashcam_video.stop_complete == NULL ||
        rtos_get_semaphore(&s_dashcam_video.stop_complete, timeout_ms) != BK_OK)
    {
        LOGE("wait playback worker stop timeout\n");
        return BK_FAIL;
    }

    return s_dashcam_video.stop_result;
}

void dashcam_video_on_frame(const void *frame, uint32_t width, uint32_t height, uint32_t format)
{
    (void)frame;
    (void)width;
    (void)height;
    (void)format;
}

bool dashcam_video_submit_owned_frame(void *frame,
                                      uint32_t width,
                                      uint32_t height,
                                      uint32_t format)
{
    const bool valid_argb8888 =
        format == DASHCAM_VIDEO_FRAME_FORMAT_ARGB8888 &&
        width == DASHCAM_PLAYBACK_WIDTH &&
        height == DASHCAM_PLAYBACK_HEIGHT;

    if (s_dashcam_video.active && frame != NULL)
    {
        dashcam_video_input_stats_update();
    }
    if (!s_dashcam_video.running ||
        frame == NULL ||
        !valid_argb8888 ||
        !dashcam_video_buf_track(frame))
    {
        return false;
    }

    dashcam_video_msg_t msg =
    {
        .type = DASHCAM_VIDEO_MSG_FRAME,
        .frame = frame,
        .format = format,
    };
    if (rtos_push_to_queue(&s_dashcam_video.queue, &msg, BEKEN_NO_WAIT) != BK_OK)
    {
        (void)dashcam_video_buf_untrack(frame);
        return false;
    }

    return true;
}
