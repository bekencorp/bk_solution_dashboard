#ifndef __DASHCAM_APP_H__
#define __DASHCAM_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "lvgl.h"
#include "common/bk_err.h"

/*
 * Dashcam business orchestrator. Keeps the camera / recorder / player resource
 * state machine out of the generated UI (beken_ui.c, req5 #6).
 *
 * Recording and playback are ORTHOGONAL and can be active at the same time: the
 * H264 encoder (camera->MP4) and the H264 decoder+PP (clip->GPU) are independent
 * hardware blocks. They are therefore tracked by two separate state variables
 * instead of one combined enum, so playing a clip never disturbs the background
 * recording state (and vice versa).
 *
 * Recording is continuous from power-on and runs in the background regardless of
 * the visible page (req5 §9.1). There is no live camera preview; the dashcam
 * page is only used for clip playback, which runs concurrently with recording.
 */

/* Background recording pipeline (camera MP/flexa -> H264 -> segmented MP4). */
typedef enum
{
    DASHCAM_REC_IDLE = 0,   /* recording not started yet (pre-boot / after shutdown) */
    DASHCAM_REC_RECORDING,  /* continuous segmented recording running */
    DASHCAM_REC_STOPPED,    /* recording disabled (dev file cap hit); camera powered down */
} dashcam_rec_state_t;

/* Clip playback surface (recorded MP4 -> H264 decoder+PP -> canvas). */
typedef enum
{
    DASHCAM_PLAY_IDLE = 0,  /* no clip playing */
    DASHCAM_PLAY_PLAYING,   /* a recorded clip is decoding into the canvas */
} dashcam_play_state_t;

/* Start continuous background recording at power-on. Idempotent. */
void dashcam_app_boot_start(void);
/* Full stop of camera/recorder/player/tick (e.g. LVGL teardown for casting). */
void dashcam_app_shutdown(void);

/*
 * User-initiated recording control (CLI / UI), independent of playback:
 *  - record_start: bring recording up from IDLE/STOPPED (e.g. after freeing
 *    space). No-op if already recording. Returns BK_OK once RECORDING.
 *  - record_stop: finalize the current MP4 and power the camera down. Any active
 *    clip playback keeps running (orthogonal state).
 */
bk_err_t dashcam_app_record_start(void);
void dashcam_app_record_stop(void);

/* Assist-view: pause/resume the LVGL segment-rotation tick WITHOUT stopping the
 * background recorder or camera, so recording (MP->H264) continues while the
 * assist view drives the display (MP->GPU) after lv_vendor_stop(). */
void dashcam_app_pause_segment_tick(void);
void dashcam_app_resume_segment_tick(void);

/* Called when the dashcam page becomes active; ensures background capture is
 * running (starts it if it was not). */
void dashcam_app_attach(lv_obj_t *preview_parent);
/* Called when the dashcam page is left/freed; drops the playback surface but
 * keeps the background recording running. */
void dashcam_app_detach(void);

/* Play a recorded clip. Recording keeps running underneath (they coexist). */
bk_err_t dashcam_app_play(const char *path);
/* Stop clip playback. Background recording is left untouched. */
void dashcam_app_stop_playback(void);

/* Independent state accessors (recording and playback can both be active). */
dashcam_rec_state_t dashcam_app_rec_state(void);
bool dashcam_app_is_playing(void);
const char *dashcam_app_status_text(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_APP_H__ */
