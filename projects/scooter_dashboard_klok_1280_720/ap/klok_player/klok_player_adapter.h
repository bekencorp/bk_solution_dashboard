#ifndef __KLOK_PLAYER_ADAPTER_H__
#define __KLOK_PLAYER_ADAPTER_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void klok_player_adapter_init(void);
int klok_player_play_default(void);
int klok_player_play_file(const char *file_path);
int klok_player_replay(void);
int klok_player_pause(void);
int klok_player_resume(void);
int klok_player_pause_toggle(void);
bool klok_player_is_started(void);
bool klok_player_is_paused(void);
int klok_player_stop(void);
int klok_player_next(void);
int klok_player_set_volume(uint8_t volume);
uint8_t klok_player_get_volume(void);
int klok_player_volume_up(void);
int klok_player_volume_down(void);
int klok_player_mute_toggle(void);
bool klok_player_is_muted(void);
int klok_player_audio_track(uint8_t index);
int klok_player_accompany(void);
int klok_player_vocal(void);
bool klok_player_is_accompany(void);

#ifdef __cplusplus
}
#endif

#endif /* __KLOK_PLAYER_ADAPTER_H__ */
