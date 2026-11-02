#ifndef __PHONE_BOOK_UI_H__
#define __PHONE_BOOK_UI_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Phone-book page logic, split out of the generated beken_ui.c (mirrors the
 * dashcam_ui / ota_ui split). The contacts list is filled from the PBAP contact
 * cache (pbap_contacts) pulled from the connected phone.
 *
 *   phone_book_ui_enter() : page became active; populate the contacts list from
 *                           the cache and subscribe to cache updates.
 *   phone_book_ui_leave() : page left/freed; unsubscribe from cache updates.
 */
void phone_book_ui_enter(void);
void phone_book_ui_leave(void);

/*
 * Physical-key handlers (mirror dashcam_ui_handle_key_*). Each returns true when
 * it consumed the key, so the caller (beken_ui home-menu) skips its own handling.
 *
 *   double : cycle focus  whole-page -> contacts list -> recents list -> page
 *   single : when a list is focused, move the selection to the next row (wraps),
 *            scrolling that row into view; returns false at page focus so the
 *            home-menu single-press (return home) still works.
 */
bool phone_book_ui_handle_key_double(void);
bool phone_book_ui_handle_key_single(void);

/*
 * long : when a list is focused, dial the currently-selected contact / recent
 *        (HFP outgoing call); returns false at page focus so the home-menu
 *        long-press (return home) still works.
 */
bool phone_book_ui_handle_key_long(void);

#ifdef __cplusplus
}
#endif

#endif /* __PHONE_BOOK_UI_H__ */
