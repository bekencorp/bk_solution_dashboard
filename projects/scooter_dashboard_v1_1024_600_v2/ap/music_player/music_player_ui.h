#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called when the music_player page becomes active. Re-applies the runtime CJK
 * TTF font to the labels that can hold arbitrary Chinese (song title/artist and
 * the playlist rows), because the generated page uses ASCII-only pingfang
 * subsets that cannot render real Chinese track metadata. The page is recreated
 * on every entry, so the font must be re-applied each time. */
void music_player_ui_enter(void);

/* Called when leaving the music_player page. */
void music_player_ui_leave(void);

/*
 * The LVGL group holding the navigable widgets (the now-playing control buttons
 * followed by the playlist rows) for key navigation. beken_ui binds the shared
 * KEYPAD indev to it while the music_player page is active: UP/DOWN move inside
 * the focused zone, LEFT/RIGHT switch between the now-playing panel and the
 * playlist, and ENTER activates the focused button / plays the focused track.
 * Created lazily; persists for the app.
 */
lv_group_t *music_player_ui_get_group(void);

/* Physical-key handlers. Each returns true when it consumed the press so the
 * home-menu default behavior (page cycling / return home) is skipped. */
bool music_player_ui_handle_key_double(void);
bool music_player_ui_handle_key_single(void);
bool music_player_ui_handle_key_long(void);

#ifdef __cplusplus
}
#endif
