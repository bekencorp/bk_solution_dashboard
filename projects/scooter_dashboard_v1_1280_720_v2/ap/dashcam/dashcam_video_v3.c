#include "dashcam_video.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "components/bk_frame_buffer.h"
#include "components/log.h"
#include "dashcam_config.h"
#include "os/mem.h"
#include "os/os.h"
#include <driver/aon_rtc.h>
#include <cache.h>
#define TAG "d_video"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DASHCAM_RGB565_BYTES              2
#define DASHCAM_VIDEO_REFRESH_PERIOD_MS   50
#define DASHCAM_VIDEO_STATS_INTERVAL_MS   3000

/* Triple buffering lets the producer (player decode callback) convert into one
 * buffer while LVGL shows another and a third is queued, so the canvas can be
 * swapped by pointer instead of copied every frame. */
#define DASHCAM_VIDEO_BUF_COUNT           3

typedef struct
{
    bool running;
    lv_obj_t *parent;
    lv_obj_t *canvas;
    lv_obj_t *status_label;
    lv_timer_t *refresh_timer;
    void *canvas_buf;
    void *ready_buf;
    void *bufs[DASHCAM_VIDEO_BUF_COUNT];        /* owns every allocation */
    void *free_bufs[DASHCAM_VIDEO_BUF_COUNT];   /* available-for-producer stack */
    uint32_t free_count;
    uint32_t width;
    uint32_t height;

    uint32_t stats_start_ms;
    uint32_t stats_disp_frames;
} dashcam_video_ctx_t;

static dashcam_video_ctx_t s_dashcam_video;

/* Return a buffer to the producer free pool. */
static void dashcam_video_buf_release(void *buf)
{
    uint32_t flags;

    if (buf == NULL)
    {
        return;
    }

    flags = rtos_enter_critical();
    if (s_dashcam_video.free_count < DASHCAM_VIDEO_BUF_COUNT)
    {
        s_dashcam_video.free_bufs[s_dashcam_video.free_count++] = buf;
    }
    rtos_exit_critical(flags);
}

/* Producer: grab a free buffer to convert the next frame into. */
static uint16_t *dashcam_video_back_buffer_take(void)
{
    uint16_t *buf = NULL;
    uint32_t flags = rtos_enter_critical();

    if (s_dashcam_video.free_count > 0)
    {
        buf = (uint16_t *)s_dashcam_video.free_bufs[--s_dashcam_video.free_count];
    }

    rtos_exit_critical(flags);
    return buf;
}

/* Producer: hand a freshly converted frame to the consumer, dropping any
 * previously queued (un-shown) frame back to the free pool. */
static void dashcam_video_back_buffer_publish(uint16_t *buf)
{
    uint32_t flags;
    void *stale = NULL;

    if (buf == NULL)
    {
        return;
    }

    flags = rtos_enter_critical();
    stale = s_dashcam_video.ready_buf;
    s_dashcam_video.ready_buf = buf;
    rtos_exit_critical(flags);

    if (stale != NULL)
    {
        dashcam_video_buf_release(stale);
    }
}

static void dashcam_video_fill_placeholder(void)
{
    uint8_t *buf = (uint8_t *)s_dashcam_video.canvas_buf;

    if (buf == NULL)
    {
        return;
    }

    os_memset(buf, 0, (size_t)s_dashcam_video.width * s_dashcam_video.height * DASHCAM_RGB565_BYTES);
}

static void dashcam_video_update_status(void)
{
    if (s_dashcam_video.status_label != NULL && lv_obj_is_valid(s_dashcam_video.status_label))
    {
        lv_label_set_text(s_dashcam_video.status_label, "PLAYBACK");
    }
}

static void dashcam_video_stats_maybe_log(uint32_t now_ms)
{
    uint32_t elapsed_ms = now_ms - s_dashcam_video.stats_start_ms;

    if (elapsed_ms < DASHCAM_VIDEO_STATS_INTERVAL_MS)
    {
        return;
    }

    uint32_t disp_frames = s_dashcam_video.stats_disp_frames;
    uint32_t disp_fps_x10 = (disp_frames * 10000u) / elapsed_ms;

    LOGI("playback stats %ums: disp=%u.%ufps\n",
         (unsigned)elapsed_ms,
         (unsigned)(disp_fps_x10 / 10u), (unsigned)(disp_fps_x10 % 10u));

    s_dashcam_video.stats_start_ms = now_ms;
    s_dashcam_video.stats_disp_frames = 0;
}

