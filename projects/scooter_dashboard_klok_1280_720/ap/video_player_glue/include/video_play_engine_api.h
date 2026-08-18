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
 * Video is always decoded through the H.264 frame decoder as fused RGB565
 * 1280x720 with decoder rotation disabled. The PP-OSD renderer owns the final
 * GPU ROTATE_90 and DPU flush.
 */
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

#ifdef __cplusplus
}
#endif
