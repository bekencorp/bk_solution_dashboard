#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void music_player_ui_enter(void);
void music_player_ui_leave(void);
void music_player_ui_leave_to_home_async(void);
void music_player_ui_select_bluetooth(void);
void music_player_ui_select_udisk(void);
void music_player_ui_local_prev(void);
void music_player_ui_local_toggle(void);
void music_player_ui_local_next(void);
void music_player_ui_bt_prev(void);
void music_player_ui_bt_toggle(void);
void music_player_ui_bt_next(void);
void music_player_ui_play_row(uint32_t index);
bool music_player_ui_is_active(void);
void music_player_ui_key_prev(void);
void music_player_ui_key_toggle(void);
void music_player_ui_key_next(void);

/* Called once after Bluetooth/A2DP initialization. */
void music_player_ui_register_bt_callback(void);

#ifdef __cplusplus
}
#endif
