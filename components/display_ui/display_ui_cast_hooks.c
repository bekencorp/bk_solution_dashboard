#include "display_ui_cast_hooks.h"

#include <os/os.h>
#include <common/bk_err.h>
#include <stdint.h>
#include <components/log.h>
#include <components/bk_display.h>
#include "cast_jpeg_pipeline.h"
#include "lv_vendor.h"

#include "display_ui_cast_context.h"

#define TAG "display_ui_cast"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

extern void beken_ui_kick_after_display_resume(void);

/*
 * LVGL's own GPU lifecycle (lv_vendor.c): lv_gpu_init == bk_gpu_driver_init +
 * vg_lite_init(0,0), lv_gpu_deinit == vg_lite_close + bk_gpu_driver_deinit.
 * LVGL owns the single global vg_lite context; the cast pipeline's plain teardown
 * (jpeg_stream_pipeline_destroy -> bk_gpu_deinit) closes that shared context, so
 * we must re-acquire it here BEFORE restarting the LVGL task, exactly like the
 * assist-view path does. Safe to call when vg_lite is still alive: vg_lite_init
 * short-circuits ("already initialized") and bk_gpu_driver_init is a bool guard.
 */
extern void lv_gpu_init(uint32_t tess_width, uint32_t tess_height);
extern void lv_gpu_deinit(void);

/*
 * Before casting/CMD path starts LVGL teardown: the product (e.g. scooter_1280_720 beken_ui.c)
 * may override with a strong symbol (e.g. switch page then teardown). Default weak impl is
 * no-op and zero delay.
 */
__attribute__((weak)) void beken_ui_before_cast_lvgl_teardown(void) {}
__attribute__((weak)) uint32_t beken_ui_after_cast_ui_painted_delay_ms(void)
{
    return 0;
}

static volatile int s_dpu_casting_reopen_pending = 0;
static volatile int s_lvgl_suspended_for_cast = 0;
/* Set when this cast session actually called lv_vendor_stop. */
static int s_cast_lvgl_was_stopped = 0;
static volatile int s_dpu_switched_to_cast = 0;
static int s_cast_hooks_registered = 0;

static avdk_err_t cast_bank_steer_noop_cb(void *frame)
{
    (void)frame;
    return AVDK_ERR_OK;
}

bk_err_t lvgl_app_suspend_display(void)
{
    LOGI("LVGL display suspend deferred to first casting frame\n");

    if (display_ui_get_dpu_handle()) {
        s_dpu_casting_reopen_pending = 1;
        LOGI("DPU casting config deferred to first frame\n");
    }
    s_lvgl_suspended_for_cast = 1;
    return BK_OK;
}

void lvgl_app_dpu_apply_casting_config(void)
{
    /*
     * LVGL renders through the GPU-compressed ARGB8888 path (display_ui.c:
     * output_compress), so the DPU is permanently ARGB8888 + decompress -- the
     * exact format cast/GPU output needs. The old bk_display_pixel_format_set()
     * runtime switch is therefore redundant (it also raced the pending flush:
     * "dpu_core_runtime_switch wait pending frame failed"). Just record state;
     * the DPU already carries the right format.
     */
    LOGI("[cast] dpu already ARGB8888+decompress; skip runtime format switch\n");
    s_dpu_casting_reopen_pending = 0;
    s_dpu_switched_to_cast = 1;
    LOGI("[cast] dpu ok\n");
}

