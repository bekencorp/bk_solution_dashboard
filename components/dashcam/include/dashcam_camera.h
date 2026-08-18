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
 * There is exactly one camera configuration: ISP MP (flexa) -> H264 encoder.
 * The SP (live preview) channel is never brought up (dropping its 960x540 NV12
 * buffers keeps the memory-tight AP heap from starving LVGL draw allocations,
 * req5 §9.1 OOM fix). The encoded H264 frames are delivered through the
 * doorbell encoded data queue and consumed by dashcam_recorder.
 *
 * The camera is meant to stay open continuously: the recorder keeps it up, and
 * the assist view reuses the same MP flexa output by adding a second MP->GPU
 * bond on top. Callers just ensure it is open; open() is idempotent and simply
 * returns BK_OK when the camera is already running.
 */

bk_err_t dashcam_camera_open(void);
void dashcam_camera_close(void);
bool dashcam_camera_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_CAMERA_H__ */
