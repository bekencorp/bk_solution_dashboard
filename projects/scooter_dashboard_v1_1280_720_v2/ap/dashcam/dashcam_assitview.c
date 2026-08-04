#include <os/os.h>
#include <os/mem.h>
#include "dashcam_assitview.h"
#include "dashcam_config.h"
#include "app_gpu.h"
#include "display_ui_cast_context.h"
#include "beken_ui.h"
#include "lvgl.h"
#include "components/log.h"
#include "components/bk_flexa_bond.h"
#include "app_camera.h"
#include "display_ui_cast_hooks.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "dashcam_camera.h"
#include "dashcam_ui.h"
#include <modules/vg_lite_gpu/vg_lite.h>
#include <cache.h>

#define TAG "d_assit"

#define LOGI(...) BK_LOGI(TAG, __VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, __VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, __VA_ARGS__)

static gpu_board_config_t gpu_board;
static void *s_gpu_bond = NULL;
static bool s_assitview_active = false;

/*
 * GPU (vg_lite) ownership handoff between LVGL and the assist bond.
 *
 * After the display_ui output_compress refactor, LVGL renders every frame
 * through vg_lite and OWNS the single global vg_lite context: it is created once
 * in lv_vendor_init() (lv_gpu_init -> vg_lite_init) and only torn down in
 * lv_vendor_deinit(). lv_vendor_start()/lv_vendor_stop() just start/stop the
 * LVGL task and do NOT touch vg_lite.
 *
 * The assist bond drives the same GPU via app_gpu_turn_on()/app_gpu_turn_off(),
 * and app_gpu_turn_off() -> bk_gpu_deinit() FULLY closes vg_lite (vg_lite_close
 * + frees the contiguous command-buffer heap). If we let that happen while LVGL
 * still believes it owns a live context, LVGL's first compressed flush after
 * assist dereferences the freed context and MemFaults in set_render_target().
 *
 * So we hand the GPU off explicitly: release LVGL's vg_lite once the LVGL task
 * is stopped and before the assist bond takes the GPU, and re-acquire it after
 * the assist bond is fully torn down and before LVGL resumes.
 *
 * We reuse LVGL's own GPU lifecycle (lv_gpu_init/lv_gpu_deinit, i.e.
 * bk_gpu_driver_init + vg_lite_init) rather than app_gpu_turn_on/off: the latter
 * is the ISP->GPU flexa *controller* (needs an ISP handle + gpu_board_config,
 * spins up the gpu worker thread and pingpong buffers) which LVGL never uses.
 * lv_gpu_init(0,0) exactly mirrors how lv_vendor_init() created the context. */
extern void lv_gpu_init(uint32_t tess_width, uint32_t tess_height);
extern void lv_gpu_deinit(void);

static void dashcam_assitview_lvgl_gpu_release(void)
{
    lv_gpu_deinit();
}

static void dashcam_assitview_lvgl_gpu_acquire(void)
{
    lv_gpu_init(0, 0);
}

void dashcam_assitview_init(void)
{
    LOGI("dashcam_assitview_init\n");
    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 90;
    /* GPU src MUST match the ISP MP channel's real output (record NV12), not the
     * sensor size: the GPU flexa engine only fires frame_done once read_lines
     * reaches src_height, and MP only produces DASHCAM_RECORD_HEIGHT lines.
     * MP is now configured to output 1280x720 (DASHCAM_RECORD_*) so that
     * src == dst and the GPU only rotates (no scaling), exactly like the
     * doorbell full-screen path (1920x1080 src==dst, rotate 90). */
    gpu_board.flexa.src_width = DASHCAM_RECORD_WIDTH;
    gpu_board.flexa.src_height = DASHCAM_RECORD_HEIGHT;
    /* dst is the pre-rotation (landscape) size; after degree=90 it becomes the
     * panel's 720x1280 portrait scanout (hx8394f h_size=720, v_size=1280). */
    gpu_board.flexa.dst_width = 1280;
    gpu_board.flexa.dst_height = 720;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    /* src (1280x720) == dst (1280x720) -> rotate only, no scaling. This makes
     * the GPU output fill the full 720x1280 panel (fixes partial coverage). */
    gpu_board.flexa.scale = false;
    gpu_board.flexa.tess_width = 0;
    gpu_board.flexa.tess_height = 0;

    app_gpu_board_config_set(&gpu_board);
}

