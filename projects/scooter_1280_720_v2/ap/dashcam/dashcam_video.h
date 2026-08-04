#ifndef __DASHCAM_VIDEO_H__
#define __DASHCAM_VIDEO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "lvgl.h"
#include "common/bk_err.h"

/*
 * dashcam_video: SD-card clip playback video surface.
 *
 * Playback-only: an LVGL canvas fed by decoded frames the player hands in via
 * dashcam_video_on_frame(). The H.264 frame controller (decode core + PP) does
 * decode + down-scale + NV12->RGB565 in one hardware pass, so frames arrive as
 * RGB565 already sized to the canvas and are just triple-buffered + swapped onto
 * the canvas (no software transcode here). The live MIPI camera preview has been
 * removed; the camera->display path now lives in dashcam_assitview.c. The
 * SD-card file list lives in dashcam_ui.c and the decode pipeline in
 * dashcam_player.c; this module only draws the frames.
 */

typedef enum
{
    DASHCAM_VIDEO_FRAME_FORMAT_RGB565 = 0,
    DASHCAM_VIDEO_FRAME_FORMAT_NV12,
} dashcam_video_frame_format_t;

/* Create the playback canvas under `parent`, at the fixed playback size
 * (DASHCAM_PLAYBACK_WIDTH x HEIGHT). */
bk_err_t dashcam_video_start_sink(lv_obj_t *parent);

/* Tear down the canvas and free all buffers. */
void dashcam_video_stop(void);

/* Feed one decoded frame (RGB565 or NV12) from the player to the canvas. */
void dashcam_video_on_frame(const void *frame, uint32_t width, uint32_t height, uint32_t format);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_VIDEO_H__ */
