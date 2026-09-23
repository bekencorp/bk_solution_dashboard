#include <os/os.h>
#include <os/mem.h>
#include "dashcam_assitview.h"
#include "dashcam_config.h"
#include "app_gpu.h"
#include "display_ui.h"
#include "display_ui_cast_context.h"
#include "lvgl.h"
#include "components/log.h"
#include "components/bk_flexa_bond.h"
#include "app_camera.h"
#include "display_ui_cast_hooks.h"
#include "lv_vendor.h"
#include "dashcam_camera.h"
#include "dashcam_video.h"
#include <modules/vg_lite_gpu/vg_lite.h>
#include <cache.h>

#define TAG "d_assit"

#define LOGI(...) BK_LOGI(TAG, __VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, __VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, __VA_ARGS__)

static gpu_board_config_t gpu_board;
static void *s_gpu_bond = NULL;
static bool s_assitview_active = false;
static dashcam_assitview_hooks_t s_assitview_hooks = {0};

void dashcam_assitview_init(void)
{
    LOGI("dashcam_assitview_init\n");
    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = DASHCAM_ASSIST_ROTATION;
    /* GPU src MUST match the ISP MP channel's real output (record NV12), not the
     * sensor size: the GPU flexa engine only fires frame_done once read_lines
     * reaches src_height, and MP only produces DASHCAM_RECORD_HEIGHT lines.
     * MP remains 1280x720 (DASHCAM_RECORD_*), while the EK79007AD panel is
     * landscape-native 1024x600. */
    gpu_board.flexa.src_width = DASHCAM_RECORD_WIDTH;
    gpu_board.flexa.src_height = DASHCAM_RECORD_HEIGHT;
    gpu_board.flexa.dst_width = DASHCAM_ASSIST_DST_WIDTH;
    gpu_board.flexa.dst_height = DASHCAM_ASSIST_DST_HEIGHT;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = DASHCAM_ASSIST_SCALE;
    gpu_board.flexa.tess_width = 0;
    gpu_board.flexa.tess_height = 0;

    app_gpu_board_config_set(&gpu_board);
}

static bk_err_t dashcam_assitview_gpu_bond_attach(void)
{
    int ret;

    /* The GPU flexa bond consumes ISP MP line-done events, so the MP channel
     * MUST be streaming (MP flexa) before we turn the GPU on.
     *
     * Continue-recording path: the camera is already open with MP->H264, and we
     * add MP->GPU on the same ISP output. Assist-only path opens ISP MP without
     * creating an H264 encoder or bond.
     *
     * The assist ownership API is idempotent and keeps ISP alive independently
     * of the recording owner. */
    ret = dashcam_camera_open_for_assist();
    if (ret != BK_OK)
    {
        LOGE("dashcam_camera_open_for_assist failed, ret: %d", ret);
        return BK_FAIL;
    }

    if (app_isp_handle_get() == NULL)
    {
        LOGE("isp handle NULL after camera open; cannot bond GPU");
        dashcam_camera_close_for_assist();
        return BK_FAIL;
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
        if (probe != NULL)
        {
            hsram_free(probe);
        }
    }

    ret = app_gpu_turn_on(app_gpu_board_config_get());
    if (ret != 0)
    {
        LOGE("app_gpu_turn_on failed, ret: %d", ret);
        dashcam_camera_close_for_assist();
        return BK_FAIL;
    }

    s_gpu_bond = NULL;
    ret = bk_flexa_isp_gpu_bond_start(&s_gpu_bond, app_isp_handle_get(), app_gpu_handle_get());
    if (ret != 0)
    {
        LOGE("bk_flexa_isp_gpu_bond_start failed, ret: %d", ret);
        (void)app_gpu_turn_off(app_gpu_handle_get());
        s_gpu_bond = NULL;
        dashcam_camera_close_for_assist();
        return BK_FAIL;
    }
    LOGI("assitview gpu bond attached (src %dx%d dst %dx%d)\n",
         gpu_board.flexa.src_width, gpu_board.flexa.src_height,
         gpu_board.flexa.dst_width, gpu_board.flexa.dst_height);
    return BK_OK;
}

