#include "dashcam_video.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "components/bk_frame_buffer.h"
#include "components/log.h"
#include "dashcam_camera.h"
#include "dashcam_config.h"
#include "display_ui.h"
#include "gpu_core.h"
#include "lv_camera_blend.h"
#include "lv_vendor.h"
#include "modules/vg_lite_gpu/vg_lite.h"
#include "os/mem.h"
#include "os/os.h"
#include <cache.h>
#include <driver/aon_rtc.h>

#define TAG "dashcam_video"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DASHCAM_RGB565_BYTES    2
#define DASHCAM_NV12_BYTES_PER_2_PIXELS 3
#define DASHCAM_CAMERA_READ_TIMEOUT_MS 100
#define DASHCAM_CAMERA_STOP_WAIT_MS    600
#define DASHCAM_VIDEO_STATS_INTERVAL_MS 3000
#define DASHCAM_VIDEO_REFRESH_PERIOD_MS 50

/*
 * Page-scoped GPU compositing for the live preview (req5_design.md §13).
 *
 * =1: live preview does NOT use an LVGL canvas. The read task pull-reads SP/MP
 *     NV12 frames and hands them to a CPU0 compose worker, which composites the
 *     latest LVGL UI snapshot + camera frame with lv_camera_blend and pushes the
 *     preallocated RGB565 output framebuffer directly to DPU.
 * =0: fall back to the legacy canvas + software conversion path below.
 *
 * Playback (start_read == false) always keeps the canvas path; blend only ever
 * applies to the live read-task path. See §13.13(5).
 */
#ifndef DASHCAM_PREVIEW_USE_BLEND
#define DASHCAM_PREVIEW_USE_BLEND 0
#endif

/* Camera-layer placement in the DPU framebuffer coordinate system. LVGL may use
 * a rotated logical resolution, but bk_widgets_flush_cb receives the final DPU
 * framebuffer; for the 720x1280 logical / ROTATE_270 UI that buffer is
 * 1280x720 physical. */
#ifndef DASHCAM_BLEND_CAM_ROTATE
#define DASHCAM_BLEND_CAM_ROTATE 0
#endif

/* LVGL UI rotation (deg) the display is configured with. The page-scoped
 * composite writes the panel-native framebuffer directly (bypassing LVGL's
 * flush rotation), so the camera overlay must apply the same logical->physical
 * transform LVGL would (lv_display_rotate_area). */
#ifndef DASHCAM_BLEND_UI_ROTATION
#if defined(CONFIG_SCOOTER_UI_ROTATION)
#define DASHCAM_BLEND_UI_ROTATION CONFIG_SCOOTER_UI_ROTATION
#else
#define DASHCAM_BLEND_UI_ROTATION 0
#endif
#endif

/* The DPU framebuffer we composite into is the *panel-native* buffer that LVGL
 * flushes (display_ui lvgl_start_internal allocates it at
 * CONFIG_SCOOTER_LVGL_HOR_RES x CONFIG_SCOOTER_LVGL_VER_RES, i.e. 720x1280 for
 * this portrait panel). LVGL renders the UI into it already rotated by
 * CONFIG_SCOOTER_UI_ROTATION, so the buffer is ALWAYS the panel resolution -
 * the rotation does NOT swap the framebuffer width/height. The earlier swap
 * here assumed a 1280x720 physical buffer and corrupted the camera overlay
 * stride (full-frame bg memcpy was unaffected, which is why only the camera box
 * sheared into stripes). */
#if defined(CONFIG_SCOOTER_LVGL_HOR_RES) && defined(CONFIG_SCOOTER_LVGL_VER_RES)
#define DASHCAM_BLEND_FB_WIDTH CONFIG_SCOOTER_LVGL_HOR_RES
#define DASHCAM_BLEND_FB_HEIGHT CONFIG_SCOOTER_LVGL_VER_RES
#else
#define DASHCAM_BLEND_FB_WIDTH 720
#define DASHCAM_BLEND_FB_HEIGHT 1280
#endif

#ifndef DASHCAM_BLEND_CAM_DST_X
#if (DASHCAM_BLEND_CAM_ROTATE == 90) || (DASHCAM_BLEND_CAM_ROTATE == 270)
#define DASHCAM_BLEND_CAM_DST_X (((DASHCAM_BLEND_FB_WIDTH) > (DASHCAM_PREVIEW_HEIGHT)) ? \
                                 (((DASHCAM_BLEND_FB_WIDTH) - (DASHCAM_PREVIEW_HEIGHT)) / 2) : 0)
#else
#define DASHCAM_BLEND_CAM_DST_X (((DASHCAM_BLEND_FB_WIDTH) > (DASHCAM_PREVIEW_WIDTH)) ? \
                                 (((DASHCAM_BLEND_FB_WIDTH) - (DASHCAM_PREVIEW_WIDTH)) / 2) : 0)
#endif
#endif
#ifndef DASHCAM_BLEND_CAM_DST_Y
#if (DASHCAM_BLEND_CAM_ROTATE == 90) || (DASHCAM_BLEND_CAM_ROTATE == 270)
#define DASHCAM_BLEND_CAM_DST_Y (((DASHCAM_BLEND_FB_HEIGHT) > (DASHCAM_PREVIEW_WIDTH)) ? \
                                 (((DASHCAM_BLEND_FB_HEIGHT) - (DASHCAM_PREVIEW_WIDTH)) / 2) : 0)
#else
#define DASHCAM_BLEND_CAM_DST_Y (((DASHCAM_BLEND_FB_HEIGHT) > (DASHCAM_PREVIEW_HEIGHT)) ? \
                                 (((DASHCAM_BLEND_FB_HEIGHT) - (DASHCAM_PREVIEW_HEIGHT)) / 2) : 0)
#endif
#endif

/* Triple buffering lets the producer convert into one buffer while LVGL shows
 * another and a third is queued, so the canvas can be swapped by pointer
 * instead of copied (req: kill the per-frame ~1 MB PSRAM memcpy). */
#define DASHCAM_VIDEO_BUF_COUNT 3

typedef struct
{
    bool running;
    dashcam_video_source_t source;
    lv_obj_t *parent;
    lv_obj_t *canvas;
    lv_obj_t *status_label;
    lv_timer_t *refresh_timer;
    void *canvas_buf;
    void *ready_buf;
    void *bufs[DASHCAM_VIDEO_BUF_COUNT];          /* owns every allocation */
    void *free_bufs[DASHCAM_VIDEO_BUF_COUNT];     /* available-for-producer stack */
    uint32_t free_count;
    uint32_t width;
    uint32_t height;
    beken_thread_t read_thread;
    beken_semaphore_t read_exit_sem;
    volatile bool read_stop_requested;
    uint32_t stats_start_ms;
    uint32_t stats_read_frames;
    uint32_t stats_read_failures;
    uint64_t stats_read_us;     /* accumulated camera read time (successful reads) */
    uint64_t stats_conv_us;     /* accumulated NV12->RGB565 convert time */
    uint32_t stats_conv_frames; /* frames converted */
    uint64_t stats_copy_us;     /* accumulated ready->canvas copy time */
    uint32_t stats_disp_frames; /* frames pushed to the canvas */
    bool conv_path_logged;      /* one-shot: log canvas conversion path */

    /* --- disp-fps RCA probe (temporary) ----------------------------------
     * Splits the per-frame LVGL pipeline cost driving the on-screen rate:
     *   refr  = full LVGL refresh cycle (REFR_START -> after flush_cb)
     *   render= rendering only (RENDER_START -> RENDER_READY); large value here
     *           includes time LVGL blocks on the GPU lock held by preview code
     *   flush = RENDER_READY -> REFR_READY (HPDMA transfer + lv_disp_sem wait)
     *   gpuw  = time the GPU path waits to acquire lv_vendor_gpu_lock
     *   loop  = wall gap between refresh_timer_cb runs = effective LVGL task rate
     * Diagnosis: render-heavy => GPU contention; flush-heavy => panel transfer;
     * loop >> (render+flush) => LVGL task starved of CPU by read/conv/H264. */
    bool perf_probe_registered;
    uint64_t stats_refr_start_us;
    uint64_t stats_render_start_us;   /* 0 => no redraw happened this cycle */
    uint64_t stats_render_ready_us;
    uint64_t stats_refr_sum_us;
    uint32_t stats_refr_count;        /* refresh cycles incl. empty (no redraw) */
    uint64_t stats_render_sum_us;
    uint32_t stats_render_count;      /* cycles that actually rendered */
    uint64_t stats_flush_sum_us;
    uint64_t stats_gpu_wait_sum_us;
    uint32_t stats_gpu_wait_max_us;
    uint32_t stats_gpu_lock_count;
    uint64_t stats_cb_last_us;
    uint64_t stats_cb_gap_sum_us;
    uint32_t stats_cb_gap_max_us;
    uint32_t stats_cb_count;
} dashcam_video_ctx_t;

