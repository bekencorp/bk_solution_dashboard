#ifndef __DASHCAM_UI_H__
#define __DASHCAM_UI_H__

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LVGL glue for the dashcam page: populates the records list from the SD card,
 * binds list-item taps to playback, drives the live/playback transition, and
 * handles the physical-key list navigation. Keeps the dashcam UI logic out of
 * the generated beken_ui.c (req5 #6, req6).
 */
/* Start continuous background recording at power-on (no preview yet). */
void dashcam_ui_boot_start(void);
/* Full stop of dashcam capture (e.g. LVGL teardown before a display cast). */
void dashcam_ui_shutdown(void);

/* Assist-view enter/leave: suspend the dashcam LVGL page (stop UI timers, pause
 * the segment tick) but KEEP the background recorder + camera running, so the
 * assist view shows the live MP output via a GPU bond while recording
 * continues. resume re-arms the segment tick. */
void dashcam_ui_suspend_keep_recording(void);
void dashcam_ui_resume_keep_recording(void);

void dashcam_ui_enter(void);
void dashcam_ui_leave(void);

/*
 * Physical-key navigation for the dashcam page (req6 #3). Driven by the GPIO_50
 * key via beken_ui pet_page_* hooks. Each returns true if the dashcam page
 * consumed the event (so the caller skips its default home-menu behavior).
 *
 *   double : toggle "list selection" focus mode (in <-> out of the list).
 *   single : when focused, move the selection down one item (wraps around).
 *   long   : when focused, play the currently selected clip.
 */
bool dashcam_ui_handle_key_double(void);
bool dashcam_ui_handle_key_single(void);
bool dashcam_ui_handle_key_long(void);
bool dashcam_ui_handle_key_home(void);

/*
 * The LVGL group holding the records-list items. beken_ui binds the shared
 * KEYPAD indev to it while the dashcam page is active. UP/DOWN and PREV/NEXT
 * move the focused record; ENTER plays it. The group persists for the app.
 */
lv_group_t *dashcam_ui_get_group(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_UI_H__ */
