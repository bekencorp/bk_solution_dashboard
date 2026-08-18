#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_player_device.h"
#include "components/bk_video_player/bk_video_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    audio_player_device_handle_t audio_player_handle;
    uint8_t audio_volume;
    bool audio_muted;
} video_play_user_ctx_t;

typedef enum {
    VIDEO_PLAY_ROTATE_NONE = 0,
    VIDEO_PLAY_ROTATE_90,
    VIDEO_PLAY_ROTATE_270,
} video_play_rotate_mode_t;

avdk_err_t video_play_audio_buffer_alloc_cb(void *user_data, video_player_buffer_t *buffer);
void video_play_audio_buffer_free_cb(void *user_data, video_player_buffer_t *buffer);

avdk_err_t video_play_video_packet_buffer_alloc_cb(void *user_data,
                                                    video_player_buffer_t *buffer);
void video_play_video_packet_buffer_free_cb(void *user_data,
                                             video_player_buffer_t *buffer);

avdk_err_t video_play_video_frame_buffer_alloc_cb(void *user_data,
                                                   video_player_buffer_t *buffer);
void video_play_video_frame_buffer_free_cb(void *user_data,
                                            video_player_buffer_t *buffer);

void video_play_video_decode_complete_cb(void *user_data,
                                         const video_player_video_frame_meta_t *meta,
                                         video_player_buffer_t *buffer);
void video_play_audio_decode_complete_cb(void *user_data,
                                         const video_player_audio_packet_meta_t *meta,
                                         video_player_buffer_t *buffer);

avdk_err_t video_play_display_worker_init(void);
void video_play_display_worker_deinit(void);
void video_play_display_process_one(void);
void video_play_display_prepare_restart(void);
void video_play_display_resume_handoff(void);

#ifdef __cplusplus
}
#endif
