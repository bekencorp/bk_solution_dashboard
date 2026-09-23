#ifndef __DASHCAM_VIDEO_H__
#define __DASHCAM_VIDEO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "common/bk_err.h"

/*
 * dashcam_video: SD-card clip playback video surface.
 *
 * Playback-only direct-DPU sink. It tears the LVGL runtime down before playback,
 * submits GPU-scaled/rotated compressed ARGB8888 frames through
 * bk_display_flush(), then rebuilds LVGL when playback stops.
 *
 * The teardown is a full lv_deinit (same as the assist view), not just
 * lv_vendor_stop(): stopping only the LVGL task keeps the 120 KiB partial draw
 * buffer and the software-render worker stacks allocated, which is most of
 * LVGL's HSRAM footprint and is exactly what the H264 decoder and GPU need
 * while a clip plays.
 */

typedef enum
{
    DASHCAM_VIDEO_FRAME_FORMAT_RGB565 = 0,
    DASHCAM_VIDEO_FRAME_FORMAT_NV12,
    DASHCAM_VIDEO_FRAME_FORMAT_ARGB8888,
} dashcam_video_frame_format_t;

typedef void (*dashcam_video_ready_cb_t)(bk_err_t result, void *user_data);

/*
 * UI-side callbacks around the LVGL teardown/rebuild that brackets playback.
 * All three run on the playback worker thread, never on the LVGL task.
 *
 *   before_lvgl_teardown : LVGL is still up (called under the display lock).
 *                          Leave the standby pages and drop their timers. It
 *                          MUST NOT touch playback - dashcam_ui_leave() and
 *                          dashcam_app_detach() call back into this worker and
 *                          would deadlock.
 *   after_lvgl_deinit    : LVGL is gone and its pool has been handed back to
 *                          the system heap, so every cached LVGL handle is
 *                          dangling. Drop them here; no lv_* call is legal.
 *   before_lvgl_restore  : LVGL is still down, about to be rebuilt. Select the
 *                          page the new tree should come up on, so the rebuild
 *                          lands where playback was launched from instead of
 *                          building home first and switching away from it.
 */
typedef struct
{
    void (*before_lvgl_teardown)(void);
    void (*after_lvgl_deinit)(void);
    void (*before_lvgl_restore)(void);
} dashcam_video_lvgl_hooks_t;

/* Register the callbacks above. Pass NULL to clear them. */
void dashcam_video_register_lvgl_hooks(const dashcam_video_lvgl_hooks_t *hooks);

/*
 * True while playback owns the display because it tore the LVGL runtime down,
 * including during the rebuild itself. The UI init path checks this (like
 * dashcam_assitview_is_active()) so a rebuild triggered by playback does not
 * schedule the power-on recording start a second time.
 */
bool dashcam_video_owns_lvgl(void);

/* Run once from the DPU worker after the initial player start succeeds or fails. */
void dashcam_video_set_ready_callback(dashcam_video_ready_cb_t callback,
                                      void *user_data);

/* Start the asynchronous LVGL-to-player/GPU-to-direct-DPU handoff. */
bk_err_t dashcam_video_start_sink(lv_obj_t *parent, const char *path);

/* Switch files on the playback worker while LVGL remains stopped. */
bk_err_t dashcam_video_switch_file(const char *path);

/* Ask the playback worker to close player/GPU and then resume LVGL. */
void dashcam_video_stop(void);

/* Stop and wait until the worker has released player/GPU and restored LVGL. */
bk_err_t dashcam_video_stop_sync(uint32_t timeout_ms);

/* Feed one full-panel decoded frame from the player to the DPU sink. */
void dashcam_video_on_frame(const void *frame, uint32_t width, uint32_t height, uint32_t format);

/* Transfer one decoder frame to the DPU sink; true means ownership transferred. */
bool dashcam_video_submit_owned_frame(void *frame, uint32_t width, uint32_t height, uint32_t format);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_VIDEO_H__ */