void dashcam_assitview_deinit(void)
{
}

void dashcam_assitview_register_hooks(const dashcam_assitview_hooks_t *hooks)
{
    if (hooks == NULL)
    {
        s_assitview_hooks.before_lvgl_teardown = NULL;
        s_assitview_hooks.after_lvgl_deinit = NULL;
        s_assitview_hooks.after_display_resume = NULL;
        return;
    }

    s_assitview_hooks = *hooks;
}

bool dashcam_assitview_is_active(void)
{
    return s_assitview_active;
}

void dashcam_assitview_start(void)
{
    uint32_t post_paint_ms = 5;
    extern void lvgl_app_dpu_apply_casting_config(void);
    extern size_t rtos_get_hsram_free_heap_size(void);

    if (s_assitview_active)
    {
        return;
    }

    if (s_assitview_hooks.before_lvgl_teardown == NULL)
    {
        LOGE("before_lvgl_teardown hook is not registered\n");
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
    s_assitview_hooks.before_lvgl_teardown();
    if (post_paint_ms > 0U)
    {
        LOGI("[cast] post page-switch delay %u ms\n", (unsigned)post_paint_ms);
        rtos_delay_milliseconds((uint32_t)post_paint_ms);
    }
    lv_vendor_disp_unlock();
    ASSIT_HSRAM_PROBE("1.after_ui_teardown");

    /*
     * The dashcam playback worker exclusively owns player/GPU/LVGL handoff.
     * Wait for it to close the decoder and restore LVGL before this assist path
     * stops LVGL and takes GPU ownership.
     */
    if (dashcam_video_stop_sync(3000U) != BK_OK)
    {
        LOGE("wait dashcam playback GPU release failed\n");
        return;
    }

    lv_vendor_stop();
    ASSIT_HSRAM_PROBE("2.after_lv_vendor_stop");

    /*
     * Release the complete LVGL runtime, not only its worker task and VG-Lite
     * context. This also releases the 120 KiB partial draw buffer and the
     * software-render worker stack needed by Assist GPU while recording.
     * Display frame buffers are owned and retained by display_ui for reinit.
     */
    if (display_ui_deinit_lvgl() != BK_OK)
    {
        LOGE("display_ui_deinit_lvgl failed\n");
        s_assitview_active = false;
        return;
    }
    if (s_assitview_hooks.after_lvgl_deinit != NULL)
    {
        s_assitview_hooks.after_lvgl_deinit();
    }

    lvgl_app_dpu_apply_casting_config();
    ASSIT_HSRAM_PROBE("3.after_lvgl_deinit");

    if (dashcam_assitview_gpu_bond_attach() != BK_OK)
    {
        /*
         * LVGL is already gone at this point, so returning here would leave a
         * dead screen with s_assitview_active still set - the UI only came
         * back because the user happened to press the exit key. Unwind through
         * the normal stop path instead: the bond and GPU are already released
         * by the attach error paths, camera close is idempotent, and it is
         * what rebuilds LVGL and clears the flag.
         */
        LOGE("assist bring-up failed, restoring LVGL UI\n");
        dashcam_assitview_stop();
    }

#undef ASSIT_HSRAM_PROBE
}

void dashcam_assitview_stop(void)
{
    if (!s_assitview_active)
    {
        return;
    }

    if (s_gpu_bond != NULL)
    {
        bk_flexa_isp_gpu_bond_stop(s_gpu_bond);
        s_gpu_bond = NULL;
    }
    if (app_gpu_handle_get() != NULL)
    {
        (void)app_gpu_turn_off(app_gpu_handle_get());
    }
    dashcam_camera_close_for_assist();
    /*
     * lv_vendor_deinit() destroyed the complete LVGL runtime. Recreate it and
     * the HOME object tree while reusing display_ui's retained frame buffers.
     * Keep Assist marked active during init so the UI does not schedule a
     * second automatic recording start.
     */
    if (display_ui_start_lvgl() != BK_OK)
    {
        LOGE("display_ui_start_lvgl failed after assist\n");
        return;
    }

    s_assitview_active = false;
    LOGI("assist exit: LVGL fully reinitialized\n");
}