#ifndef __DASHCAM_RECORDER_H__
#define __DASHCAM_RECORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "common/bk_err.h"

/*
 * MP4/H264 muxer. The camera + H264 encoder must already be running (opened via
 * dashcam_camera_open(), owned by dashcam_camera); the recorder only pulls
 * encoded frames from the doorbell encoded-data queue and writes one MP4 file.
 *
 * Continuous/segmented recording is driven by dashcam_app, which calls
 * start(path) / stop() once per segment. The underlying recorder object (and its
 * worker thread) is created on the first start and kept alive across segments;
 * stop() only finalizes the current MP4. dashcam_recorder_destroy() performs the
 * full teardown (worker thread + semaphores) and must only be called when no
 * segment rotation is in flight (e.g. from dashcam_app_shutdown()).
 */
bk_err_t dashcam_recorder_start(const char *path);
bk_err_t dashcam_recorder_stop(void);
void dashcam_recorder_destroy(void);
bool dashcam_recorder_is_running(void);
const char *dashcam_recorder_current_path(void);
uint32_t dashcam_recorder_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_RECORDER_H__ */
