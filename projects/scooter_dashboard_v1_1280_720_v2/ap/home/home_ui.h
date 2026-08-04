#ifndef __HOME_UI_H__
#define __HOME_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Home page logic, split out of the generated beken_ui.c.
 *
 * Mirrors the ota_ui / dashcam_ui split: the home page's own behavior (speed
 * gauge sweep, hazard double-flash, the music/phone panel fed by the Bluetooth
 * A2DP/HFP callbacks, and the home background bitmap) lives here so that the
 * Designer-generated beken_ui.c only drives navigation between pages.
 *
 *   home_ui_install_bg()           : install the home background bitmap (fast
 *                                    preloaded path, else a one-shot JPEG
 *                                    decode into a PSRAM canvas).
 *   home_ui_enter()                : home page became active; (re)start the
 *                                    speed-gauge / hazard / music timers.
 *   home_ui_leave()                : home page left or freed; stop those timers.
 *   home_ui_unload()               : the home screen object tree is about to be
 *                                    deleted by the page manager. Stop timers and
 *                                    drop the static handles into it (canvases)
 *                                    plus free the buffers LVGL does not own, so
 *                                    nothing dangles and nothing leaks.
 *   home_ui_register_bt_callbacks(): register the A2DP/HFP UI callbacks that
 *                                    feed the music + phone panel.
 */
void home_ui_install_bg(void);
void home_ui_enter(void);
void home_ui_leave(void);
void home_ui_unload(void);
void home_ui_register_bt_callbacks(void);
void phone_key_answer(void);
void phone_key_hangup(void);

#ifdef __cplusplus
}
#endif

#endif /* __HOME_UI_H__ */