static dashcam_video_ctx_t s_dashcam_video =
{
    .source = DASHCAM_VIDEO_SOURCE_FRONT_MIPI,
};

#if DASHCAM_PREVIEW_USE_BLEND
static bool s_blend_gpu_inited = false;
static bool s_blend_gpu_init_failed = false;

static bool dashcam_video_blend_gpu_ensure_init(void)
{
    vg_lite_error_t ret;

    if (s_blend_gpu_inited)
    {
        return true;
    }
    if (s_blend_gpu_init_failed)
    {
        return false;
    }

    bk_gpu_driver_init();
    ret = vg_lite_init(0, 0);
    if (ret != VG_LITE_SUCCESS)
    {
        LOGE("blend_start: vg_lite_init failed: %d\n", ret);
        s_blend_gpu_init_failed = true;
        return false;
    }

    s_blend_gpu_inited = true;
    LOGI("vg_lite GPU context ready for dashcam blend\n");
    return true;
}

/* ---- page-scoped GPU compositing state (live preview only) --------------
 *
 * Memory budget note (req5_design.md §13): the GPU can only reach the
 * PSRAM_MEM_SLAB_UNCODED aperture (0x60000000, 0xAC0000 = ~11.27 MB). During
 * preview that aperture already holds the LVGL framebuffer + the camera NV12
 * ring, so the blend path keeps its own footprint minimal:
 *   - no separate staging buffer: the LVGL flush memcpys straight into the
 *     lv_camera_blend internal bg (same RGB565 format), saving one full frame
 *     and a redundant GPU blit;
 *   - two DPU output buffers to avoid rewriting the buffer still scanned by DPU.
 * All sizes use the real RGB565 payload (w*h*2), not the LVGL fb size which is
 * over-allocated at sizeof(lv_color_t)=3 bytes/pixel by display_ui. */
#define DASHCAM_BLEND_OUT_BUF_COUNT 2

#ifndef DASHCAM_BLEND_MIN_OUTPUT_REUSE_MS
#define DASHCAM_BLEND_MIN_OUTPUT_REUSE_MS 35
#endif

/* Keep one NV12 handoff slot to leave enough UNCODED heap for tear-free DPU
 * double buffering. The read task naturally back-pressures while GPU composes. */
#define DASHCAM_BLEND_CAM_SLOTS 1

/* Lock-free handoff state per camera slot (single writer per transition):
 *   read task:    FREE  -> (fill + cache flush) -> READY, then signal worker
 *   compose work: READY -> BUSY -> (compose + DPU flush) -> FREE
 * The read task only ever touches FREE slots; the worker only ever touches
 * READY/BUSY slots, so no mutex is needed. */
enum
{
    DASHCAM_BLEND_SLOT_FREE = 0,
    DASHCAM_BLEND_SLOT_READY,
    DASHCAM_BLEND_SLOT_BUSY,
};

static lv_camera_blend_handle_t s_blend = NULL;
static void *s_blend_bg = NULL;                 /* internal bg buf (owned by s_blend), CPU memcpy target */
static volatile bool s_blend_dirty = false;     /* set by display_ui flush, read by compose worker */
static void *s_blend_out_bufs[DASHCAM_BLEND_OUT_BUF_COUNT] = {NULL};
static volatile bool s_blend_out_busy[DASHCAM_BLEND_OUT_BUF_COUNT] = {false};
/* UI-background generation tracking: the full bg memcpy into an out buffer
 * (1.8 MB on uncached PSRAM) only needs to happen when the UI actually changed.
 * s_blend_bg_gen bumps every time the LVGL flush hook delivers a new UI frame;
 * each out buffer remembers the generation it currently holds so an unchanged
 * UI (refr~0/s on the dashcam page) skips the expensive memcpy and only the
 * camera box is redrawn (it fully overwrites its own previous-frame pixels). */
static uint32_t s_blend_bg_gen = 1;
static uint32_t s_blend_out_bg_gen[DASHCAM_BLEND_OUT_BUF_COUNT] = {0};
static uint16_t s_blend_w = 0;
static uint16_t s_blend_h = 0;
static uint32_t s_blend_fb_size = 0;            /* LVGL fb size (over-allocated, w*h*3) */
static uint32_t s_blend_real_size = 0;          /* real RGB565 payload, w*h*2 */
static bool s_blend_active = false;
static uint32_t s_blend_compose_ok = 0;
static uint32_t s_blend_compose_fail = 0;
static uint32_t s_blend_drop_no_buf = 0;
static bool s_blend_first_frame_logged = false;
static uint64_t s_blend_last_flush_us = 0;
static uint16_t s_blend_cam_dst_x = DASHCAM_BLEND_CAM_DST_X;
static uint16_t s_blend_cam_dst_y = DASHCAM_BLEND_CAM_DST_Y;

/* --- camera frame handoff: read task (any core) -> GPU compose worker (CPU0) -
 * The single GPU is SMP-unsafe and its IRQ is serviced on CPU0; only the compose
 * worker touches vg_lite, so it is the sole GPU owner and is pinned to CPU0. The
 * read task no longer runs any GPU op nor the NV12 cache maintenance that raced
 * the ISP DMA path (the heartbeat-wedge root cause); it just reads, cleans the
 * frame's cache lines, and hands the slot to the worker. */
static void *s_blend_cam_buf[DASHCAM_BLEND_CAM_SLOTS] = {NULL};
static volatile uint8_t s_blend_cam_state[DASHCAM_BLEND_CAM_SLOTS] = {0};
static uint32_t s_blend_cam_size = 0;           /* NV12 frame size = cam_w*cam_h*3/2 */
static uint16_t s_blend_cam_w = 0;
static uint16_t s_blend_cam_h = 0;
static uint16_t s_blend_cam_buf_h = 0;
static beken_thread_t s_blend_worker_thread = NULL;
static beken_semaphore_t s_blend_worker_sem = NULL;       /* read task -> worker wakeups */
static beken_semaphore_t s_blend_worker_exit_sem = NULL;  /* worker -> stop() join */
static volatile bool s_blend_worker_stop = false;

static int dashcam_video_blend_out_free_cb(void *arg)
{
    for (uint32_t i = 0; i < DASHCAM_BLEND_OUT_BUF_COUNT; i++)
    {
        if (arg == s_blend_out_bufs[i])
        {
            s_blend_out_busy[i] = false;
            return 0;
        }
    }
    return 0;
}

/*
 * Compose one camera NV12 frame over the latest LVGL UI snapshot on the GPU and
 * push the result to the DPU. Runs ONLY on the dedicated CPU0 compose worker,
 * which is the single GPU owner. The shared GPU lock still serialises against
 * LVGL's own GPU use (req5_design.md §13.13). The NV12 frame is already cache-
 * clean (flushed by the read task before handoff), so no cache op on the camera
 * buffer happens here.
 */
