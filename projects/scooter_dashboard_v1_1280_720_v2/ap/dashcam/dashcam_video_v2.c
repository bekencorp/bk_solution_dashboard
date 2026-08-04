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
 * Page-scoped GPU compositing for the live preview (req5_design.md §13) - v2.
 *
 * Live preview is GPU-composited: the read task pull-reads SP/MP NV12 frames and
 * CPU-converts them to RGB565, then a dedicated compose worker hands the RGB565
 * camera frame to lv_camera_blend_process(), which uses VG-Lite to blit the LVGL
 * UI snapshot (bg) + the camera frame into the output framebuffer and pushes it
 * directly to DPU. (The sibling dashcam_video.c keeps the CPU page-composite
 * path; only one of the two is compiled - see ap/CMakeLists.txt.)
 *
 * The earlier stripes came from VG-Lite's vg_lite_blit_rect shearing a linear
 * RGB565 sub-rectangle; lv_camera_blend_process() now issues a full vg_lite_blit
 * for the camera (src_x==src_y==0), which the GPU composites cleanly.
 *
 * Only the live read-task preview is GPU-composited. Playback (start_sink, i.e.
 * start_read == false) still draws decoded frames into an LVGL canvas; that
 * canvas path lives further down and is independent of the blend compositor.
 */

/* Camera-layer rotation applied by the GPU (lv_camera_blend_set_camera_matrix).
 * lv_camera_blend_process composites into the panel-native bg that LVGL already
 * flushed pre-rotated by CONFIG_SCOOTER_UI_ROTATION (270 here). Empirically the
 * GPU's rotate is opposite-handed to the panel, so matching the UI's 270 yielded
 * a 180-flipped image; 90 lands in the SAME bounding box as 270 (both map into
 * [dst_x, dst_x+src_height] x [dst_y, dst_y+src_width]) but with the content
 * flipped 180 - i.e. upright. Override at build time to retune on-device. */
#ifndef DASHCAM_BLEND_CAM_ROTATE
#define DASHCAM_BLEND_CAM_ROTATE 90
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

/* Signed fine-tune of the computed camera box top-left in the physical (panel)
 * frame. OFF_X moves it right(+)/left(-), OFF_Y down(+)/up(-). Default 0 - the
 * placement is computed from the preview box coords below; use these only for a
 * residual sensor/box-edge nudge. */
#ifndef DASHCAM_BLEND_CAM_DST_OFF_X
#define DASHCAM_BLEND_CAM_DST_OFF_X 0
#endif
#ifndef DASHCAM_BLEND_CAM_DST_OFF_Y
#define DASHCAM_BLEND_CAM_DST_OFF_Y 0
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
    /* Preview pipeline counters (reset each stats interval). The only true frame
     * rate is stats_disp_frames/elapsed (on-screen). The read task's per-frame
     * wall time decomposes as slot_wait + read + conv; compose runs on the worker
     * and overlaps with the next read/convert when a handoff slot is free. */
    uint32_t stats_read_frames;   /* successful camera reads = produced frames */
    uint32_t stats_read_failures;
    uint64_t stats_read_us;       /* accumulated ISP channel read time */
    uint64_t stats_slot_wait_us;  /* sum of read-task wait for a free compose slot */
    uint64_t stats_conv_us;       /* accumulated CPU NV12->RGB565 convert time */
    uint32_t stats_conv_frames;   /* frames converted into RGB565 handoff slots */
    uint64_t stats_copy_us;       /* blend: sum of GPU compose+flush; legacy: canvas swap */
    uint64_t stats_blend_process_us; /* accumulated lv_camera_blend_process() time */
    uint32_t stats_blend_process_count;
    uint32_t stats_disp_frames;   /* frames shown on screen */

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

/* Maximum consecutive GPU-hang recoveries before we give up and stop the
 * preview. Reset to 0 after any successful composite, so this bounds a burst of
 * back-to-back hangs, not the lifetime count. */
#define DASHCAM_BLEND_GPU_RECOVER_MAX 8u
static uint32_t s_blend_gpu_recover_count = 0;

/* Re-initialize the vg_lite command engine after a hang (vg_lite_finish
 * timeout). On this nano GPU a finish timeout leaves the engine wedged
 * (idle=0x7ffffffe); the driver's own reset path is compiled out, so the only
 * way to make the GPU usable again - for both this blend path and LVGL's own
 * rendering - is a full close + init. The blend handle keeps no persistent
 * vg_lite allocations across process() calls (buffers are wrapped/freed each
 * call), so a close+init is safe here.
 *
 * MUST be called with the vendor GPU lock held so LVGL cannot be mid-submit. */
