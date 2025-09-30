// Copyright 2025-2026 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __BK_MP3_PLAY_H__
#define __BK_MP3_PLAY_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <components/bk_audio/audio_streams/onboard_speaker_stream.h>
#include <components/bk_audio/audio_decoders/mp3_decoder.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>


typedef enum
{
    MP3_PLAY_STATE_NONE = 0,
    MP3_PLAY_STATE_IDLE,
    MP3_PLAY_STATE_STOPED,
    MP3_PLAY_STATE_PLAYING,
    MP3_PLAY_STATE_PAUSED,
} bk_mp3_play_state_t;

typedef enum
{
    MP3_PLAY_EVENT_FINISH = 0,

} bk_mp3_play_event_t;

typedef struct mp3_play *bk_mp3_play_handle_t;
typedef int (*mp3_play_event_handle_cb)(int, void *, void *);

typedef struct
{
    mp3_decoder_cfg_t mp3_dec_cfg;
    onboard_speaker_stream_cfg_t ob_spk_cfg;
    uint32_t pool_size;

    mp3_play_event_handle_cb event_handle;
    void *args;
} bk_mp3_play_cfg_t;


#define DEFAULT_MP3_PLAY_CONFIG() {                             \
    .mp3_dec_cfg = {                                            \
        .main_buff_size     = MP3_DECODER_MAIN_BUFF_SIZE,       \
        .out_pcm_buff_size  = MP3_DECODER_OUT_PCM_BUFF_SIZE,    \
        .out_block_size     = MP3_DECODER_OUT_PCM_BUFF_SIZE,    \
        .out_block_num      = 1,                                \
        .task_stack         = MP3_DECODER_TASK_STACK,           \
        .task_core          = MP3_DECODER_TASK_CORE,            \
        .task_prio          = MP3_DECODER_TASK_PRIO,            \
    },                                                          \
    .ob_spk_cfg = {                                             \
        .chl_num = 1,                                           \
        .sample_rate = 8000,                                    \
        .dig_gain = 0x2d,                                       \
        .ana_gain = 0x07,                                       \
        .work_mode = AUD_DAC_WORK_MODE_DIFFEN,                  \
        .bits = 16,                                             \
        .clk_src = AUD_CLK_XTAL,                                \
        .multi_in_port_num = 0,                                 \
        .multi_out_port_num = 0,                                \
        .frame_size = 320,                                      \
        .pool_length = 0,                                       \
        .pool_play_thold = 0,                                   \
        .pool_pause_thold = 0,                                  \
        .pa_ctrl_en = false,                                    \
        .pa_ctrl_gpio = 0,                                      \
        .pa_on_level = 0,                                       \
        .pa_on_delay = 0,                                       \
        .pa_off_delay = 0,                                      \
        .task_stack = ONBOARD_SPEAKER_STREAM_TASK_STACK,        \
        .task_core = ONBOARD_SPEAKER_STREAM_TASK_CORE,          \
        .task_prio = ONBOARD_SPEAKER_STREAM_TASK_PRIO,          \
    },                                                          \
    .pool_size = 4096,                                          \
    .event_handle = NULL,                                       \
    .args = NULL,                                               \
}

/**
 * @brief     Create mp3 player with config
 *
 * This API create mp3 play handle according to config.
 * This API should be called before other api.
 *
 * @param[in] config    Mp3 play config
 *
 * @return
 *    - Not NULL: success
 *    - NULL: failed
 */
bk_mp3_play_handle_t bk_mp3_play_create(bk_mp3_play_cfg_t *config);

/**
 * @brief      Destroy mp3 play
 *
 * This API Destroy mp3 play according to mp3 play handle.
 *
 *
 * @param[in] mp3_play  The mp3 play handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t bk_mp3_play_destroy(bk_mp3_play_handle_t mp3_play);

/**
 * @brief      Open mp3 play
 *
 * This API open mp3 play and start decode speaker data to play.
 *
 *
 * @param[in] mp3_play  The mp3 play handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t bk_mp3_play_start(bk_mp3_play_handle_t mp3_play);

/**
 * @brief      Close mp3 play
 *
 * This API close mp3 play
 *
 *
 * @param[in] mp3_play  The mp3 play handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t bk_mp3_play_stop(bk_mp3_play_handle_t mp3_play);

/**
 * @brief      Write mp3 data to mp3 play
 *
 * This API write mp3 data to decoder pool.
 * If memory in pool is not enough, wait until the pool has enough memory.
 *
 *
 * @param[in] mp3_play      The mp3 play handle
 * @param[in] buffer    The mp3 data buffer
 * @param[in] len       The length (byte) of mp3 data
 *
 * @return
 *    - len: length of the written data
 *    - Others: failed
 */
bk_err_t bk_mp3_play_write_data(bk_mp3_play_handle_t mp3_play, char *buffer, uint32_t len);

/**
 * @brief      Notify the MP3 player that the data writing is completed.
 *
 * This API notifies the MP3 player that the MP3 data writing is finished.
 *
 *
 * @param[in] mp3_play      The mp3 play handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t bk_mp3_play_write_data_done(bk_mp3_play_handle_t mp3_play);

#ifdef __cplusplus
}
#endif
#endif /* __MP3_PLAY_H__ */

