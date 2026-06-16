#include "display_ui_cast_hooks.h"

#include <os/os.h>
#include <common/bk_err.h>
#include <stdint.h>
#include <components/log.h>
#include <components/bk_display.h>
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>
#include "cast_jpeg_pipeline.h"
#include "lv_vendor.h"

#include "display_ui_cast_context.h"

#define TAG "display_ui_cast"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

extern void beken_ui_kick_after_display_resume(void);

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
static volatile int s_lvgl_stopped_for_cast = 0;
static volatile int s_lvgl_suspended_for_cast = 0;
/* Set when this cast session actually called lv_vendor_stop (first frame path). */
static int s_cast_lvgl_was_stopped = 0;
static volatile int s_cast_decompress_enabled = 1;
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

    if (display_ui_get_ctx() && display_ui_get_ctx()->dpu_ctlr_handle) {
        s_dpu_casting_reopen_pending = 1;
        LOGI("DPU casting config deferred to first frame\n");
    }
    s_lvgl_suspended_for_cast = 1;
    return BK_OK;
}

void lvgl_app_stop_for_casting(void)
{
    if (!s_dpu_casting_reopen_pending) {
        LOGW("[cast] sf skip pend0\n");
        return;
    }
    if (s_lvgl_stopped_for_cast) {
        LOGW("[cast] sf skip stp\n");
        return;
    }
    lv_vendor_stop();
    s_lvgl_stopped_for_cast = 1;

    if (display_ui_get_ctx() && display_ui_get_ctx()->dpu_ctlr_handle && display_ui_get_lvgl_fb(1)) {
        avdk_err_t fr = bk_display_flush(display_ui_get_ctx()->dpu_ctlr_handle, display_ui_get_lvgl_fb(1),
                                         cast_bank_steer_noop_cb);
        LOGI("[cast] st %p r=%d\n", display_ui_get_lvgl_fb(1), (int)fr);
        rtos_delay_milliseconds(30);
    } else {
        LOGE("[cast] st !ctx %p %p %p\n",
             (void *)display_ui_get_ctx(),
             display_ui_get_ctx() ? (void *)display_ui_get_ctx()->dpu_ctlr_handle : NULL,
             (void *)display_ui_get_lvgl_fb(1));
    }
    LOGI("DPU steered to fb[1] (PSRAM bank 0) for contention-free GPU compress\n");
}

void lvgl_app_dpu_apply_casting_config(void)
{
    bk_display_dpu_config_t cast_config = *display_ui_get_dpu_config();
    cast_config.video.decompress = true;
    cast_config.video.format = BK_PIXEL_FORMAT_ARGB8888;
    LOGI("[diag] dpu apply cast: decompress=%d format=%d disp=%ux%u\n",
         (int)cast_config.video.decompress, (int)cast_config.video.format,
         (unsigned)cast_config.video.disp_w, (unsigned)cast_config.video.disp_h);
    if (display_ui_get_ctx() && display_ui_get_ctx()->dpu_ctlr_handle) {
        bk_display_pixel_format_set(display_ui_get_ctx()->dpu_ctlr_handle,
                                          &(bk_display_pixel_format_config_t) {
                                              .format = cast_config.video.format,
                                              .decompress = cast_config.video.decompress,
                                          });
    }
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
         * Cast may defer lv_vendor_stop to first frame; if we never got a frame,
         * s_cast_lvgl_was_stopped stays 0 and LVGL must not be lv_vendor_start() again.
         */
        s_lvgl_stopped_for_cast = 0;
        if (s_cast_lvgl_was_stopped) {
            s_cast_lvgl_was_stopped = 0;
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

    s_lvgl_stopped_for_cast = 0;

    if (s_dpu_switched_to_cast && display_ui_get_ctx() && display_ui_get_ctx()->dpu_ctlr_handle) {
        avdk_err_t fr;

        s_dpu_switched_to_cast = 0;
        /*
         * Apply LVGL layer (RGB565, decompress off) before flushing the LVGL FB so
         * scanout does not interpret RGB565 through the cast ARGB/decompress path.
         * Flush then repoints DPU at valid LVGL memory (cast pool freed only after
         * post_stop in cast_jpeg_pipeline_turn_off).
         */
        fr = bk_display_pixel_format_set(display_ui_get_ctx()->dpu_ctlr_handle,
            &(bk_display_pixel_format_config_t) {
                .format = display_ui_get_dpu_config()->video.format,
                .decompress = display_ui_get_dpu_config()->video.decompress,
            });
        if (fr != AVDK_ERR_OK) {
            bk_err_t wait_ret = cast_jpeg_pipeline_wait_display_flush(1000);
            LOGW("[cast] restore LVGL pixel format retry after flush wait, r=%d wait=%d\n",
                 (int)fr, (int)wait_ret);
            fr = bk_display_pixel_format_set(display_ui_get_ctx()->dpu_ctlr_handle,
                &(bk_display_pixel_format_config_t) {
                    .format = display_ui_get_dpu_config()->video.format,
                    .decompress = display_ui_get_dpu_config()->video.decompress,
                });
        }
        if (fr != AVDK_ERR_OK) {
            LOGE("[cast] restore LVGL pixel format failed, r=%d\n", (int)fr);
            return BK_FAIL;
        }
        LOGI("DPU layer config + open restored for LVGL\n");
        if (display_ui_get_lvgl_fb(0)) {
            fr = bk_display_flush(display_ui_get_ctx()->dpu_ctlr_handle,
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
    s_cast_lvgl_was_stopped = 0;
    s_lvgl_suspended_for_cast = 1;
    /*
     * Pipeline create/start runs next (cast_jpeg_pipeline_turn_on). LVGL stop and
     * DPU ARGB/decompress apply in display_ui_cast_first_frame_apply on first flush.
     * Disconnect before any frame keeps RGB565 and leaves LVGL running.
     */
    LOGI("[cast] pipeline prep: lvgl stop + dpu cast format deferred to first frame flush\n");
}

static void display_ui_cast_first_frame_apply(void)
{
    uint32_t post_paint_ms = beken_ui_after_cast_ui_painted_delay_ms();

    /*
     * Runs from cast frame_display path; keep section minimal. Hold disp_lock
     * until lv_vendor_stop completes (same ordering as former pre_start).
     */
    lv_vendor_disp_lock();
    beken_ui_before_cast_lvgl_teardown();
    if (post_paint_ms > 0U) {
        LOGI("[cast] post page-switch delay %u ms\n", (unsigned)post_paint_ms);
        rtos_delay_milliseconds((uint32_t)post_paint_ms);
    }
    lv_vendor_stop();
    s_cast_lvgl_was_stopped = 1;
    lv_vendor_disp_unlock();

    lvgl_app_dpu_apply_casting_config();
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
