#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "components/avdk_utils/avdk_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Klok product playback API.
 *
 * KLOK_VIDEO_FLEXA_DIRECT_MODE enables runtime switching between Flexa direct
 * display and RGB565 frame preview. When disabled, the fixed H.264 frame
 * decoder/PP-OSD path remains unchanged.
 */
typedef enum {
    VIDEO_PLAY_OUTPUT_FLEXA_DIRECT = 0,
    VIDEO_PLAY_OUTPUT_FRAME_PREVIEW,
} video_play_output_mode_t;

typedef void (*video_play_finished_cb_t)(const char *file_path, void *user_data);

void video_play_engine_api_set_finished_callback(video_play_finished_cb_t callback,
                                                 void *user_data);
avdk_err_t video_play_engine_api_prepare_storage(void);
avdk_err_t video_play_engine_api_start(const char *file_path);
avdk_err_t video_play_engine_api_stop(void);
avdk_err_t video_play_engine_api_set_pause(bool pause);
avdk_err_t video_play_engine_api_seek(uint64_t time_ms);
avdk_err_t video_play_engine_api_set_volume(uint8_t volume);
avdk_err_t video_play_engine_api_volume_up(uint8_t step);
avdk_err_t video_play_engine_api_volume_down(uint8_t step);
avdk_err_t video_play_engine_api_set_mute(bool mute);
avdk_err_t video_play_engine_api_select_audio_track(uint8_t index);
avdk_err_t video_play_engine_api_reassert_audio_format(void);
video_play_output_mode_t video_play_engine_api_get_output_mode(void);
bool video_play_engine_api_is_switching(void);
avdk_err_t video_play_engine_api_begin_output_switch(video_play_output_mode_t target_mode);
avdk_err_t video_play_engine_api_complete_output_switch(const char *file_path,
                                                        bool remain_paused);
void video_play_engine_api_cancel_output_switch(void);

#ifdef __cplusplus
}
#endif
