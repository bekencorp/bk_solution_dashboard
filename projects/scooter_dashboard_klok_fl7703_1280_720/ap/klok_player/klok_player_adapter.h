#ifndef __KLOK_PLAYER_ADAPTER_H__
#define __KLOK_PLAYER_ADAPTER_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KLOK_PLAYER_OUTPUT_FLEXA_DIRECT = 0,
    KLOK_PLAYER_OUTPUT_FRAME_PREVIEW,
} klok_player_output_mode_t;

typedef void (*klok_player_finished_cb_t)(const char *file_path, void *user_data);

void klok_player_adapter_init(void);
void klok_player_acquire_ktv_audio_focus(void);
void klok_player_set_finished_callback(klok_player_finished_cb_t callback,
                                       void *user_data);
int klok_player_play_default(void);
int klok_player_play_file(const char *file_path);
void klok_player_set_repeat(bool repeat);
int klok_player_replay(void);
int klok_player_pause(void);
int klok_player_resume(void);
int klok_player_pause_toggle(void);
bool klok_player_is_started(void);
bool klok_player_is_paused(void);
int klok_player_stop(void);
int klok_player_next(void);
int klok_player_previous(void);
int klok_player_set_volume(uint8_t volume);
uint8_t klok_player_get_volume(void);
int klok_player_volume_up(void);
int klok_player_volume_down(void);
int klok_player_mute_toggle(void);
int klok_player_set_muted(bool muted);
bool klok_player_is_muted(void);
int klok_player_audio_track(uint8_t index);
int klok_player_accompany(void);
int klok_player_vocal(void);
bool klok_player_is_accompany(void);
klok_player_output_mode_t klok_player_get_output_mode(void);
bool klok_player_is_switching(void);
/* Returns 0 when prepared, 1 when already in the target mode, -1 on error. */
int klok_player_begin_output_switch(klok_player_output_mode_t target_mode);
int klok_player_complete_output_switch(const char *new_file_path);
void klok_player_cancel_output_switch(void);

#ifdef __cplusplus
}
#endif

#endif /* __KLOK_PLAYER_ADAPTER_H__ */
