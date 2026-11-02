#ifndef __OTA_UI_H__
#define __OTA_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OTA page logic, split out of the generated beken_ui.c (req6 #1).
 *
 * Keeps the OTA progress timer / arc / percent-label driving out of the
 * Designer-generated file, mirroring the dashcam_ui_enter()/leave() split.
 *
 *   ota_ui_enter() : called when the OTA page becomes active; starts the
 *                    progress animation.
 *   ota_ui_leave() : called when the OTA page is left/freed; stops the timer.
 */
void ota_ui_enter(void);
void ota_ui_leave(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_UI_H__ */
