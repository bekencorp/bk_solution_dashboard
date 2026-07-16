#ifndef __DASHCAM_APP_H__
#define __DASHCAM_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "lvgl.h"
#include "common/bk_err.h"

/*
 * Dashcam business orchestrator. Keeps the camera / recorder / player / preview
 * resource state machine out of the generated UI (beken_ui.c, req5 #6).
 *
 * Recording is continuous from power-on and runs in the background regardless of
 * the visible page (req5 §9.1). The live preview is an overlay that is attached
 * only while the dashcam page is showing; attaching/detaching the preview does
 * not interrupt recording.
 *
 *   LIVE_REC  : continuous segmented recording (MP/flexa->H264). The live view
 *               (SP) is shown when the page is attached.
 *   LIVE_ONLY : recording disabled (dev file cap hit). Camera is up only while
 *               the page shows the live view (MP read), otherwise powered down.
 *   PLAYBACK  : recording paused; a recorded clip is decoded into the canvas.
 */
typedef enum
{
    DASHCAM_APP_IDLE = 0,
    DASHCAM_APP_LIVE_REC,
    DASHCAM_APP_LIVE_ONLY,
    DASHCAM_APP_PLAYBACK,
} dashcam_app_state_t;

/* Start continuous background recording at power-on. Idempotent. */
void dashcam_app_boot_start(void);
/* Full stop of camera/recorder/player/tick (e.g. LVGL teardown for casting). */
void dashcam_app_shutdown(void);

/* Called when the dashcam page becomes active; attaches the live preview to the
 * already-running capture (starts capture if it was not running). */
void dashcam_app_attach(lv_obj_t *preview_parent);
/* Called when the dashcam page is left/freed; drops the preview but keeps the
 * background recording running. */
void dashcam_app_detach(void);

/* Play a recorded clip (switches LIVE -> PLAYBACK). */
bk_err_t dashcam_app_play(const char *path);
/* Return from playback to the live preview/recording state. */
void dashcam_app_resume_live(void);

dashcam_app_state_t dashcam_app_get_state(void);
const char *dashcam_app_status_text(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_APP_H__ */