bk_err_t lvgl_app_resume_display(void)
{
    if (!s_lvgl_suspended_for_cast) {
        return BK_OK;
    }
    s_lvgl_suspended_for_cast = 0;

    if (s_dpu_casting_reopen_pending) {
        s_dpu_casting_reopen_pending = 0;
        /*
         * suspend_display() (e.g. provisioning) only defers teardown; it never
         * calls lv_vendor_stop, so s_cast_lvgl_was_stopped stays 0 and LVGL must
         * not be lv_vendor_start()ed again. The cast hook path sets it in pre_start.
         */
        if (s_cast_lvgl_was_stopped) {
            s_cast_lvgl_was_stopped = 0;
            /* Cast teardown closed the shared vg_lite; re-acquire before LVGL runs. */
            lv_gpu_init(0, 0);
            lv_vendor_start();
            rtos_delay_milliseconds(50);
            lv_vendor_disp_lock();
            beken_ui_kick_after_display_resume();
            lv_vendor_disp_unlock();
            LOGI("LVGL display resume: casting cancelled after LVGL stop, restarted\n");
        } else {
            LOGI("LVGL display resume: cast cancelled before first frame, LVGL still running\n");
        }
        return BK_OK;
    }

    if (s_dpu_switched_to_cast && display_ui_get_dpu_handle()) {
        avdk_err_t fr;

        s_dpu_switched_to_cast = 0;
        /*
         * DPU format no longer differs between LVGL and cast: both scan out
         * ARGB8888 + decompress (LVGL uses the GPU-compress path now). So the
         * pixel-format restore is gone; we only need to repoint the DPU scanout
         * back at the LVGL frame buffer (cast steered it to its own pool).
         */
        if (display_ui_get_lvgl_fb(0)) {
            fr = bk_display_flush(display_ui_get_dpu_handle(),
                                  display_ui_get_lvgl_fb(0), cast_bank_steer_noop_cb);
            LOGI("[cast] restore flush lvgl fb[0] %p r=%d\n",
                 (void *)display_ui_get_lvgl_fb(0), (int)fr);
        } else {
            LOGW("[cast] restore skip flush: lvgl fb[0] is NULL\n");
        }
    } else if (!s_dpu_switched_to_cast) {
        LOGI("DPU was not switched to cast mode; skip restore\n");
    }

    if (s_cast_lvgl_was_stopped) {
        s_cast_lvgl_was_stopped = 0;
        /* Cast teardown closed the shared vg_lite; re-acquire before LVGL runs. */
        lv_gpu_init(0, 0);
        lv_vendor_start();
        rtos_delay_milliseconds(50);
        lv_vendor_disp_lock();
        beken_ui_kick_after_display_resume();
        lv_vendor_disp_unlock();
        LOGI("LVGL display resumed\n");
    } else {
        LOGI("LVGL still running after cast end; skip lv_vendor_start\n");
    }
    return BK_OK;
}

static void display_ui_cast_pre_start(void)
{
    uint32_t post_paint_ms = beken_ui_after_cast_ui_painted_delay_ms();

    s_cast_lvgl_was_stopped = 0;
    s_lvgl_suspended_for_cast = 1;

    /*
     * Stop LVGL and release the single vg_lite/GPU engine BEFORE the cast
     * JPEG->GPU pipeline is created/started (cast_jpeg_pipeline_turn_on calls
     * this right before jpeg_stream_pipeline_create/start).
     *
     * The cast decoder's flexa bond needs the GPU to drain its output ring; if
     * LVGL keeps compositing (GPU-compress every frame) the bond starves ->
     * "wait flexa registered ports done" / vcdec decode timeout, and the first
     * cast frame never arrives. Deferring the stop to the first frame therefore
     * deadlocks (no GPU -> no first frame -> no stop). Do it up front instead.
     */
    /*
     * Do the LVGL-touching teardown (page leave + lv_timer_delete via the product
     * hook) UNDER the disp lock so the LVGL task is parked on g_disp_mutex and the
     * timers are still on the active list, then RELEASE the lock BEFORE
     * lv_vendor_stop().
     *
     * lv_vendor_stop() sets STATE_STOP and blocks (NEVER timeout) on the LVGL
     * task's exit semaphore, but that task only reaches the exit / semaphore after
     * re-acquiring g_disp_mutex at the top of every loop iteration. Holding the
     * disp lock across lv_vendor_stop() therefore deadlocks: turn_on hangs inside
     * pre_start, never reaches cast_video_recv_gate_call(1), and the WiFi unfragment
     * alloc gate stays closed forever -> endless "frame_malloc: denied (cast alloc
     * gate)". This mirrors the assist-view teardown (dashcam_assitview.c).
     */
    lv_vendor_disp_lock();
    beken_ui_before_cast_lvgl_teardown();
    lv_vendor_disp_unlock();

    if (post_paint_ms > 0U) {
        LOGI("[cast] post page-switch delay %u ms\n", (unsigned)post_paint_ms);
        rtos_delay_milliseconds((uint32_t)post_paint_ms);
    }

    lv_vendor_stop();
    lv_gpu_deinit();
    s_cast_lvgl_was_stopped = 1;

    lvgl_app_dpu_apply_casting_config();

    LOGI("[cast] pipeline prep: LVGL stopped + GPU released before pipeline create\n");
}

static void display_ui_cast_first_frame_apply(void)
{
    /*
     * LVGL stop + DPU cast config now happen in display_ui_cast_pre_start (before
     * the pipeline is created) so the cast GPU bond is not starved by LVGL.
     * Nothing left to do on the first frame.
     */
    LOGI("[cast] first frame (lvgl already stopped in pre_start)\n");
}

static void display_ui_cast_post_stop(void)
{
    (void)lvgl_app_resume_display();
}

void display_ui_register_cast_hooks_once(void)
{
    cast_jpeg_pipeline_hooks_t hooks = { 0 };

    if (s_cast_hooks_registered)
        return;

    hooks.pre_start = display_ui_cast_pre_start;
    hooks.first_frame_apply = display_ui_cast_first_frame_apply;
    hooks.post_stop = display_ui_cast_post_stop;
    cast_jpeg_pipeline_register_hooks(&hooks);
    s_cast_hooks_registered = 1;
}