static bool dashcam_video_blend_compose_and_flush(const uint8_t *rgb565,
                                                  uint32_t width,
                                                  uint32_t height)
{
    if (s_blend == NULL || rgb565 == NULL)
    {
        return false;
    }

    if (DASHCAM_BLEND_OUT_BUF_COUNT == 1 && s_blend_last_flush_us != 0)
    {
        uint64_t now_us = bk_aon_rtc_get_us();
        uint32_t elapsed_ms = (uint32_t)((now_us - s_blend_last_flush_us) / 1000u);
        if (elapsed_ms < DASHCAM_BLEND_MIN_OUTPUT_REUSE_MS)
        {
            rtos_delay_milliseconds(DASHCAM_BLEND_MIN_OUTPUT_REUSE_MS - elapsed_ms);
        }
    }

    int idx = -1;
    for (uint32_t i = 0; i < DASHCAM_BLEND_OUT_BUF_COUNT; i++)
    {
        if (DASHCAM_BLEND_OUT_BUF_COUNT == 1 || !s_blend_out_busy[i])
        {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
    {
        /* All output frames still being scanned out by the DPU; drop this one. */
        s_blend_drop_no_buf++;
        return false;
    }
    void *out = s_blend_out_bufs[idx];

    /* CPU page-composite. This nano GPU corrupts (shears) any blit into a linear
     * RGB565 *sub-rectangle* of the target: full-frame blits are fine (so the UI
     * background always rendered cleanly) but the smaller camera box came out
     * striped, regardless of NV12-vs-RGB565 source or blit vs blit_rect. Both
     * layers are already RGB565, so we compose them on the CPU into the single
     * output buffer - seed the full UI snapshot, then overlay the camera frame.
     * Keeps the tear-free single-buffer (no LVGL canvas) design. */
    if (s_blend_dirty)
    {
        /* New UI frame in the bg snapshot: invalidate every out buffer's copy. */
        s_blend_dirty = false;
        s_blend_bg_gen++;
    }

    /* 1) UI background: only re-copy the full 1.8 MB snapshot when this out
     *    buffer is stale. An unchanged UI just keeps its existing bg and lets
     *    the camera overlay below repaint its (fixed) box. */
    if (s_blend_out_bg_gen[idx] != s_blend_bg_gen)
    {
        os_memcpy(out, s_blend_bg, s_blend_real_size);
        s_blend_out_bg_gen[idx] = s_blend_bg_gen;
    }

    /* 2) Overlay the camera frame, applying the SAME logical->physical transform
     *    LVGL applies for the display rotation (see lv_display_rotate_area):
     *    s_blend_cam_dst_x/y are LVGL *logical* coords (from lv_obj_get_coords),
     *    but `out` is the panel-native (physical) framebuffer LVGL flushes into.
     *    The camera frame is `width`x`height`, stride `width`, in upright logical
     *    orientation; we scatter it into the physical buffer so that, after the
     *    panel's rotation, it lands in the preview box upright - exactly what the
     *    legacy LVGL canvas path produced. */
    {
        const uint8_t *src = rgb565;
        uint16_t *out16 = (uint16_t *)out;
        const uint32_t lx0 = s_blend_cam_dst_x;     /* logical top-left x */
        const uint32_t ly0 = s_blend_cam_dst_y;     /* logical top-left y */

#if (DASHCAM_BLEND_UI_ROTATION == 270)
        /* ROTATE_270: px = (hor_res-1) - (ly0+sy); py = lx0+sx.
         * hor_res == physical framebuffer width == s_blend_w. */
        for (uint32_t sy = 0; sy < height; sy++)
        {
            int px = (int)s_blend_w - 1 - (int)(ly0 + sy);
            if (px < 0 || px >= (int)s_blend_w)
            {
                continue;
            }
            const uint16_t *srow = (const uint16_t *)(src + (size_t)sy * width * 2u);
            uint16_t *dcol = out16 + (size_t)lx0 * s_blend_w + (uint32_t)px;
            for (uint32_t sx = 0; sx < width; sx++)
            {
                if (lx0 + sx >= s_blend_h)
                {
                    break;
                }
                *dcol = srow[sx];
                dcol += s_blend_w;          /* py = lx0 + sx -> next physical row */
            }
        }
#elif (DASHCAM_BLEND_UI_ROTATION == 90)
        /* ROTATE_90: px = (ly0+sy); py = (ver_res-1) - (lx0+sx). */
        for (uint32_t sy = 0; sy < height; sy++)
        {
            uint32_t px = ly0 + sy;
            if (px >= s_blend_w)
            {
                continue;
            }
            const uint16_t *srow = (const uint16_t *)(src + (size_t)sy * width * 2u);
            for (uint32_t sx = 0; sx < width; sx++)
            {
                int py = (int)s_blend_h - 1 - (int)(lx0 + sx);
                if (py < 0)
                {
                    break;
                }
                out16[(uint32_t)py * s_blend_w + px] = srow[sx];
            }
        }
#else
        /* ROTATE_NONE: physical == logical; straight row copy, clamped. */
        for (uint32_t sy = 0; sy < height; sy++)
        {
            uint32_t py = ly0 + sy;
            if (py >= s_blend_h)
            {
                break;
            }
            const uint16_t *srow = (const uint16_t *)(src + (size_t)sy * width * 2u);
            uint16_t *drow = out16 + (size_t)py * s_blend_w + lx0;
            uint32_t n = (lx0 + width > s_blend_w) ? (s_blend_w - lx0) : width;
            for (uint32_t sx = 0; sx < n; sx++)
            {
                drow[sx] = srow[sx];
            }
        }
#endif
    }

    /* Publish to the DPU: clean the CPU-written frame out of cache. */
    arch_dcache_flush_range(out, s_blend_real_size);

    s_blend_out_busy[idx] = true;
    if (display_ui_blend_flush(out, dashcam_video_blend_out_free_cb) != BK_OK)
    {
        s_blend_out_busy[idx] = false;
        LOGW("blend flush to DPU failed (out=%p)\n", out);
        return false;
    }
    s_blend_last_flush_us = bk_aon_rtc_get_us();

    s_blend_compose_ok++;
    s_dashcam_video.stats_disp_frames++;
    if (!s_blend_first_frame_logged)
    {
        s_blend_first_frame_logged = true;
        LOGI("blend first frame on screen: cam %ux%u rot=%d dst=(%d,%d) out=%ux%u idx=%d\n",
             (unsigned)width, (unsigned)height,
             (int)DASHCAM_BLEND_CAM_ROTATE,
             (int)s_blend_cam_dst_x, (int)s_blend_cam_dst_y,
             (unsigned)s_blend_w, (unsigned)s_blend_h, idx);
    }
    return true;
}

/*
 * Dedicated GPU compose worker. Modelled on lv_camera_blend_async_worker(): a
 * single thread drains camera frames and performs every vg_lite operation, so
 * the GPU has exactly one owner (besides LVGL's own serialised use). Pinned to
 * CPU0 because the GPU IRQ is serviced there. It runs no cache maintenance on
 * the camera buffer (the read task cleaned it before handoff).
 */
static void dashcam_video_blend_worker(void *arg)
{
    (void)arg;
    LOGI("blend worker: start (CPU0, single GPU owner)\n");

    while (!s_blend_worker_stop)
    {
        /* Timed wait so a missed signal can never wedge the worker. */
        (void)rtos_get_semaphore(&s_blend_worker_sem, 100);

        if (s_blend_worker_stop)
        {
            break;
        }

        for (uint32_t i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
        {
            if (s_blend_cam_state[i] != DASHCAM_BLEND_SLOT_READY)
            {
                continue;
            }

            s_blend_cam_state[i] = DASHCAM_BLEND_SLOT_BUSY;

            uint64_t t0 = bk_aon_rtc_get_us();
            bool ok = dashcam_video_blend_compose_and_flush((const uint8_t *)s_blend_cam_buf[i],
                                                            s_blend_cam_w, s_blend_cam_h);
            if (ok)
            {
                /* CPU page-composite time (bg seed + rotated camera overlay +
                 * DPU flush). Keyed by stats_disp_frames, bumped inside compose
                 * on success. Kept separate from the NV12->RGB565 convert time
                 * (stats_conv_*, accumulated on the read task). */
                s_dashcam_video.stats_copy_us += bk_aon_rtc_get_us() - t0;
            }

            s_blend_cam_state[i] = DASHCAM_BLEND_SLOT_FREE;
        }
    }

    LOGI("blend worker: exit (ok=%u fail=%u)\n",
         (unsigned)s_blend_compose_ok, (unsigned)s_blend_compose_fail);
    if (s_blend_worker_exit_sem != NULL)
    {
        rtos_set_semaphore(&s_blend_worker_exit_sem);
    }
    s_blend_worker_thread = NULL;
    rtos_delete_thread(NULL);
}

static void dashcam_video_blend_worker_stop(void)
{
    if (s_blend_worker_thread == NULL)
    {
        /* Worker never started; just tidy any half-created primitives. */
        if (s_blend_worker_sem != NULL)
        {
            rtos_deinit_semaphore(&s_blend_worker_sem);
            s_blend_worker_sem = NULL;
        }
        if (s_blend_worker_exit_sem != NULL)
        {
            rtos_deinit_semaphore(&s_blend_worker_exit_sem);
            s_blend_worker_exit_sem = NULL;
        }
        return;
    }

    s_blend_worker_stop = true;
    if (s_blend_worker_sem != NULL)
    {
        rtos_set_semaphore(&s_blend_worker_sem);
    }

    if (s_blend_worker_exit_sem != NULL)
    {
        bk_err_t ret = rtos_get_semaphore(&s_blend_worker_exit_sem, DASHCAM_CAMERA_STOP_WAIT_MS);
        if (ret != BK_OK)
        {
            LOGW("blend worker exit wait timeout: %d\n", ret);
        }
    }

    if (s_blend_worker_sem != NULL)
    {
        rtos_deinit_semaphore(&s_blend_worker_sem);
        s_blend_worker_sem = NULL;
    }
    if (s_blend_worker_exit_sem != NULL)
    {
        rtos_deinit_semaphore(&s_blend_worker_exit_sem);
        s_blend_worker_exit_sem = NULL;
    }
    s_blend_worker_thread = NULL;
}

static bk_err_t dashcam_video_blend_start(lv_obj_t *parent)
{
    if (s_blend_active)
    {
        LOGW("blend_start: already active\n");
        return BK_OK;
    }

    uint16_t lvgl_w = 0;
    uint16_t lvgl_h = 0;
    display_ui_get_lvgl_dims(&lvgl_w, &lvgl_h, &s_blend_fb_size);
    s_blend_w = (uint16_t)DASHCAM_BLEND_FB_WIDTH;
    s_blend_h = (uint16_t)DASHCAM_BLEND_FB_HEIGHT;
    if (lvgl_w == 0 || lvgl_h == 0 || s_blend_w == 0 || s_blend_h == 0 ||
        s_blend_fb_size == 0)
    {
        LOGE("blend_start: bad dims lvgl=%ux%u dpu=%ux%u fb=%u\n",
             (unsigned)lvgl_w, (unsigned)lvgl_h,
             (unsigned)s_blend_w, (unsigned)s_blend_h,
             (unsigned)s_blend_fb_size);
        return BK_FAIL;
    }

    /* Real RGB565 payload (2 bytes/pixel). The LVGL fb is over-allocated at
     * sizeof(lv_color_t)=3 bytes/pixel; only the lower 2/3 carries RGB565. */
    s_blend_real_size = (uint32_t)s_blend_w * (uint32_t)s_blend_h * 2u;
    if (s_blend_real_size != s_blend_fb_size)
    {
        LOGW("blend_start: dpu payload %u differs from LVGL fb size %u\n",
             (unsigned)s_blend_real_size, (unsigned)s_blend_fb_size);
    }

    lv_obj_update_layout(parent);
    lv_area_t area;
    lv_obj_get_coords(parent, &area);
    if (area.x1 >= 0 && area.y1 >= 0)
    {
        s_blend_cam_dst_x = (uint16_t)area.x1;
        s_blend_cam_dst_y = (uint16_t)area.y1;
    }
    else
    {
        s_blend_cam_dst_x = DASHCAM_BLEND_CAM_DST_X;
        s_blend_cam_dst_y = DASHCAM_BLEND_CAM_DST_Y;
    }

    /* Ground-truth the GPU-reachable aperture before allocating anything. */
    LOGI("blend_start: UNCODED heap before alloc (need bg+out = 2x %u):\n",
         (unsigned)s_blend_real_size);
    bk_mem_slab_dump_heap(MEM_SLAB_HEAP_UNCODED);

    if (!dashcam_video_blend_gpu_ensure_init())
    {
        LOGE("blend_start: vg_lite init failed\n");
        return BK_FAIL;
    }

    lv_camera_blend_config_t cfg = {0};
    cfg.width = s_blend_w;
    cfg.height = s_blend_h;
    cfg.lvgl_format = BK_PIXEL_FORMAT_RGB565;
    cfg.lvgl_compress = false;
    cfg.output_format = BK_PIXEL_FORMAT_RGB565;
    cfg.output_compress = false;

    if (lv_camera_blend_init(&s_blend, &cfg) != BK_OK || s_blend == NULL)
    {
        LOGE("blend_start: lv_camera_blend_init failed\n");
        s_blend = NULL;
        return BK_FAIL;
    }

    /* No separate staging buffer: feed the LVGL frame straight into the blend
     * internal bg via CPU memcpy in the flush hook. */
    s_blend_bg = lv_camera_blend_get_bg_buffer(s_blend, NULL);
    if (s_blend_bg == NULL)
    {
        LOGE("blend_start: bg buffer unavailable\n");
        goto fail;
    }

    for (uint32_t i = 0; i < DASHCAM_BLEND_OUT_BUF_COUNT; i++)
    {
        s_blend_out_bufs[i] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, s_blend_real_size);
        s_blend_out_busy[i] = false;
        if (s_blend_out_bufs[i] == NULL)
        {
            LOGE("blend_start: out buf #%u malloc(%u) failed (UNCODED aperture full)\n",
                 (unsigned)i, (unsigned)s_blend_real_size);
            bk_mem_slab_dump_heap(MEM_SLAB_HEAP_UNCODED);
            goto fail;
        }
    }

    /* Camera handoff pool (read task -> compose worker). The read task reads the
     * ISP NV12 frame into a private scratch and CPU-converts it to RGB565 into
     * these slots, so the GPU only ever blits RGB565 (its direct NV12 path is
     * unreliable on this platform). Slots are RGB565 = w*h*2. */
    s_blend_cam_w = (uint16_t)dashcam_camera_preview_width();
    s_blend_cam_h = (uint16_t)dashcam_camera_preview_height();
    s_blend_cam_buf_h = (uint16_t)DASHCAM_PREVIEW_CAPTURE_HEIGHT;
    s_blend_cam_size = (uint32_t)s_blend_cam_w * (uint32_t)s_blend_cam_h * 2u;
    for (uint32_t i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
    {
        s_blend_cam_buf[i] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, s_blend_cam_size);
        s_blend_cam_state[i] = DASHCAM_BLEND_SLOT_FREE;
        if (s_blend_cam_buf[i] == NULL)
        {
            LOGE("blend_start: cam slot #%u malloc(%u) failed (UNCODED aperture full)\n",
                 (unsigned)i, (unsigned)s_blend_cam_size);
            bk_mem_slab_dump_heap(MEM_SLAB_HEAP_UNCODED);
            goto fail;
        }
    }

    s_blend_dirty = false;
    s_blend_compose_ok = 0;
    s_blend_compose_fail = 0;
    s_blend_drop_no_buf = 0;
    s_blend_first_frame_logged = false;
    s_blend_last_flush_us = 0;
    s_blend_bg_gen = 1;
    for (uint32_t i = 0; i < DASHCAM_BLEND_OUT_BUF_COUNT; i++)
    {
        s_blend_out_bg_gen[i] = 0;
    }

    /* Bring up the single GPU-owner compose worker before the producer. */
    s_blend_worker_stop = false;
    if (rtos_init_semaphore_ex(&s_blend_worker_sem, DASHCAM_BLEND_CAM_SLOTS + 1, 0) != BK_OK ||
        rtos_init_semaphore_ex(&s_blend_worker_exit_sem, 1, 0) != BK_OK)
    {
        LOGE("blend_start: worker semaphore init failed\n");
        goto fail;
    }
    if (rtos_core0_create_thread(&s_blend_worker_thread,
                                 4,
                                 "dashcam_blend",
                                 (beken_thread_function_t)dashcam_video_blend_worker,
                                 4096,
                                 NULL) != BK_OK)
    {
        LOGE("blend_start: compose worker create failed\n");
        s_blend_worker_thread = NULL;
        goto fail;
    }

    /* Show the status label so the LVGL UI snapshot (bg) has content. */
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

    s_blend_active = true;
    display_ui_blend_attach(s_blend_bg, s_blend_real_size, &s_blend_dirty);

    /* Deterministically seed the bg snapshot and mark it ready *before* the
     * first camera frame can arrive. Otherwise lv_camera_blend_process() returns
     * BK_ERR_NOT_INIT for every frame until an LVGL refresh happens to win the
     * scheduling race - and when LVGL never refreshes (refr=0/s) the worker
     * churns forever and the AP eventually wedges. Start from black; the first
     * real LVGL flush overwrites it via bk_widgets_flush_cb(). */
    os_memset(s_blend_bg, 0, s_blend_real_size);
    arch_dcache_flush_range(s_blend_bg, s_blend_real_size);
    lv_camera_blend_mark_bg_ready(s_blend);

    /* Force one LVGL refresh so the flush populates the bg snapshot with the
     * real UI as soon as the LVGL task runs. */
    lv_obj_invalidate(lv_screen_active());

    LOGI("blend_start: active, ui=%ux%u rgb565=%u (fb alloc=%u), %u out buf(s), cam=%ux%u(buf %u) x%u slot(s), dst=(%u,%u), bg=%p, compose on CPU0 worker\n",
         (unsigned)s_blend_w, (unsigned)s_blend_h, (unsigned)s_blend_real_size,
         (unsigned)s_blend_fb_size, (unsigned)DASHCAM_BLEND_OUT_BUF_COUNT,
         (unsigned)s_blend_cam_w, (unsigned)s_blend_cam_h,
         (unsigned)s_blend_cam_buf_h,
         (unsigned)DASHCAM_BLEND_CAM_SLOTS,
         (unsigned)s_blend_cam_dst_x, (unsigned)s_blend_cam_dst_y,
         s_blend_bg);
    return BK_OK;

fail:
    dashcam_video_blend_worker_stop();
    display_ui_blend_detach();
    s_blend_active = false;
    if (s_dashcam_video.status_label != NULL && lv_obj_is_valid(s_dashcam_video.status_label))
    {
        lv_obj_del(s_dashcam_video.status_label);
        s_dashcam_video.status_label = NULL;
    }
    for (uint32_t i = 0; i < DASHCAM_BLEND_OUT_BUF_COUNT; i++)
    {
        if (s_blend_out_bufs[i] != NULL)
        {
            bk_frame_buffer_free(s_blend_out_bufs[i]);
            s_blend_out_bufs[i] = NULL;
        }
    }
    for (uint32_t i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
    {
        if (s_blend_cam_buf[i] != NULL)
        {
            bk_frame_buffer_free(s_blend_cam_buf[i]);
            s_blend_cam_buf[i] = NULL;
        }
        s_blend_cam_state[i] = DASHCAM_BLEND_SLOT_FREE;
    }
    s_blend_bg = NULL;  /* owned by s_blend, freed in lv_camera_blend_deinit */
    if (s_blend != NULL)
    {
        lv_camera_blend_deinit(s_blend);
        s_blend = NULL;
    }
    return BK_FAIL;
}

static void dashcam_video_blend_stop(void)
{
    if (!s_blend_active && s_blend == NULL)
    {
        return;
    }

    LOGI("blend_stop: compose ok=%u fail=%u drop_no_buf=%u\n",
         (unsigned)s_blend_compose_ok, (unsigned)s_blend_compose_fail,
         (unsigned)s_blend_drop_no_buf);

    /* Restore the normal LVGL->DPU flush before tearing anything down. The read
     * task (only producer) has already been stopped by the caller, so no new
     * camera slot can become READY while we drain and stop the worker. */
    display_ui_blend_detach();
    s_blend_active = false;

    /* Stop the GPU compose worker before freeing any buffer it may touch. */
    dashcam_video_blend_worker_stop();

    if (s_dashcam_video.status_label != NULL && lv_obj_is_valid(s_dashcam_video.status_label))
    {
        lv_obj_del(s_dashcam_video.status_label);
    }
    s_dashcam_video.status_label = NULL;

    /* Detach already stopped the flush hook from writing into bg; safe to free
     * the blend ctx (which owns and frees the bg buffer). */
    s_blend_bg = NULL;
    if (s_blend != NULL)
    {
        lv_camera_blend_deinit(s_blend);
        s_blend = NULL;
    }

    for (uint32_t i = 0; i < DASHCAM_BLEND_OUT_BUF_COUNT; i++)
    {
        if (s_blend_out_bufs[i] != NULL)
        {
            bk_frame_buffer_free(s_blend_out_bufs[i]);
            s_blend_out_bufs[i] = NULL;
        }
        s_blend_out_busy[i] = false;
    }
    for (uint32_t i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
    {
        if (s_blend_cam_buf[i] != NULL)
        {
            bk_frame_buffer_free(s_blend_cam_buf[i]);
            s_blend_cam_buf[i] = NULL;
        }
        s_blend_cam_state[i] = DASHCAM_BLEND_SLOT_FREE;
    }
    s_blend_dirty = false;
    s_blend_last_flush_us = 0;
}
#endif /* DASHCAM_PREVIEW_USE_BLEND */

static const char *dashcam_video_source_name(dashcam_video_source_t source)
{
    switch (source)
    {
    case DASHCAM_VIDEO_SOURCE_FRONT_MIPI:
        return "FRONT MIPI";
    case DASHCAM_VIDEO_SOURCE_REAR_MIPI:
        return "REAR MIPI";
    default:
        return "UNKNOWN";
    }
}

static uint16_t dashcam_video_pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((uint16_t)(r & 0xF8) << 8) |
           (uint16_t)((uint16_t)(g & 0xFC) << 3) |
           (uint16_t)(b >> 3);
}

static uint8_t dashcam_video_clip_u8(int32_t value)
{
    if (value < 0)
    {
        return 0;
    }

    if (value > 255)
    {
        return 255;
    }

    return (uint8_t)value;
}

static uint16_t dashcam_video_yuv_to_rgb565(uint8_t y, uint8_t u, uint8_t v)
{
    int32_t c = (int32_t)y - 16;
    int32_t d = (int32_t)u - 128;
    int32_t e = (int32_t)v - 128;

    if (c < 0)
    {
        c = 0;
    }

    uint8_t r = dashcam_video_clip_u8((298 * c + 409 * e + 128) >> 8);
    uint8_t g = dashcam_video_clip_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    uint8_t b = dashcam_video_clip_u8((298 * c + 516 * d + 128) >> 8);

    return dashcam_video_pack_rgb565(r, g, b);
}

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

static void dashcam_video_on_nv12_frame_software(const uint8_t *frame,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t buffer_height,
                                                 uint16_t *dst)
{
    const uint8_t *y_plane = frame;
    const uint8_t *uv_plane = frame + width * buffer_height;

    for (uint32_t y = 0; y < height; y += 2u)
    {
        const uint8_t *y_row0 = y_plane + y * width;
        const uint8_t *y_row1 = y_row0 + width;
        const uint8_t *uv_row = uv_plane + (y / 2u) * width;
        uint16_t *dst_row0 = dst + y * width;
        uint16_t *dst_row1 = dst_row0 + width;

        for (uint32_t x = 0; x < width; x += 2u)
        {
            uint8_t u = uv_row[x];
            uint8_t v = uv_row[x + 1u];
            uint8_t avg_y = (uint8_t)(((uint32_t)y_row0[x] +
                                        (uint32_t)y_row0[x + 1u] +
                                        (uint32_t)y_row1[x] +
                                        (uint32_t)y_row1[x + 1u] + 2u) >> 2);
            uint16_t pixel = dashcam_video_yuv_to_rgb565(avg_y, u, v);

            dst_row0[x] = pixel;
            dst_row0[x + 1u] = pixel;
            dst_row1[x] = pixel;
            dst_row1[x + 1u] = pixel;
        }
    }
}

static void dashcam_video_on_nv12_frame(const uint8_t *frame, uint32_t width, uint32_t height)
{
    if (!s_dashcam_video.running ||
        frame == NULL ||
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

    uint64_t conv_t0 = bk_aon_rtc_get_us();
    dashcam_video_on_nv12_frame_software(frame, width, height,
                                         DASHCAM_PREVIEW_CAPTURE_HEIGHT, dst);
    s_dashcam_video.stats_conv_us += bk_aon_rtc_get_us() - conv_t0;
    s_dashcam_video.stats_conv_frames++;

    if (!s_dashcam_video.conv_path_logged)
    {
        LOGI("convert path: software (%ux%u)\n", (unsigned)width, (unsigned)height);
        s_dashcam_video.conv_path_logged = true;
    }

    dashcam_video_back_buffer_publish(dst);
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
        lv_label_set_text_fmt(s_dashcam_video.status_label,
                              "%s  LVGL PREVIEW",
                              dashcam_video_source_name(s_dashcam_video.source));
    }
}

static void dashcam_video_refresh_placeholder(void)
{
    dashcam_video_fill_placeholder();
    dashcam_video_update_status();

    if (s_dashcam_video.canvas != NULL && lv_obj_is_valid(s_dashcam_video.canvas))
    {
        lv_obj_invalidate(s_dashcam_video.canvas);
    }
}

/* disp-fps RCA probe: LVGL refresh/render/flush timing. Runs in the lv_vendor
 * LVGL task. Tagged with &s_dashcam_video as user_data for clean removal. */
static void dashcam_video_disp_event_cb(lv_event_t *e)
{
    uint64_t now = bk_aon_rtc_get_us();

    switch (lv_event_get_code(e))
    {
    case LV_EVENT_REFR_START:
        s_dashcam_video.stats_refr_start_us = now;
        s_dashcam_video.stats_render_start_us = 0;
        s_dashcam_video.stats_render_ready_us = 0;
        break;

    case LV_EVENT_RENDER_START:
        s_dashcam_video.stats_render_start_us = now;
        break;

    case LV_EVENT_RENDER_READY:
        if (s_dashcam_video.stats_render_start_us != 0)
        {
            s_dashcam_video.stats_render_sum_us +=
                now - s_dashcam_video.stats_render_start_us;
            s_dashcam_video.stats_render_count++;
        }
        s_dashcam_video.stats_render_ready_us = now;
        break;

    case LV_EVENT_REFR_READY:
        if (s_dashcam_video.stats_refr_start_us != 0)
        {
            s_dashcam_video.stats_refr_sum_us +=
                now - s_dashcam_video.stats_refr_start_us;
            s_dashcam_video.stats_refr_count++;
            if (s_dashcam_video.stats_render_ready_us != 0)
            {
                s_dashcam_video.stats_flush_sum_us +=
                    now - s_dashcam_video.stats_render_ready_us;
            }
        }
        break;

    default:
        break;
    }
}

static void dashcam_video_perf_probe_register(void)
{
    lv_display_t *disp;

    if (s_dashcam_video.perf_probe_registered || s_dashcam_video.canvas == NULL)
    {
        return;
    }

    disp = lv_obj_get_display(s_dashcam_video.canvas);
    if (disp == NULL)
    {
        return;
    }

    lv_display_add_event_cb(disp, dashcam_video_disp_event_cb,
                            LV_EVENT_REFR_START, &s_dashcam_video);
    lv_display_add_event_cb(disp, dashcam_video_disp_event_cb,
                            LV_EVENT_RENDER_START, &s_dashcam_video);
    lv_display_add_event_cb(disp, dashcam_video_disp_event_cb,
                            LV_EVENT_RENDER_READY, &s_dashcam_video);
    lv_display_add_event_cb(disp, dashcam_video_disp_event_cb,
                            LV_EVENT_REFR_READY, &s_dashcam_video);
    s_dashcam_video.perf_probe_registered = true;
}

static void dashcam_video_perf_probe_unregister(void)
{
    lv_display_t *disp;

    if (!s_dashcam_video.perf_probe_registered)
    {
        return;
    }

    disp = (s_dashcam_video.canvas != NULL) ?
           lv_obj_get_display(s_dashcam_video.canvas) : lv_display_get_default();
    if (disp != NULL)
    {
        lv_display_remove_event_cb_with_user_data(disp, dashcam_video_disp_event_cb,
                                                  &s_dashcam_video);
    }
    s_dashcam_video.perf_probe_registered = false;
}

static void dashcam_video_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    void *ready_buf = NULL;
    uint64_t cb_now = bk_aon_rtc_get_us();

    if (s_dashcam_video.stats_cb_last_us != 0)
    {
        uint32_t gap = (uint32_t)(cb_now - s_dashcam_video.stats_cb_last_us);
        s_dashcam_video.stats_cb_gap_sum_us += gap;
        if (gap > s_dashcam_video.stats_cb_gap_max_us)
        {
            s_dashcam_video.stats_cb_gap_max_us = gap;
        }
        s_dashcam_video.stats_cb_count++;
    }
    s_dashcam_video.stats_cb_last_us = cb_now;

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
         * memcpy'ing ~1 MB PSRAM->PSRAM every frame. The buffer that was on the
         * canvas was already composited by LVGL in a previous lv_timer cycle, so
         * the canvas no longer references it once set_buffer points elsewhere -
         * it is safe to hand straight back to the producer.
         */
        uint64_t copy_t0 = bk_aon_rtc_get_us();
        void *old_canvas = s_dashcam_video.canvas_buf;

        s_dashcam_video.canvas_buf = ready_buf;
        lv_canvas_set_buffer(s_dashcam_video.canvas, ready_buf,
                             s_dashcam_video.width, s_dashcam_video.height,
                             LV_COLOR_FORMAT_RGB565);
        dashcam_video_buf_release(old_canvas);

        s_dashcam_video.stats_copy_us += bk_aon_rtc_get_us() - copy_t0;
        s_dashcam_video.stats_disp_frames++;
    }

    lv_obj_invalidate(s_dashcam_video.canvas);
}

static void dashcam_video_stats_maybe_log(uint32_t now_ms)
{
    uint32_t elapsed_ms = now_ms - s_dashcam_video.stats_start_ms;

    if (elapsed_ms < DASHCAM_VIDEO_STATS_INTERVAL_MS)
    {
        return;
    }

    uint32_t read_frames = s_dashcam_video.stats_read_frames;
    uint32_t conv_frames = s_dashcam_video.stats_conv_frames;
    uint32_t disp_frames = s_dashcam_video.stats_disp_frames;

    uint32_t read_fps_x10 = (read_frames * 10000u) / elapsed_ms;
    uint32_t disp_fps_x10 = (disp_frames * 10000u) / elapsed_ms;
    uint32_t read_avg_us = read_frames ? (uint32_t)(s_dashcam_video.stats_read_us / read_frames) : 0u;
    uint32_t conv_avg_us = conv_frames ? (uint32_t)(s_dashcam_video.stats_conv_us / conv_frames) : 0u;
    uint32_t comp_avg_us = disp_frames ? (uint32_t)(s_dashcam_video.stats_copy_us / disp_frames) : 0u;

#if DASHCAM_PREVIEW_USE_BLEND
    uint32_t drop = s_blend_drop_no_buf;
    s_blend_drop_no_buf = 0;
#else
    uint32_t drop = 0;
#endif

    /*
     * Per-frame pipeline cost; the largest avg-us value is the bottleneck:
     *   read    = ISP channel read (camera -> CPU NV12)          [read task]
     *   conv    = NV12 -> RGB565 software convert                [read task]
     *   compose = CPU page-composite (UI bg seed when dirty +
     *             rotated camera overlay) + DPU flush            [compose worker]
     * read fps = capture rate, disp fps = on-screen rate.
     * drop = frames skipped this interval because both DPU out buffers were
     *        still being scanned out (DPU back-pressure).
     */
    LOGI("preview stats %ums: read=%u.%ufps(%uus) conv=%uf(%uus) compose=%uus disp=%u.%ufps drop=%u read_fail=%u\n",
         (unsigned)elapsed_ms,
         (unsigned)(read_fps_x10 / 10u), (unsigned)(read_fps_x10 % 10u),
         (unsigned)read_avg_us,
         (unsigned)conv_frames, (unsigned)conv_avg_us,
         (unsigned)comp_avg_us,
         (unsigned)(disp_fps_x10 / 10u), (unsigned)(disp_fps_x10 % 10u),
         (unsigned)drop,
         (unsigned)s_dashcam_video.stats_read_failures);

    s_dashcam_video.stats_start_ms = now_ms;
    s_dashcam_video.stats_read_frames = 0;
    s_dashcam_video.stats_read_failures = 0;
    s_dashcam_video.stats_read_us = 0;
    s_dashcam_video.stats_conv_us = 0;
    s_dashcam_video.stats_conv_frames = 0;
    s_dashcam_video.stats_copy_us = 0;
    s_dashcam_video.stats_disp_frames = 0;

    s_dashcam_video.stats_refr_sum_us = 0;
    s_dashcam_video.stats_refr_count = 0;
    s_dashcam_video.stats_render_sum_us = 0;
    s_dashcam_video.stats_render_count = 0;
    s_dashcam_video.stats_flush_sum_us = 0;
    s_dashcam_video.stats_gpu_wait_sum_us = 0;
    s_dashcam_video.stats_gpu_wait_max_us = 0;
    s_dashcam_video.stats_gpu_lock_count = 0;
    s_dashcam_video.stats_cb_gap_sum_us = 0;
    s_dashcam_video.stats_cb_gap_max_us = 0;
    s_dashcam_video.stats_cb_count = 0;
}

static void dashcam_video_read_task(void *arg)
{
    (void)arg;

    const uint32_t width = dashcam_camera_preview_width();
    const uint32_t height = dashcam_camera_preview_height();
    const uint32_t buffer_height = DASHCAM_PREVIEW_CAPTURE_HEIGHT;
    const uint32_t frame_size = width * buffer_height * DASHCAM_NV12_BYTES_PER_2_PIXELS / 2u;
    uint8_t *frame = NULL;
    uint32_t frame_count = 0;
    int ret;

#if DASHCAM_PREVIEW_USE_BLEND
    const bool blend = s_blend_active;
#else
    const bool blend = false;
#endif

    /* Both paths read the ISP NV12 frame into this private scratch. The legacy
     * canvas path converts it into the LVGL canvas; the blend path converts it
     * into the RGB565 compose-worker handoff slot. Allocated from the
     * MEM_SLAB_HEAP_UNCODED slab (enlarged in ram_regions.csv to fit it). */
    frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, frame_size);
    if (frame == NULL)
    {
        LOGE("preview NV12 scratch malloc(%u) from UNCODED failed\n", (unsigned)frame_size);
        goto exit;
    }

    s_dashcam_video.stats_start_ms = rtos_get_time();
    s_dashcam_video.stats_read_frames = 0;
    s_dashcam_video.stats_read_failures = 0;
    s_dashcam_video.stats_read_us = 0;
    s_dashcam_video.stats_conv_us = 0;
    s_dashcam_video.stats_conv_frames = 0;
    s_dashcam_video.stats_copy_us = 0;
    s_dashcam_video.stats_disp_frames = 0;

    while (!s_dashcam_video.read_stop_requested)
    {
        uint8_t *target = frame;
        int slot = -1;

#if DASHCAM_PREVIEW_USE_BLEND
        if (blend)
        {
            for (int i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
            {
                if (s_blend_cam_state[i] == DASHCAM_BLEND_SLOT_FREE)
                {
                    slot = i;
                    break;
                }
            }
            if (slot < 0)
            {
                /* All slots queued/being composed: back off rather than stomp a
                 * frame the worker is still reading. */
                rtos_delay_milliseconds(2);
                continue;
            }
            /* Read into the NV12 scratch; conversion targets the RGB565 slot. */
        }
#endif

        uint64_t read_t0 = bk_aon_rtc_get_us();
        // LOGD("read#%u: read_start (slot=%d)\n", (unsigned)(frame_count + 1), slot);
        ret = dashcam_camera_read_preview(target, frame_size, DASHCAM_CAMERA_READ_TIMEOUT_MS);
        // LOGD("read#%u: read_done ret=%d\n", (unsigned)(frame_count + 1), ret);
        if (ret == BK_OK)
        {
            s_dashcam_video.stats_read_us += bk_aon_rtc_get_us() - read_t0;
            s_dashcam_video.stats_read_frames++;

            if (s_dashcam_video.running &&
                s_dashcam_video.source == DASHCAM_VIDEO_SOURCE_FRONT_MIPI)
            {
#if DASHCAM_PREVIEW_USE_BLEND
                if (blend && slot >= 0)
                {
                    /* CPU-convert NV12 -> RGB565 into the handoff slot, then
                     * clean (write-back) its cache lines so the GPU sees the
                     * frame, and hand the slot to the compose worker. The GPU
                     * only ever blits RGB565 (its direct NV12 blit is unreliable
                     * on this platform). This runs on the (unpinned) read task,
                     * never on the CPU0 GPU worker. */
                    uint64_t conv_t0 = bk_aon_rtc_get_us();
                    dashcam_video_on_nv12_frame_software(
                        target, width, height, buffer_height,
                        (uint16_t *)s_blend_cam_buf[slot]);
                    s_dashcam_video.stats_conv_us += bk_aon_rtc_get_us() - conv_t0;
                    s_dashcam_video.stats_conv_frames++;

                    arch_dcache_flush_range(s_blend_cam_buf[slot], s_blend_cam_size);
                    s_blend_cam_state[slot] = DASHCAM_BLEND_SLOT_READY;
                    LOGD("read#%u: handoff slot=%d\n",
                         (unsigned)frame_count, slot);
                    if (s_blend_worker_sem != NULL)
                    {
                        rtos_set_semaphore(&s_blend_worker_sem);
                    }
                }
                else
#endif
                {
                    dashcam_video_on_nv12_frame(target, width, height);
                }
            }

            frame_count++;
            if (frame_count == 1 || (frame_count % 30u) == 0u)
            {
                LOGI("preview frame %u size=%u\n", (unsigned)frame_count, (unsigned)frame_size);
            }
        }
        else
        {
            s_dashcam_video.stats_read_failures++;
            if (!s_dashcam_video.read_stop_requested)
            {
                LOGW("preview read timeout/fail: %d\n", ret);
            }
        }

        dashcam_video_stats_maybe_log(rtos_get_time());
    }

exit:
    if (frame != NULL)
    {
        bk_frame_buffer_free(frame);
    }

    LOGI("preview read task exit, frames=%u\n", (unsigned)frame_count);
    if (s_dashcam_video.read_exit_sem != NULL)
    {
        rtos_set_semaphore(&s_dashcam_video.read_exit_sem);
    }
    s_dashcam_video.read_thread = NULL;
    rtos_delete_thread(NULL);
}

static void dashcam_video_start_read_task(void)
{
    if (s_dashcam_video.read_thread != NULL)
    {
        return;
    }

    s_dashcam_video.read_stop_requested = false;
    s_dashcam_video.conv_path_logged = false;

    if (s_dashcam_video.read_exit_sem != NULL)
    {
        rtos_deinit_semaphore(&s_dashcam_video.read_exit_sem);
        s_dashcam_video.read_exit_sem = NULL;
    }

    if (rtos_init_semaphore_ex(&s_dashcam_video.read_exit_sem, 1, 0) != BK_OK)
    {
        LOGE("preview read semaphore init failed\n");
        return;
    }

    /* The read task no longer touches the GPU (the dedicated CPU0 compose worker
     * is the sole vg_lite owner), so it does not need CPU0 pinning. Keeping it
     * unpinned also moves its NV12 cache-clean off CPU0, away from the GPU IRQ
     * core - the flush_and_invd on CPU0 was the heartbeat-wedge trigger. */
    if (rtos_create_thread(&s_dashcam_video.read_thread,
                           4,
                           "dashcam_prev",
                           (beken_thread_function_t)dashcam_video_read_task,
                           8192,
                           NULL) != BK_OK)
    {
        LOGE("preview read task create failed\n");
        rtos_deinit_semaphore(&s_dashcam_video.read_exit_sem);
        s_dashcam_video.read_exit_sem = NULL;
        s_dashcam_video.read_thread = NULL;
    }
}

static void dashcam_video_stop_read_task(void)
{
    s_dashcam_video.read_stop_requested = true;

    if (s_dashcam_video.read_thread != NULL && s_dashcam_video.read_exit_sem != NULL)
    {
        bk_err_t ret = rtos_get_semaphore(&s_dashcam_video.read_exit_sem,
                                          DASHCAM_CAMERA_STOP_WAIT_MS);
        if (ret != BK_OK)
        {
            LOGW("wait preview read task exit timeout: %d\n", ret);
        }
        rtos_deinit_semaphore(&s_dashcam_video.read_exit_sem);
        s_dashcam_video.read_exit_sem = NULL;
        s_dashcam_video.read_thread = NULL;
    }
}

static bk_err_t dashcam_video_start_internal(lv_obj_t *parent, bool start_read,
                                             uint32_t width, uint32_t height)
{
    LOGD("start_internal parent=%p read=%d %ux%u\n",
         (void *)parent, (int)start_read, (unsigned)width, (unsigned)height);

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

#if DASHCAM_PREVIEW_USE_BLEND
    /* Live preview (start_read) goes through the page-scoped GPU compositor: no
     * LVGL canvas, the read task composites NV12 over the UI snapshot and pushes
     * to the DPU. Playback (start_read == false) keeps the canvas path below. */
    if (start_read)
    {
        if (dashcam_video_blend_start(parent) == BK_OK)
        {
            s_dashcam_video.parent = parent;
            s_dashcam_video.width = width;
            s_dashcam_video.height = height;
            s_dashcam_video.running = true;
            dashcam_video_start_read_task();
            LOGI("start %s preview (BLEND) %ux%u\n",
                 dashcam_video_source_name(s_dashcam_video.source),
                 (unsigned)width, (unsigned)height);
            return BK_OK;
        }

        LOGE("blend_start failed, live preview unavailable\n");
        return BK_FAIL;
    }
#endif

    const size_t buf_size = (size_t)width * (size_t)height * DASHCAM_RGB565_BYTES;
    void *bufs[DASHCAM_VIDEO_BUF_COUNT] = {0};
    uint32_t i;

    /* Keep canvas buffers in the display/camera slab so playback frames and
     * fallback preview frames use the same DPU-reachable aperture. */
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

    lv_canvas_set_buffer(canvas, bufs[0], width, height, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, width, height);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    s_dashcam_video.refresh_timer = lv_timer_create(dashcam_video_refresh_timer_cb,
                                                    DASHCAM_VIDEO_REFRESH_PERIOD_MS,
                                                    NULL);

    s_dashcam_video.stats_cb_last_us = 0;
    dashcam_video_perf_probe_register();

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

    dashcam_video_refresh_placeholder();
    if (start_read)
    {
        dashcam_video_start_read_task();
    }
    LOGI("start %s %s %ux%u\n",
         dashcam_video_source_name(s_dashcam_video.source),
         start_read ? "preview" : "sink",
         (unsigned)width,
         (unsigned)height);
    return BK_OK;
}

bk_err_t dashcam_video_start(lv_obj_t *parent)
{
    return dashcam_video_start_internal(parent, true,
                                        dashcam_camera_preview_width(),
                                        dashcam_camera_preview_height());
}

bk_err_t dashcam_video_start_sink(lv_obj_t *parent)
{
    /* Playback decodes recorded clips, which are encoded at the record
     * resolution; size the canvas to match so decoded frames are not dropped. */
    return dashcam_video_start_internal(parent, false,
                                        DASHCAM_RECORD_WIDTH,
                                        DASHCAM_RECORD_HEIGHT);
}

void dashcam_video_stop(void)
{
    LOGD("stop (running=%d)\n", (int)s_dashcam_video.running);
    s_dashcam_video.running = false;
    dashcam_video_stop_read_task();

#if DASHCAM_PREVIEW_USE_BLEND
    /* Read task (sole GPU user in blend mode) has exited; tear the compositor
     * down and restore the normal LVGL->DPU flush. No-op if blend was inactive
     * (e.g. playback sink), so the canvas path below still runs. */
    if (s_blend_active || s_blend != NULL)
    {
        dashcam_video_blend_stop();
        s_dashcam_video.parent = NULL;
        s_dashcam_video.width = 0;
        s_dashcam_video.height = 0;
        return;
    }
#endif

    if (s_dashcam_video.refresh_timer != NULL)
    {
        lv_timer_delete(s_dashcam_video.refresh_timer);
        s_dashcam_video.refresh_timer = NULL;
    }

    dashcam_video_perf_probe_unregister();

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
    s_dashcam_video.refresh_timer = NULL;
    s_dashcam_video.canvas_buf = NULL;
    s_dashcam_video.ready_buf = NULL;
    s_dashcam_video.width = 0;
    s_dashcam_video.height = 0;
}

bk_err_t dashcam_video_switch_next(void)
{
    s_dashcam_video.source = (s_dashcam_video.source == DASHCAM_VIDEO_SOURCE_FRONT_MIPI) ?
                             DASHCAM_VIDEO_SOURCE_REAR_MIPI :
                             DASHCAM_VIDEO_SOURCE_FRONT_MIPI;
    LOGI("switch to %s\n", dashcam_video_source_name(s_dashcam_video.source));
    return BK_OK;
}

dashcam_video_source_t dashcam_video_get_source(void)
{
    return s_dashcam_video.source;
}

void dashcam_video_on_frame(const void *frame, uint32_t width, uint32_t height, uint32_t format)
{
    if (!s_dashcam_video.running ||
        frame == NULL ||
        (format != DASHCAM_VIDEO_FRAME_FORMAT_RGB565 &&
         format != DASHCAM_VIDEO_FRAME_FORMAT_NV12) ||
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

    if (format == DASHCAM_VIDEO_FRAME_FORMAT_NV12)
    {
        dashcam_video_on_nv12_frame_software((const uint8_t *)frame, width, height, height, dst);
    }
    else
    {
        memcpy(dst, frame, (size_t)width * (size_t)height * DASHCAM_RGB565_BYTES);
    }
    dashcam_video_back_buffer_publish(dst);
}