static void dashcam_video_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    void *ready_buf = NULL;

    if (!s_dashcam_video.running ||
        s_dashcam_video.canvas == NULL ||
        !lv_obj_is_valid(s_dashcam_video.canvas) ||
        s_dashcam_video.canvas_buf == NULL)
    {
        return;
    }

    uint32_t flags = rtos_enter_critical();
    if (s_dashcam_video.ready_buf != NULL)
    {
        ready_buf = s_dashcam_video.ready_buf;
        s_dashcam_video.ready_buf = NULL;
    }
    rtos_exit_critical(flags);

    if (ready_buf != NULL)
    {
        /*
         * Swap the canvas to the freshly converted buffer by pointer instead of
         * memcpy'ing the frame PSRAM->PSRAM every cycle. The buffer that was on
         * the canvas was already composited by LVGL in a previous lv_timer
         * cycle, so once set_buffer points elsewhere it is safe to hand straight
         * back to the producer.
         */
        void *old_canvas = s_dashcam_video.canvas_buf;

        s_dashcam_video.canvas_buf = ready_buf;
        lv_canvas_set_buffer(s_dashcam_video.canvas, ready_buf,
                             s_dashcam_video.width, s_dashcam_video.height,
                             LV_COLOR_FORMAT_RGB565);
        dashcam_video_buf_release(old_canvas);

        s_dashcam_video.stats_disp_frames++;
    }

    lv_obj_invalidate(s_dashcam_video.canvas);
    dashcam_video_stats_maybe_log(rtos_get_time());
}

