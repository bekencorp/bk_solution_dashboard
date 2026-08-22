#ifndef __DASHCAM_CAMERA_H__
#define __DASHCAM_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "common/bk_err.h"

/*
 * Single owner of the MIPI camera + ISP.
 *
 * There is exactly one camera configuration: ISP MP flexa. Recording adds an
 * MP->H264 bond; assist view adds an independent MP->GPU bond. The SP (live
 * preview) channel is never brought up (dropping its 960x540 NV12 buffers keeps
 * the memory-tight AP heap from starving LVGL draw allocations, req5 §9.1 OOM
 * fix). Encoded H264 frames are consumed by dashcam_recorder.
 *
 * Recording and assist view have separate ownership. If they overlap, closing
 * either owner leaves ISP running for the other. The recording open/close pair
 * controls the H264 bond; the assist pair never creates one.
 */

bk_err_t dashcam_camera_open(void);
void dashcam_camera_close(void);
bk_err_t dashcam_camera_open_for_assist(void);
void dashcam_camera_close_for_assist(void);
bool dashcam_camera_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_CAMERA_H__ */