static bool dashcam_video_blend_gpu_recover(void)
{
    vg_lite_close();
    vg_lite_error_t ret = vg_lite_init(0, 0);
    if (ret != VG_LITE_SUCCESS)
    {
        LOGE("blend GPU recover: vg_lite_init failed: %d\n", (int)ret);
        s_blend_gpu_inited = false;
        return false;
    }
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

#ifndef DASHCAM_VERBOSE_READ_LOG
#define DASHCAM_VERBOSE_READ_LOG 0
#endif

/* Two RGB565 handoff slots double-buffer the read->compose pipeline: the read
 * task reads the next ISP NV12 frame into a private scratch, CPU-converts it to
 * RGB565 into one slot, while the compose worker GPU-blits the other slot. So the
 * ISP read AND the CPU NV12->RGB565 convert both overlap the GPU compose instead
 * of serialising in front of it - the convert no longer sits on the worker's
 * critical path. The GPU only ever blits a single-plane RGB565 source (its
 * rotated planar-NV12 blit is unreliable on this platform: stripes / GPU wedge). */
#define DASHCAM_BLEND_CAM_SLOTS 2

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

/* --- camera frame handoff: read task (any core) -> GPU compose worker --------
 * The single GPU is SMP-unsafe, so only the compose worker touches vg_lite and
 * is the sole GPU owner; concurrency against LVGL's GPU use is handled by the
 * lv_vendor GPU lock, not by core pinning (see worker create). The read task
 * reads the ISP NV12 frame into a private scratch, CPU-converts it to RGB565 into
 * a free slot, flushes the slot, and hands it to the worker. The worker runs no
 * cache maintenance on the camera buffer and only ever GPU-blits RGB565.
 *
 * The read-side NV12 scratch is a single shared buffer: only the read task uses
 * it, one frame at a time (read -> convert -> reuse), so it needs no per-slot
 * copy. Double-buffering lives on the RGB565 slots so read+convert overlaps the
 * GPU compose. */
static void *s_blend_cam_buf[DASHCAM_BLEND_CAM_SLOTS] = {NULL};   /* RGB565 slots */
static volatile uint8_t s_blend_cam_state[DASHCAM_BLEND_CAM_SLOTS] = {0};
static uint32_t s_blend_cam_size = 0;           /* RGB565 slot size = cam_w*cam_h*2 */
static uint16_t s_blend_cam_w = 0;
static uint16_t s_blend_cam_h = 0;
static uint16_t s_blend_cam_buf_h = 0;
static void *s_blend_cam_nv12 = NULL;           /* read-task NV12 scratch (shared) */
static uint32_t s_blend_cam_nv12_size = 0;      /* NV12 size = cam_w*buf_h*3/2 */
static beken_thread_t s_blend_worker_thread = NULL;
static beken_semaphore_t s_blend_worker_sem = NULL;       /* read task -> worker wakeups */
static beken_semaphore_t s_blend_worker_exit_sem = NULL;  /* worker -> stop() join */
static volatile bool s_blend_worker_stop = false;
static volatile bool s_blend_fatal_error = false;

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

static bool s_blend_yuv_lut_ready = false;
static int32_t s_blend_y_part[256];
static int32_t s_blend_u_b[256];
static int32_t s_blend_u_g[256];
static int32_t s_blend_v_g[256];
static int32_t s_blend_v_r[256];

static inline uint8_t dashcam_video_blend_clip_u8(int32_t value)
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

static void dashcam_video_blend_yuv_lut_init(void)
{
    if (s_blend_yuv_lut_ready)
    {
        return;
    }

    for (uint32_t i = 0; i < 256u; i++)
    {
        int32_t c = (int32_t)i - 16;
        int32_t d = (int32_t)i - 128;

        if (c < 0)
        {
            c = 0;
        }

        s_blend_y_part[i] = 298 * c + 128;
        s_blend_u_b[i] = 516 * d;
        s_blend_u_g[i] = -100 * d;
        s_blend_v_g[i] = -208 * d;
        s_blend_v_r[i] = 409 * d;
    }

    s_blend_yuv_lut_ready = true;
}

static inline uint16_t dashcam_video_blend_yuv_to_rgb565_fast(uint8_t y, uint8_t u, uint8_t v)
{
    int32_t y_part = s_blend_y_part[y];
    uint8_t r = dashcam_video_blend_clip_u8((y_part + s_blend_v_r[v]) >> 8);
    uint8_t g = dashcam_video_blend_clip_u8((y_part + s_blend_u_g[u] + s_blend_v_g[v]) >> 8);
    uint8_t b = dashcam_video_blend_clip_u8((y_part + s_blend_u_b[u]) >> 8);

    return (uint16_t)(((uint32_t)(r & 0xF8) << 8) |
                      ((uint32_t)(g & 0xFC) << 3) |
                      ((uint32_t)b >> 3));
}

/*
 * CPU-convert one NV12 frame (BT.601 limited range) to a linear RGB565 slot.
 * Preview favours cadence over per-pixel sharpness: NV12 already shares chroma
 * per 2x2 luma block, so we average that 2x2 luma group, do one table-assisted
 * YUV->RGB565 conversion, then fill the four RGB565 pixels. This cuts the hot
 * path conversion work to roughly 1/4 while preserving source dimensions for the
 * downstream GPU rotation/placement math. The UV plane sits at y_plane +
 * cam_w*buf_h (16-aligned buffer height), matching how the ISP lays out the
 * frame. Called on the read task (the sole producer), so the destination slot it
 * is filling is not yet visible to the worker - no locking needed.
 */
static bool dashcam_video_blend_cpu_convert_nv12_to_rgb(const uint8_t *nv12,
                                                        uint16_t *dst,
                                                        uint32_t width,
                                                        uint32_t height)
{
    const uint8_t *y_plane = nv12;
    const uint8_t *uv_plane = nv12 + (uint32_t)width * (uint32_t)s_blend_cam_buf_h;

    if (s_dashcam_video.read_stop_requested || !s_dashcam_video.running)
    {
        return false;
    }

    dashcam_video_blend_yuv_lut_init();

    for (uint32_t y = 0; y < height; y += 2u)
    {
        if ((y & 0x3Fu) == 0u)
        {
            if (s_dashcam_video.read_stop_requested || !s_dashcam_video.running)
            {
                return false;
            }

            /* The read task and blend worker are both long-running CPU/GPU jobs.
             * Yield occasionally so page-exit key handling and LVGL teardown are
             * not starved while a preview frame is being converted. */
            (void)rtos_delay_milliseconds(1);
        }

        const uint8_t *y_row = y_plane + (uint32_t)y * width;
        const uint8_t *y_row_next = y_row + width;
        const uint8_t *uv_row = uv_plane + (uint32_t)(y >> 1) * width;
        uint16_t *dst_row = dst + (uint32_t)y * width;
        uint16_t *dst_row_next = dst_row + width;

        for (uint32_t x = 0; x < width; x += 2u)
        {
            uint8_t u = uv_row[x];
            uint8_t v = uv_row[x + 1u];
            uint8_t avg_y = (uint8_t)(((uint32_t)y_row[x] +
                                        (uint32_t)y_row[x + 1u] +
                                        (uint32_t)y_row_next[x] +
                                        (uint32_t)y_row_next[x + 1u] + 2u) >> 2);
            uint16_t pixel = dashcam_video_blend_yuv_to_rgb565_fast(avg_y, u, v);

            dst_row[x] = pixel;
            dst_row[x + 1u] = pixel;
            dst_row_next[x] = pixel;
            dst_row_next[x + 1u] = pixel;
        }
    }

    return true;
}

/*
 * Compose one camera frame over the latest LVGL UI snapshot on the GPU
 * (lv_camera_blend_process) and push the result to the DPU. Runs ONLY on the
 * dedicated compose worker, which is the single GPU owner. The shared GPU lock
 * serialises against LVGL's own GPU use (req5_design.md §13.13).
 *
 * `rgb565` is a handoff slot the read task already CPU-converted from NV12 and
 * flushed, so the worker only GPU-blits a single-plane RGB565 source (no CPU
 * convert nor camera-buffer cache op on the worker's critical path).
 */
static bool dashcam_video_blend_compose_and_flush(const uint8_t *rgb565,
                                                  uint32_t width,
                                                  uint32_t height)
{
    if (s_blend == NULL || rgb565 == NULL)
    {
        return false;
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

    /* The output buffer is produced by the GPU and consumed by the DPU. Drop any
     * stale CPU cache ownership before the GPU writes it; a CPU flush here would
     * risk writing old lines over the freshly rendered GPU frame. */
    arch_dcache_invd_range(out, s_blend_real_size);

    /* The LVGL flush hook CPU-memcpys each new UI frame into the blend bg buffer,
     * cleans the cache lines (under the GPU lock so it never overlaps this
     * worker's GPU read of bg), and sets s_blend_dirty. When the UI actually
     * changed, only mark the bg ready here so lv_camera_blend_process() blits the
     * up-to-date UI snapshot without a duplicate cache clean on this worker. */
    bool bg_refreshed = s_blend_dirty;
    s_blend_dirty = false;
    if (bg_refreshed)
    {
        lv_camera_blend_mark_bg_ready(s_blend);
    }

    /* Serialise against LVGL's own GPU use: the single nano GPU is SMP-unsafe, so
     * this lock (the same one LVGL takes) guarantees one submit+finish at a time
     * regardless of which core this worker runs on. */
    uint64_t gpu_wait_t0 = bk_aon_rtc_get_us();
    bool gpu_locked = lv_vendor_gpu_lock();
    uint32_t gpu_wait_us = (uint32_t)(bk_aon_rtc_get_us() - gpu_wait_t0);
    s_dashcam_video.stats_gpu_wait_sum_us += gpu_wait_us;
    if (gpu_wait_us > s_dashcam_video.stats_gpu_wait_max_us)
    {
        s_dashcam_video.stats_gpu_wait_max_us = gpu_wait_us;
    }
    s_dashcam_video.stats_gpu_lock_count++;

    /* GPU page-composite (lv_camera_blend_process): blit the UI bg full-frame
     * into `out`, then blit the camera over it placed/rotated by the matrix
     * (lv_camera_blend_set_camera_matrix uses rotate_degree + dst_x/dst_y).
     *
     * Because src_x and src_y are 0 the camera covers its whole source buffer, so
     * a full vg_lite_blit (not vg_lite_blit_rect) is used - the blit that does NOT
     * stripe on a linear target (the blit_rect-on-linear-sub-rect shear was the
     * earlier stripe root cause, now avoided). The camera layer is the read task's
     * CPU-converted single-plane RGB565 slot. */
    lv_camera_blend_camera_frame_t cam = {0};
    cam.buffer = (void *)rgb565;
    cam.src_format = BK_PIXEL_FORMAT_RGB565;
    cam.src_x = 0;
    cam.src_y = 0;
    cam.src_width = (uint16_t)width;
    cam.src_height = (uint16_t)height;
    cam.src_compress = false;
    cam.dst_x = s_blend_cam_dst_x;
    cam.dst_y = s_blend_cam_dst_y;
    cam.rotate_degree = DASHCAM_BLEND_CAM_ROTATE;
    cam.alpha_blend = false;

    uint64_t blend_t0 = bk_aon_rtc_get_us();
    bk_err_t ret = lv_camera_blend_process(s_blend, out, &cam);
    s_dashcam_video.stats_blend_process_us += bk_aon_rtc_get_us() - blend_t0;
    s_dashcam_video.stats_blend_process_count++;
    if (ret == BK_OK)
    {
        /* The GPU wrote `out` directly to memory; drop any stale CPU copy so the
         * DPU push below (and any later CPU read) sees the GPU result. */
        arch_dcache_invd_range(out, s_blend_real_size);
    }
    lv_vendor_gpu_unlock(gpu_locked);

    if (ret != BK_OK)
    {
        s_blend_compose_fail++;

        /* BK_FAIL specifically means a vg_lite_finish() timed out - the GPU
         * command engine is wedged (idle=0x7ffffffe). If left wedged, the next
         * GPU user (LVGL rendering) blocks on the dead engine and the task
         * watchdog resets the whole board. Reset the engine and DROP this frame
         * so the preview keeps running. Other returns (e.g. BK_ERR_NOT_INIT
         * before the first LVGL bg snapshot) are not hangs - just skip the frame,
         * no reset and no fatal stop. */
        if (ret == BK_FAIL)
        {
            s_blend_gpu_recover_count++;
            if (s_blend_gpu_recover_count <= DASHCAM_BLEND_GPU_RECOVER_MAX)
            {
                bool relock = lv_vendor_gpu_lock();
                bool recovered = dashcam_video_blend_gpu_recover();
                lv_vendor_gpu_unlock(relock);
                LOGW("blend compose fail #%u ret=%d (bg refreshed=%d): GPU hang, reset #%u %s\n",
                     (unsigned)s_blend_compose_fail, (int)ret, (int)bg_refreshed,
                     (unsigned)s_blend_gpu_recover_count,
                     recovered ? "ok, frame dropped" : "FAILED");
                if (recovered)
                {
                    return false;
                }
            }
            else
            {
                LOGE("blend compose fail #%u: GPU hang recover limit (%u) reached, stopping preview\n",
                     (unsigned)s_blend_compose_fail, (unsigned)DASHCAM_BLEND_GPU_RECOVER_MAX);
            }

            /* Recovery failed or the consecutive-hang budget is exhausted: fall
             * back to the safe stop. Also request a camera close from the read
             * task - keeping ISP/AE running after the GPU path died has
             * reproduced a later sensor callback MemFault. */
            s_blend_fatal_error = true;
            s_blend_worker_stop = true;
            s_dashcam_video.read_stop_requested = true;
            return false;
        }

        if (s_blend_compose_fail == 1 || (s_blend_compose_fail % 30u) == 0u)
        {
            LOGW("blend compose fail #%u ret=%d (bg refreshed=%d)\n",
                 (unsigned)s_blend_compose_fail, (int)ret, (int)bg_refreshed);
        }
        return false;
    }

    s_blend_out_busy[idx] = true;
    if (display_ui_blend_flush(out, dashcam_video_blend_out_free_cb) != BK_OK)
    {
        s_blend_out_busy[idx] = false;
        LOGW("blend flush to DPU failed (out=%p)\n", out);
        return false;
    }
    s_blend_last_flush_us = bk_aon_rtc_get_us();

    s_blend_compose_ok++;
    s_blend_gpu_recover_count = 0;   /* a clean composite ends the consecutive-hang burst */
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
 * the GPU has exactly one owner (besides LVGL's own serialised use). Pin it to
 * AP core0 (CPU2), the same core on which the GPU IRQ is enabled by the driver.
 * VG-Lite keeps process-global command/context state; letting the submit/finish
 * path migrate across AP cores repeatedly reproduced GPU hang (finish err=4).
 * The read task stays unpinned because it no longer touches VG-Lite.
 */
static void dashcam_video_blend_worker(void *arg)
{
    (void)arg;
    LOGI("blend worker: start (single GPU owner, core0)\n");

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
                /* GPU page-composite time (lv_camera_blend_process: bg blit +
                 * RGB565 camera blit, incl. GPU lock wait + DPU flush). Keyed by
                 * stats_disp_frames, bumped inside compose on success. */
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

    /* Map the preview box's LVGL *logical* top-left (lx0,ly0) into the physical
     * (panel) framebuffer the GPU composites into. The UI is ROTATE_270, so the
     * logical 1280x720 space relates to the physical 720x1280 buffer (width
     * s_blend_w) by:  px = (s_blend_w-1) - ly0 ;  py = lx0.
     * The camera is composited with rotate_degree=90, whose matrix anchors the
     * cam_w x cam_h frame in [dst_x, dst_x+cam_h) x [dst_y, dst_y+cam_w).
     * Equating the two placements gives the closed form below - identical to the
     * legacy CPU scatter that was confirmed visually correct:
     *     dst_x = s_blend_w - ly0 - cam_h ;  dst_y = lx0
     * (DASHCAM_BLEND_CAM_DST_OFF_X/Y stay available for any sensor/box edge nudge,
     *  default 0 now that the placement is computed.) */
    const int32_t cam_h = (int32_t)dashcam_camera_preview_height();
    int32_t lx0;
    int32_t ly0;
    if (area.x1 >= 0 && area.y1 >= 0)
    {
        lx0 = area.x1;
        ly0 = area.y1;
    }
    else
    {
        lx0 = DASHCAM_BLEND_CAM_DST_X;
        ly0 = DASHCAM_BLEND_CAM_DST_Y;
    }
    int32_t dst_x = (int32_t)s_blend_w - ly0 - cam_h + DASHCAM_BLEND_CAM_DST_OFF_X;
    int32_t dst_y = lx0 + DASHCAM_BLEND_CAM_DST_OFF_Y;
    if (dst_x < 0)
    {
        dst_x = 0;
    }
    if (dst_y < 0)
    {
        dst_y = 0;
    }
    s_blend_cam_dst_x = (uint16_t)dst_x;
    s_blend_cam_dst_y = (uint16_t)dst_y;

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
     * internal bg via CPU memcpy in the flush hook. The memcpy is serialised
     * against this worker's GPU read of bg by the GPU lock (see display_ui
     * bk_widgets_flush_cb), so CPU write and GPU read never touch bg at once. */
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
     * ISP NV12 frame into a single shared scratch and CPU-converts only the
     * visible image to RGB565 slots; the worker only GPU-blits RGB565. The NV12
     * scratch uses the 16-aligned capture height for its UV plane offset, but the
     * RGB565 source handed to VG-Lite must stay at the visible height. */
    s_blend_cam_w = (uint16_t)dashcam_camera_preview_width();
    s_blend_cam_h = (uint16_t)dashcam_camera_preview_height();
    s_blend_cam_buf_h = (uint16_t)DASHCAM_PREVIEW_CAPTURE_HEIGHT;
    s_blend_cam_size = (uint32_t)s_blend_cam_w * (uint32_t)s_blend_cam_h * 2u;
    s_blend_cam_nv12_size = (uint32_t)s_blend_cam_w * (uint32_t)s_blend_cam_buf_h *
                            DASHCAM_NV12_BYTES_PER_2_PIXELS / 2u;
    for (uint32_t i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
    {
        s_blend_cam_buf[i] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, s_blend_cam_size);
        s_blend_cam_state[i] = DASHCAM_BLEND_SLOT_FREE;
        if (s_blend_cam_buf[i] == NULL)
        {
            LOGE("blend_start: cam rgb565 slot #%u malloc(%u) failed (UNCODED aperture full)\n",
                 (unsigned)i, (unsigned)s_blend_cam_size);
            bk_mem_slab_dump_heap(MEM_SLAB_HEAP_UNCODED);
            goto fail;
        }
    }

    s_blend_cam_nv12 = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, s_blend_cam_nv12_size);
    if (s_blend_cam_nv12 == NULL)
    {
        LOGE("blend_start: cam nv12 scratch malloc(%u) failed (UNCODED aperture full)\n",
             (unsigned)s_blend_cam_nv12_size);
        bk_mem_slab_dump_heap(MEM_SLAB_HEAP_UNCODED);
        goto fail;
    }

    s_blend_dirty = false;
    s_blend_compose_ok = 0;
    s_blend_compose_fail = 0;
    s_blend_drop_no_buf = 0;
    s_blend_first_frame_logged = false;
    s_blend_last_flush_us = 0;

    /* Bring up the single GPU-owner compose worker before the producer. Keep it
     * pinned to AP core0 (CPU2), where the GPU IRQ is enabled, so VG-Lite submit
     * and finish/IRQ bookkeeping stay on the same core. The read task remains
     * unpinned and only does CPU read/convert work. */
    s_blend_fatal_error = false;
    s_blend_worker_stop = false;
    s_blend_gpu_recover_count = 0;
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

    LOGI("blend_start: active, ui=%ux%u rgb565=%u (fb alloc=%u), %u out buf(s), cam=%ux%u(buf %u) x%u slot(s), dst=(%u,%u), bg=%p, compose on core0 worker\n",
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
    if (s_blend_cam_nv12 != NULL)
    {
        bk_frame_buffer_free(s_blend_cam_nv12);
        s_blend_cam_nv12 = NULL;
    }
    s_blend_bg = NULL;  /* owned by s_blend, freed in lv_camera_blend_deinit */
    if (s_blend != NULL)
    {
        lv_camera_blend_deinit(s_blend);
        s_blend = NULL;
    }
    LOGW("blend_start: cleanup complete after failure\n");
    bk_mem_slab_dump_heap(MEM_SLAB_HEAP_UNCODED);
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
    if (s_blend_cam_nv12 != NULL)
    {
        bk_frame_buffer_free(s_blend_cam_nv12);
        s_blend_cam_nv12 = NULL;
    }
    s_blend_dirty = false;
    s_blend_last_flush_us = 0;
}

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

    /*
     * The blend pipeline gates read and compose to one cadence, so a standalone
     * "read fps" only mirrors the on-screen rate. Report the one true rate (disp
     * fps) and the average wall period per shown frame, then decompose it:
     *   read-task per frame  ~= slotwait + read + conv
     *   with 2 slots the read overlaps compose, so period ~= max(read, compose)
     * and the larger of {compose, read} is the real bottleneck.
     *   read    = ISP channel read                                [read task]
     *   conv    = CPU NV12->RGB565 convert                        [read task]
     *   slotwait= read task blocked waiting for a free compose slot [read task]
     *   compose = GPU page-composite (RGB565 blit) + DPU flush     [worker]
     *   blendproc = lv_camera_blend_process() duration             [worker]
     *   gpuw    = avg/max wait to acquire the shared lv_vendor GPU lock
     *   drop    = frames skipped because both DPU out buffers were busy
     */
    uint32_t disp_fps_x10 = (disp_frames * 10000u) / elapsed_ms;
    uint32_t period_ms = disp_frames ? (elapsed_ms / disp_frames) : 0u;
    uint32_t slotwait_avg_us = read_frames ? (uint32_t)(s_dashcam_video.stats_slot_wait_us / read_frames) : 0u;
    uint32_t read_avg_us = read_frames ? (uint32_t)(s_dashcam_video.stats_read_us / read_frames) : 0u;
    uint32_t conv_avg_us = conv_frames ? (uint32_t)(s_dashcam_video.stats_conv_us / conv_frames) : 0u;
    uint32_t comp_avg_us = disp_frames ? (uint32_t)(s_dashcam_video.stats_copy_us / disp_frames) : 0u;
    uint32_t blend_process_count = s_dashcam_video.stats_blend_process_count;
    uint32_t blend_process_avg_us = blend_process_count ?
                                    (uint32_t)(s_dashcam_video.stats_blend_process_us / blend_process_count) : 0u;
    uint32_t gpu_locks = s_dashcam_video.stats_gpu_lock_count;
    uint32_t gpuw_avg_us = gpu_locks ? (uint32_t)(s_dashcam_video.stats_gpu_wait_sum_us / gpu_locks) : 0u;
    uint32_t gpuw_max_us = s_dashcam_video.stats_gpu_wait_max_us;

    uint32_t drop = s_blend_drop_no_buf;
    s_blend_drop_no_buf = 0;

    LOGI("preview stats %ums: disp=%u.%ufps period=%ums | read-task slotwait=%uus read=%uus conv=%uf/%uus | worker compose=%uus blendproc=%uf/%uus gpuw=%u/%uus | drop=%u read_fail=%u\n",
         (unsigned)elapsed_ms,
         (unsigned)(disp_fps_x10 / 10u), (unsigned)(disp_fps_x10 % 10u),
         (unsigned)period_ms,
         (unsigned)slotwait_avg_us,
         (unsigned)read_avg_us,
         (unsigned)conv_frames, (unsigned)conv_avg_us,
         (unsigned)comp_avg_us,
         (unsigned)blend_process_count, (unsigned)blend_process_avg_us,
         (unsigned)gpuw_avg_us, (unsigned)gpuw_max_us,
         (unsigned)drop,
         (unsigned)s_dashcam_video.stats_read_failures);

    s_dashcam_video.stats_start_ms = now_ms;
    s_dashcam_video.stats_read_frames = 0;
    s_dashcam_video.stats_read_failures = 0;
    s_dashcam_video.stats_read_us = 0;
    s_dashcam_video.stats_slot_wait_us = 0;
    s_dashcam_video.stats_conv_us = 0;
    s_dashcam_video.stats_conv_frames = 0;
    s_dashcam_video.stats_copy_us = 0;
    s_dashcam_video.stats_blend_process_us = 0;
    s_dashcam_video.stats_blend_process_count = 0;
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

    /* The read task only runs for the live GPU-composited preview. It reads each
     * ISP NV12 frame into a shared scratch, CPU-converts it to RGB565 into a free
     * handoff slot, and hands the slot to the compose worker (which only GPU-blits
     * RGB565). Doing the convert here overlaps it with the worker's GPU compose.
     * frame_size is the NV12 read size set up by dashcam_video_blend_start(). */
    const uint32_t frame_size = s_blend_cam_nv12_size;
    uint32_t frame_count = 0;
    int ret;

    s_dashcam_video.stats_start_ms = rtos_get_time();
    s_dashcam_video.stats_read_frames = 0;
    s_dashcam_video.stats_read_failures = 0;
    s_dashcam_video.stats_read_us = 0;
    s_dashcam_video.stats_slot_wait_us = 0;
    s_dashcam_video.stats_conv_us = 0;
    s_dashcam_video.stats_conv_frames = 0;
    s_dashcam_video.stats_copy_us = 0;
    s_dashcam_video.stats_blend_process_us = 0;
    s_dashcam_video.stats_blend_process_count = 0;
    s_dashcam_video.stats_disp_frames = 0;

    while (!s_dashcam_video.read_stop_requested)
    {
        int slot = -1;
        uint64_t slot_wait_us = 0;

        /* Block until a handoff slot is free. With two slots this rarely blocks:
         * the read fills one slot while the worker composes the other (pipeline
         * overlap). Time the wait separately (slot_wait_us) so it never inflates
         * the camera read cost. */
        uint64_t wait_t0 = bk_aon_rtc_get_us();
        for (;;)
        {
            for (int i = 0; i < DASHCAM_BLEND_CAM_SLOTS; i++)
            {
                if (s_blend_cam_state[i] == DASHCAM_BLEND_SLOT_FREE)
                {
                    slot = i;
                    break;
                }
            }
            if (slot >= 0 || s_dashcam_video.read_stop_requested)
            {
                break;
            }
            /* Back off rather than stomp a frame the worker is still reading. */
            rtos_delay_milliseconds(2);
        }
        slot_wait_us = bk_aon_rtc_get_us() - wait_t0;
        if (s_dashcam_video.read_stop_requested)
        {
            break;
        }

        /* Read the ISP NV12 frame into the shared NV12 scratch (bk_isp_camera_read
         * CPU-memcpys it in); the CPU convert below writes RGB565 into the slot. */
        uint8_t *target = (uint8_t *)s_blend_cam_nv12;

        uint64_t read_t0 = bk_aon_rtc_get_us();
#if DASHCAM_VERBOSE_READ_LOG
        LOGD("read#%u: read_start (slot=%d)\n", (unsigned)(frame_count + 1), slot);
#endif
        ret = dashcam_camera_read_preview(target, frame_size, DASHCAM_CAMERA_READ_TIMEOUT_MS);
#if DASHCAM_VERBOSE_READ_LOG
        LOGD("read#%u: read_done ret=%d\n", (unsigned)(frame_count + 1), ret);
#endif
        if (ret == BK_OK)
        {
            s_dashcam_video.stats_read_us += bk_aon_rtc_get_us() - read_t0;

            if (s_dashcam_video.running &&
                s_dashcam_video.source == DASHCAM_VIDEO_SOURCE_FRONT_MIPI)
            {
                /* CPU-convert the NV12 scratch to RGB565 into the slot, then clean
                 * (write-back) the slot's cache lines so the GPU compose worker
                 * sees the fresh pixels, and hand the slot off. Running the convert
                 * here (not on the worker) lets it overlap the worker's GPU blit. */
                uint64_t conv_t0 = bk_aon_rtc_get_us();
                bool converted = dashcam_video_blend_cpu_convert_nv12_to_rgb(
                    target, (uint16_t *)s_blend_cam_buf[slot],
                    s_blend_cam_w, s_blend_cam_h);
                if (!converted)
                {
#if DASHCAM_VERBOSE_READ_LOG
                    LOGD("read#%u: convert aborted by stop\n", (unsigned)(frame_count + 1));
#endif
                    break;
                }
                s_dashcam_video.stats_conv_us += bk_aon_rtc_get_us() - conv_t0;
                s_dashcam_video.stats_conv_frames++;
                arch_dcache_flush_range(s_blend_cam_buf[slot], s_blend_cam_size);
                s_blend_cam_state[slot] = DASHCAM_BLEND_SLOT_READY;
#if DASHCAM_VERBOSE_READ_LOG
                LOGD("read#%u: handoff slot=%d\n",
                     (unsigned)frame_count, slot);
#endif
                if (s_blend_worker_sem != NULL)
                {
                    rtos_set_semaphore(&s_blend_worker_sem);
                }
            }

            s_dashcam_video.stats_slot_wait_us += slot_wait_us;
            s_dashcam_video.stats_read_frames++;

            frame_count++;
            if (frame_count == 1 || (frame_count % 30u) == 0u)
            {
                LOGI("preview frame %u size=%u\n", (unsigned)frame_count, (unsigned)frame_size);
            }
        }
        else
        {
            s_dashcam_video.stats_read_failures++;
            if (!s_dashcam_video.read_stop_requested &&
                (s_dashcam_video.stats_read_failures == 1u ||
                 (s_dashcam_video.stats_read_failures % 30u) == 0u))
            {
                LOGW("preview read timeout/fail: %d\n", ret);
            }
        }

        dashcam_video_stats_maybe_log(rtos_get_time());
    }

    if (s_blend_fatal_error)
    {
        LOGW("preview read task fatal exit, closing camera to stop ISP callbacks\n");
        dashcam_camera_close();
        s_dashcam_video.running = false;
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

    /* The read task no longer touches the GPU (the dedicated compose worker is
     * the sole vg_lite owner), so it does not need core pinning. Keeping it
     * unpinned also moves its NV12 cache-clean away from the GPU IRQ core - the
     * flush_and_invd pinned to that core was the heartbeat-wedge trigger. */
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

    /* ---- playback canvas path (start_read == false only) ----------------
     * Decoded clip frames are pushed in via dashcam_video_on_frame() and shown
     * through an LVGL canvas; the read task and GPU compositor are not used. */
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
    LOGI("start %s sink %ux%u\n",
         dashcam_video_source_name(s_dashcam_video.source),
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
    s_blend_fatal_error = false;

    if (s_blend_active || s_blend != NULL)
    {
        /* Page exit must first restore the normal LVGL->DPU path and stop queueing
         * preview composites. The read task may still be inside a timed camera
         * read or CPU conversion; it observes read_stop_requested/running below and
         * exits without handing off another slot. Wake the worker too so it does
         * not sit in its timed wait during teardown. */
        display_ui_blend_detach();
        s_blend_active = false;
        s_blend_worker_stop = true;
        if (s_blend_worker_sem != NULL)
        {
            rtos_set_semaphore(&s_blend_worker_sem);
        }
    }

    dashcam_video_stop_read_task();

    /* Live preview: the read task (sole GPU user) has exited; tear the compositor
     * down and restore the normal LVGL->DPU flush. Inactive for playback sink, so
     * the canvas teardown below runs instead. */
    if (s_blend_active || s_blend != NULL)
    {
        dashcam_video_blend_stop();
        s_dashcam_video.parent = NULL;
        s_dashcam_video.width = 0;
        s_dashcam_video.height = 0;
        return;
    }

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