static bk_err_t dashcam_video_start_internal(lv_obj_t *parent, uint32_t width, uint32_t height)
{
    LOGD("start parent=%p %ux%u\n", (void *)parent, (unsigned)width, (unsigned)height);

    if (parent == NULL || !lv_obj_is_valid(parent) || width == 0 || height == 0)
    {
        return BK_FAIL;
    }

    if (s_dashcam_video.running)
    {
        if (s_dashcam_video.parent == parent)
        {
            return BK_OK;
        }

        dashcam_video_stop();
    }

    const size_t buf_size = (size_t)width * (size_t)height * DASHCAM_RGB565_BYTES;
    void *bufs[DASHCAM_VIDEO_BUF_COUNT] = {0};
    uint32_t i;

    /* Keep canvas buffers in the display slab so decoded playback frames use the
     * same DPU-reachable aperture. */
    for (i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        bufs[i] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, buf_size);
        if (bufs[i] == NULL)
        {
            LOGE("frame_buffer_malloc(%u) #%u failed\n", (unsigned)buf_size, (unsigned)i);
            while (i-- > 0)
            {
                bk_frame_buffer_free(bufs[i]);
            }
            return BK_ERR_NO_MEM;
        }
    }

    lv_obj_t *canvas = lv_canvas_create(parent);
    if (canvas == NULL)
    {
        for (i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
        {
            bk_frame_buffer_free(bufs[i]);
        }
        return BK_FAIL;
    }

    s_dashcam_video.parent = parent;
    s_dashcam_video.canvas = canvas;
    /* bufs[0] is the initial canvas; the rest seed the producer free pool. */
    for (i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        s_dashcam_video.bufs[i] = bufs[i];
    }
    s_dashcam_video.canvas_buf = bufs[0];
    s_dashcam_video.ready_buf = NULL;
    s_dashcam_video.free_count = 0;
    for (i = 1; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        s_dashcam_video.free_bufs[s_dashcam_video.free_count++] = bufs[i];
    }
    s_dashcam_video.width = width;
    s_dashcam_video.height = height;
    s_dashcam_video.running = true;
    s_dashcam_video.stats_start_ms = rtos_get_time();
    s_dashcam_video.stats_disp_frames = 0;

    lv_canvas_set_buffer(canvas, bufs[0], width, height, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, width, height);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    s_dashcam_video.refresh_timer = lv_timer_create(dashcam_video_refresh_timer_cb,
                                                    DASHCAM_VIDEO_REFRESH_PERIOD_MS,
                                                    NULL);

    s_dashcam_video.status_label = lv_label_create(parent);
    if (s_dashcam_video.status_label != NULL)
    {
        lv_obj_set_style_text_color(s_dashcam_video.status_label,
                                    lv_color_hex(0x1EF2C4),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(s_dashcam_video.status_label,
                                   &lv_font_montserrat_14,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(s_dashcam_video.status_label, LV_ALIGN_BOTTOM_LEFT, 18, -16);
    }

    dashcam_video_fill_placeholder();
    dashcam_video_update_status();
    if (s_dashcam_video.canvas != NULL && lv_obj_is_valid(s_dashcam_video.canvas))
    {
        lv_obj_invalidate(s_dashcam_video.canvas);
    }

    LOGI("start playback sink %ux%u\n", (unsigned)width, (unsigned)height);
    return BK_OK;
}

bk_err_t dashcam_video_start_sink(lv_obj_t *parent)
{
    /* The standalone PP down-scales + converts decoded NV12 clips to RGB565 at
     * the fixed playback size (DASHCAM_PLAYBACK_WIDTH x HEIGHT), so the canvas is
     * created at that size and frames arrive already sized to it. */
    return dashcam_video_start_internal(parent,
                                        DASHCAM_PLAYBACK_WIDTH,
                                        DASHCAM_PLAYBACK_HEIGHT);
}

void dashcam_video_stop(void)
{
    LOGD("stop (running=%d)\n", (int)s_dashcam_video.running);
    s_dashcam_video.running = false;

    if (s_dashcam_video.refresh_timer != NULL)
    {
        lv_timer_delete(s_dashcam_video.refresh_timer);
        s_dashcam_video.refresh_timer = NULL;
    }

    if (s_dashcam_video.canvas != NULL && lv_obj_is_valid(s_dashcam_video.canvas))
    {
        lv_obj_del(s_dashcam_video.canvas);
    }

    if (s_dashcam_video.status_label != NULL && lv_obj_is_valid(s_dashcam_video.status_label))
    {
        lv_obj_del(s_dashcam_video.status_label);
    }

    for (uint32_t i = 0; i < DASHCAM_VIDEO_BUF_COUNT; i++)
    {
        if (s_dashcam_video.bufs[i] != NULL)
        {
            bk_frame_buffer_free(s_dashcam_video.bufs[i]);
            s_dashcam_video.bufs[i] = NULL;
        }
    }
    s_dashcam_video.free_count = 0;

    s_dashcam_video.parent = NULL;
    s_dashcam_video.canvas = NULL;
    s_dashcam_video.status_label = NULL;
    s_dashcam_video.canvas_buf = NULL;
    s_dashcam_video.ready_buf = NULL;
    s_dashcam_video.width = 0;
    s_dashcam_video.height = 0;
}

void dashcam_video_on_frame(const void *frame, uint32_t width, uint32_t height, uint32_t format)
{
    /* Frames arrive as RGB565 already scaled to the canvas size by the standalone
     * PP (NV12 -> RGB565); just copy into a producer buffer and publish. */
    if (!s_dashcam_video.running ||
        frame == NULL ||
        format != DASHCAM_VIDEO_FRAME_FORMAT_RGB565 ||
        width != s_dashcam_video.width ||
        height != s_dashcam_video.height ||
        s_dashcam_video.canvas_buf == NULL)
    {
        return;
    }

    uint16_t *dst = dashcam_video_back_buffer_take();
    if (dst == NULL)
    {
        return;
    }

    const size_t frame_bytes = (size_t)width * (size_t)height * DASHCAM_RGB565_BYTES;

    /*
     * The standalone PP wrote this RGB565 frame to PSRAM via DMA. With AP PSRAM
     * D-cache enabled, the CPU could otherwise memcpy stale cached bytes for the
     * output range, so invalidate it before reading. (Mirrors flush_dcache() on
     * the HW->CPU direction, as in jpeg_stream_pipeline / gerrit 96591.)
     */
    flush_dcache((void *)frame, (long)frame_bytes);

    memcpy(dst, frame, frame_bytes);
    dashcam_video_back_buffer_publish(dst);
}