void dashcam_assitview_gpu_bond_attach(void)
{
    int ret;

    /* The GPU flexa bond consumes ISP MP line-done events, so the MP channel
     * MUST be streaming (MP flexa) before we turn the GPU on.
     *
     * Continue-recording path: the assist view is entered WITHOUT tearing the
     * recorder down (beken_ui_before_assist_lvgl_teardown), so the camera is
     * still open (MP->H264) and app_isp_handle_get() is valid. We simply add a
     * SECOND MP flexa bond (MP->GPU) on the same ISP MP output -> recording
     * keeps running while the GPU drives the full-screen view.
     *
     * dashcam_camera_open() is idempotent: if the recorder already brought the
     * camera up it just returns BK_OK; otherwise it opens it here. */
    ret = dashcam_camera_open();
    if (ret != BK_OK) {
        LOGE("dashcam_camera_open failed, ret: %d", ret);
        return;
    }

    if (app_isp_handle_get() == NULL) {
        LOGE("isp handle NULL after camera open; cannot bond GPU");
        return;
    }

    /* Diagnostic: gpu_ctlr_init() allocates CONFIG_VG_LITE_GPU_CONTIGUOUS_MEM_SZ
     * (64KB) for the vg_lite heap AND, right after, a ~40KB pingpong output
     * buffer (bk_get_gpu_output_buffer), both from the same HSRAM heap. Log total
     * free and trial-allocate the vg_lite heap size to tell "total shortage"
     * (free < 64KB) apart from "fragmentation" (free >> 64KB but trial fails). */
    {
        extern size_t rtos_get_hsram_free_heap_size(void);
        extern size_t rtos_get_hsram_minimum_free_heap_size(void);
        void *probe = hsram_malloc(CONFIG_VG_LITE_GPU_CONTIGUOUS_MEM_SZ);
        LOGI("HSRAM before gpu_on: free=%u min_ever=%u trial_vglite(%uK)=%s",
             (unsigned)rtos_get_hsram_free_heap_size(),
             (unsigned)rtos_get_hsram_minimum_free_heap_size(),
             (unsigned)(CONFIG_VG_LITE_GPU_CONTIGUOUS_MEM_SZ / 1024),
             probe ? "OK" : "FAIL");
        if (probe != NULL) {
            hsram_free(probe);
        }
    }

    ret = app_gpu_turn_on(app_gpu_board_config_get());
    if (ret != 0) {
        LOGE("app_gpu_turn_on failed, ret: %d", ret);
        return;
    }

    s_gpu_bond = NULL;
    ret = bk_flexa_isp_gpu_bond_start(&s_gpu_bond, app_isp_handle_get(), app_gpu_handle_get());
    if (ret != 0) {
        LOGE("bk_flexa_isp_gpu_bond_start failed, ret: %d", ret);
        (void)app_gpu_turn_off(app_gpu_handle_get());
        s_gpu_bond = NULL;
        return;
    }
    LOGI("assitview gpu bond attached (src %dx%d dst %dx%d)\n",
         gpu_board.flexa.src_width, gpu_board.flexa.src_height,
         gpu_board.flexa.dst_width, gpu_board.flexa.dst_height);
}

void dashcam_assitview_deinit(void)
{
}

bool dashcam_assitview_is_active(void)
{
    return s_assitview_active;
}

void dashcam_assitview_start(void)
{
    uint32_t post_paint_ms = 5;
    extern void lvgl_app_dpu_apply_casting_config(void);
    extern void beken_ui_before_assist_lvgl_teardown(void);
    extern size_t rtos_get_hsram_free_heap_size(void);

    if (s_assitview_active)
    {
        return;
    }
    s_assitview_active = true;

#define ASSIT_HSRAM_PROBE(step) \
    LOGI("HSRAM[%s]: free=%u\n", (step), (unsigned)rtos_get_hsram_free_heap_size())

    ASSIT_HSRAM_PROBE("0.enter");

    /*
     * Do the LVGL-touching work (page teardown) under disp_lock, then RELEASE
     * the lock BEFORE lv_vendor_stop(). lv_vendor_stop() blocks (NEVER timeout)
     * on the LVGL task's exit semaphore, but the LVGL task can only exit its
     * loop after acquiring g_disp_mutex each iteration. Holding disp_lock across
     * lv_vendor_stop() therefore deadlocks: we wait for the LVGL task while it
     * waits for our lock. (cast's frame_display path only gets away with holding
     * it because the LVGL task is usually idle in rtos_delay at that moment.)
     *
     * Unlike the cast path, this uses the *assist* teardown: it leaves the
     * standby pages and pauses the segment tick but KEEPS the recorder + camera
     * (mode=3, MP->H264) running, so recording continues while the GPU bond
     * (MP->GPU) drives the full-screen assist view off the same ISP MP output.
     */
    lv_vendor_disp_lock();
    beken_ui_before_assist_lvgl_teardown();
    if (post_paint_ms > 0U) {
        LOGI("[cast] post page-switch delay %u ms\n", (unsigned)post_paint_ms);
        rtos_delay_milliseconds((uint32_t)post_paint_ms);
    }
    lv_vendor_disp_unlock();
    ASSIT_HSRAM_PROBE("1.after_ui_teardown");

    lv_vendor_stop();
    ASSIT_HSRAM_PROBE("2.after_lv_vendor_stop");

    /* LVGL task is now stopped: release its vg_lite context so the assist bond
     * can take exclusive GPU ownership (re-acquired in dashcam_assitview_stop). */
    dashcam_assitview_lvgl_gpu_release();

    lvgl_app_dpu_apply_casting_config();
    ASSIT_HSRAM_PROBE("3.after_dpu_casting_cfg");

    dashcam_assitview_gpu_bond_attach();

#undef ASSIT_HSRAM_PROBE
}

void dashcam_assitview_stop(void)
{
    if (!s_assitview_active)
    {
        return;
    }

    if (s_gpu_bond != NULL) {
        bk_flexa_isp_gpu_bond_stop(s_gpu_bond);
        s_gpu_bond = NULL;
    }
    if (app_gpu_handle_get() != NULL) {
        (void)app_gpu_turn_off(app_gpu_handle_get());
    }
    /* app_gpu_turn_off() closed vg_lite; re-acquire it for LVGL BEFORE resuming
     * the LVGL task, otherwise LVGL's first compressed flush faults on the freed
     * context. */
    dashcam_assitview_lvgl_gpu_acquire();
    /* DPU stays ARGB8888 + decompress the whole time now (LVGL renders through
     * the same GPU-compress path), so there is no pixel-format to restore when
     * leaving assist -- just bring LVGL back up. */
    lv_vendor_start();
    rtos_delay_milliseconds(50);
    lv_vendor_disp_lock();
    extern void beken_ui_kick_after_display_resume(void);
    beken_ui_kick_after_display_resume();
    lv_vendor_disp_unlock();

    /* Recording was never stopped while assisting; LVGL is back now, so re-arm
     * the paused segment-rotation tick. */
    dashcam_ui_resume_keep_recording();
    s_assitview_active = false;
}