#ifndef __DASHCAM_CAMERA_H__
#define __DASHCAM_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "common/bk_err.h"

/*
 * Single owner of the MIPI camera + ISP. Resolves the "preview vs record"
 * contention on the ISP MP channel:
 *
 *   PREVIEW     : ISP MP (no flexa) -> caller reads NV12 frames from MP.
 *   RECORD      : ISP MP (flexa)    -> H264 encoder (record);
 *                 ISP SP            -> caller reads NV12 frames from SP (live view).
 *   RECORD_ONLY : ISP MP (flexa)    -> H264 encoder (record); SP channel NOT
 *                 brought up. Used for headless/background recording (no viewer,
 *                 e.g. power-on continuous recording while the home page is
 *                 shown). Dropping the SP channel frees its 960x540 NV12 buffers,
 *                 which on this memory-tight AP heap is what otherwise starves
 *                 LVGL draw allocations and asserts (req5 §9.1 OOM fix).
 *
 * In RECORD mode the same sensor frame feeds both the encoder (MP/flexa) and the
 * preview (SP), so the dashcam can record and show the live stream at once
 * (req5 #3). The encoded H264 frames are delivered through the doorbell encoded
 * data queue, consumed by dashcam_recorder.
 */
typedef enum
{
    DASHCAM_CAMERA_MODE_OFF = 0,
    DASHCAM_CAMERA_MODE_PREVIEW,
    DASHCAM_CAMERA_MODE_RECORD,
    DASHCAM_CAMERA_MODE_RECORD_ONLY,
} dashcam_camera_mode_t;

bk_err_t dashcam_camera_open(dashcam_camera_mode_t mode);
void dashcam_camera_close(void);
dashcam_camera_mode_t dashcam_camera_get_mode(void);

/* Read one NV12 preview frame from the channel matching the current mode. */
int dashcam_camera_read_preview(uint8_t *frame, uint32_t size, uint32_t timeout_ms);

uint32_t dashcam_camera_preview_width(void);
uint32_t dashcam_camera_preview_height(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_CAMERA_H__ */
