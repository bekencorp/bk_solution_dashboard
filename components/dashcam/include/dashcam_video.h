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
 * Playback-only direct-DPU sink. It stops the LVGL task before playback, submits
 * GPU-scaled/rotated compressed ARGB8888 frames through bk_display_flush(), then
 * restores the preserved LVGL task when playback stops.
 */

typedef enum
{
    DASHCAM_VIDEO_FRAME_FORMAT_RGB565 = 0,
    DASHCAM_VIDEO_FRAME_FORMAT_NV12,
    DASHCAM_VIDEO_FRAME_FORMAT_ARGB8888,
} dashcam_video_frame_format_t;

typedef void (*dashcam_video_ready_cb_t)(bk_err_t result, void *user_data);

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
