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

#ifndef __BLUE_AUDIO_PLAYER_SERVICE_H__
#define __BLUE_AUDIO_PLAYER_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <components/bk_audio/audio_streams/onboard_speaker_stream.h>
#include <components/bk_audio/audio_streams/uac_speaker_stream.h>
#include <components/bk_audio/audio_streams/i2s_stream.h>
#include <components/bk_audio/audio_streams/raw_stream.h>
#include <components/bk_audio/audio_decoders/sbc_decoder.h>
#include <components/bk_audio/audio_decoders/aac_decoder.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_algorithms/mix_algorithm.h>
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
#include "components/bk_audio/audio_algorithms/eq_algorithm.h"
#endif

/**
 * @brief Decoder type enum
 */
typedef enum
{
    BLUE_AUDIO_DECODER_TYPE_SBC = 0,
    BLUE_AUDIO_DECODER_TYPE_AAC,
    BLUE_AUDIO_DECODER_TYPE_LC3,  /* Reserved for future support */
    BLUE_AUDIO_DECODER_TYPE_PCM,  /* PCM format, no decoding needed */
} blue_audio_decoder_type_t;

/**
 * @brief Speaker type enum
 */
typedef enum
{
    BLUE_AUDIO_SPEAKER_TYPE_ONBOARD = 0,
    BLUE_AUDIO_SPEAKER_TYPE_UAC,
    BLUE_AUDIO_SPEAKER_TYPE_I2S,
} blue_audio_speaker_type_t;

/**
 * @brief Player state enum
 */
typedef enum
{
    BLUE_AUDIO_PLAYER_STATE_NONE = 0,
    BLUE_AUDIO_PLAYER_STATE_IDLE,
    BLUE_AUDIO_PLAYER_STATE_STOPPED,
    BLUE_AUDIO_PLAYER_STATE_PLAYING,
    BLUE_AUDIO_PLAYER_STATE_PAUSED,
} blue_audio_player_state_t;

/**
 * @brief Player event enum
 */
typedef enum
{
    BLUE_AUDIO_PLAYER_EVENT_FINISH = 0,
    BLUE_AUDIO_PLAYER_EVENT_MUSIC_INFO,
    BLUE_AUDIO_PLAYER_EVENT_ERROR,
} blue_audio_player_event_t;

/**
 * @brief Player handle type
 */
typedef struct blue_audio_player *blue_audio_player_handle_t;

/**
 * @brief Player event callback function type
 */
typedef int (*blue_audio_player_event_handle_cb)(int, void *, void *);

/**
 * @brief Player configuration structure
 */
typedef struct
{
    blue_audio_decoder_type_t decoder_type;      /*!< Decoder type */
    blue_audio_speaker_type_t speaker_type;      /*!< Speaker type */
    raw_stream_cfg_t raw_strm_cfg;             /*!< Raw stream configuration */
    uint32_t task_stack;                         /*!< Task stack size */
    int task_core;                               /*!< Task running core (0 or 1) */
    int task_prio;                               /*!< Task priority */
    bool mix_en;                                 /*!< Mix algorithm enable flag */
    uint32_t play_threshold;                     /*!< Play threshold (number of frames) */

    union {
        sbc_decoder_cfg_t sbc_dec_cfg;           /*!< SBC decoder configuration */
        aac_decoder_cfg_t aac_dec_cfg;           /*!< AAC decoder configuration */
        /* Reserved for LC3 decoder configuration */
    } decoder_cfg;

    union {
        onboard_speaker_stream_cfg_t ob_spk_cfg; /*!< Onboard speaker configuration */
        uac_speaker_stream_cfg_t uac_spk_cfg;    /*!< UAC speaker configuration */
        i2s_stream_cfg_t i2s_spk_cfg;            /*!< I2S speaker configuration */
    } speaker_cfg;

    mix_algorithm_cfg_t mix_alg_cfg;              /*!< Mix algorithm configuration */

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    bool eq_en;                                          /*!< Enable EQ algorithm */
    union {
        eq_algorithm_cfg_t eq_alg_cfg;                  /*!< EQ algorithm configuration */
    } eq_cfg;                                           /*!< EQ configuration union */
#endif

    blue_audio_player_event_handle_cb event_handle; /*!< Event callback function */
    void *args;                                    /*!< User data for callback */
} blue_audio_player_cfg_t;

#define BLUE_AUDIO_PLAYER_DEFAULT_TASK_STACK      (4 * 1024)
#define BLUE_AUDIO_PLAYER_DEFAULT_TASK_CORE       (1)
#define BLUE_AUDIO_PLAYER_DEFAULT_TASK_PRIO       (BEKEN_DEFAULT_WORKER_PRIORITY - 1)
#define BLUE_AUDIO_PLAYER_DEFAULT_FRAME_SIZE      (512)
#define BLUE_AUDIO_PLAYER_DEFAULT_FRAME_NUM       (25)
#define BLUE_AUDIO_PLAYER_DEFAULT_PLAY_THRESHOLD  (20)

/**
 * @brief Default SBC decoder configuration
 */
#define BLUE_AUDIO_PLAYER_DEFAULT_SBC_DEC_CONFIG() {    \
    .buf_sz             = SBC_DECODER_BUFFER_SIZE,      \
    .out_block_size     = 3528,                         \
    .out_block_num      = SBC_DECODER_OUT_BLOCK_NUM,    \
    .task_stack         = SBC_DECODER_TASK_STACK,       \
    .task_core          = SBC_DECODER_TASK_CORE,        \
    .task_prio          = SBC_DECODER_TASK_PRIO,        \
}

/**
 * @brief Default AAC decoder configuration
 */
#define BLUE_AUDIO_PLAYER_DEFAULT_AAC_DEC_CONFIG() {        \
    .main_buff_size     = AAC_DECODER_MAIN_BUFF_SIZE,       \
    .out_pcm_buff_size  = AAC_DECODER_OUT_PCM_BUFF_SIZE,    \
    .out_block_size     = AAC_DECODER_OUT_PCM_BUFF_SIZE,    \
    .out_block_num      = 2,                                \
    .task_stack         = AAC_DECODER_TASK_STACK,           \
    .task_core          = AAC_DECODER_TASK_CORE,            \
    .task_prio          = AAC_DECODER_TASK_PRIO,            \
}

/**
 * @brief Default onboard speaker configuration
 */
#define BLUE_AUDIO_PLAYER_DEFAULT_ONBOARD_SPK_CONFIG() {    \
    .chl_num = 1,                                           \
    .sample_rate = 44100,                                   \
    .dig_gain = 0x2d,                                       \
    .ana_gain = 0x00,                                       \
    .work_mode = AUD_DAC_WORK_MODE_DIFFEN,                  \
    .bits = 16,                                             \
    .clk_src = AUD_CLK_XTAL,                                \
    .multi_in_port_num = 0,                                 \
    .multi_out_port_num = 0,                                \
    .frame_size = 3528,                                     \
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
}

#define BLUE_AUDIO_PLAYER_DEFAULT_MIX_ALGORITHM_CONFIG() {  \
    .buf_sz             = 1280,                             \
    .out_block_size     = 3528,                             \
    .out_block_num      = 2,                                \
    .task_stack         = MIX_ALGORITHM_TASK_STACK,         \
    .task_core          = MIX_ALGORITHM_TASK_CORE,          \
    .task_prio          = MIX_ALGORITHM_TASK_PRIO,          \
    .input_channel_num  = 2,                                \
}

/**
 * @brief Default UAC speaker configuration
 */
#define BLUE_AUDIO_PLAYER_DEFAULT_UAC_SPK_CONFIG() {        \
    .port_index = USB_HUB_PORT_1,                           \
    .format = AUDIO_FORMAT_PCM,                             \
    .chl_num = 1,                                           \
    .bits = 16,                                             \
    .samp_rate = 44100,                                     \
    .frame_size = 3528,                                     \
    .volume = 5,                                            \
    .auto_connect = true,                                   \
    .multi_out_port_num = 0,                                \
    .pool_size = 0,                                         \
    .pool_play_thold = 0,                                   \
    .pool_pause_thold = 0,                                  \
    .task_stack = UAC_SPEAKER_STREAM_TASK_STACK,            \
    .task_core = UAC_SPEAKER_STREAM_TASK_CORE,              \
    .task_prio = UAC_SPEAKER_STREAM_TASK_PRIO,              \
}

/**
 * @brief Default I2S speaker configuration
 */
#define BLUE_AUDIO_PLAYER_DEFAULT_I2S_SPK_CONFIG() {         \
    .gpio_group = I2S_STREAM_GPIO_GROUP,                     \
    .role = I2S_STREAM_ROLE,                                 \
    .work_mode = I2S_STREAM_WORK_MODE,                       \
    .lrck_invert = I2S_LRCK_INVERT_DISABLE,                  \
    .sck_invert = I2S_SCK_INVERT_DISABLE,                    \
    .lsb_first_en = I2S_LSB_FIRST_DISABLE,                   \
    .sync_length = 0,                                        \
    .data_length = 16,                                       \
    .pcm_dlength = 0,                                        \
    .store_mode = I2S_LRCOM_STORE_16R16L,                    \
    .samp_rate = I2S_SAMP_RATE_44100,                        \
    .pcm_chl_num = 2,                                        \
    .channel_id = I2S_STREAM_CHANNEL_ID,                     \
    .type = AUDIO_STREAM_WRITER,                             \
    .buff_size = 3528,                                       \
    .out_block_size = 3528,                                  \
    .out_block_num = I2S_STREAM_BLOCK_NUM,                   \
    .task_stack = I2S_STREAM_TASK_STACK,                     \
    .task_core = I2S_STREAM_TASK_CORE,                       \
    .task_prio = I2S_STREAM_TASK_PRIO,                       \
    .multi_in_port_num = 0,                                  \
    .multi_out_port_num = 0,                                 \
    .manual_config_gpio_en = 0,                              \
}

/**
 * @brief Default player configuration
 */
#define DEFAULT_BLUE_AUDIO_PLAYER_SBC_ONBOARD_SPK_CONFIG() {            \
    .decoder_type = BLUE_AUDIO_DECODER_TYPE_SBC,                        \
    .speaker_type = BLUE_AUDIO_SPEAKER_TYPE_ONBOARD,                    \
    .raw_strm_cfg = {                                                   \
        .type = AUDIO_STREAM_WRITER,                                    \
        .out_block_size = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_SIZE,         \
        .out_block_num = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_NUM,           \
        .output_port_type = PORT_TYPE_FB                                \
    },                                                                  \
    .task_stack = BLUE_AUDIO_PLAYER_DEFAULT_TASK_STACK,                 \
    .task_core = BLUE_AUDIO_PLAYER_DEFAULT_TASK_CORE,                   \
    .task_prio = BLUE_AUDIO_PLAYER_DEFAULT_TASK_PRIO,                   \
    .mix_en = true,                                                     \
    .play_threshold = BLUE_AUDIO_PLAYER_DEFAULT_PLAY_THRESHOLD,         \
    .decoder_cfg = {                                                    \
        .sbc_dec_cfg = BLUE_AUDIO_PLAYER_DEFAULT_SBC_DEC_CONFIG()       \
    },                                                                  \
    .speaker_cfg = {                                                    \
        .ob_spk_cfg = BLUE_AUDIO_PLAYER_DEFAULT_ONBOARD_SPK_CONFIG()    \
    },                                                                  \
    .mix_alg_cfg = BLUE_AUDIO_PLAYER_DEFAULT_MIX_ALGORITHM_CONFIG(),    \
    .event_handle = NULL,                                               \
    .args = NULL,                                                       \
}

/**
 * @brief Default SBC player with I2S speaker configuration
 */
#define DEFAULT_BLUE_AUDIO_PLAYER_SBC_I2S_SPK_CONFIG() {                \
    .decoder_type = BLUE_AUDIO_DECODER_TYPE_SBC,                        \
    .speaker_type = BLUE_AUDIO_SPEAKER_TYPE_I2S,                        \
    .raw_strm_cfg = {                                                   \
        .type = AUDIO_STREAM_WRITER,                                    \
        .out_block_size = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_SIZE,         \
        .out_block_num = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_NUM,           \
        .output_port_type = PORT_TYPE_FB                                \
    },                                                                  \
    .task_stack = BLUE_AUDIO_PLAYER_DEFAULT_TASK_STACK,                 \
    .task_core = BLUE_AUDIO_PLAYER_DEFAULT_TASK_CORE,                   \
    .task_prio = BLUE_AUDIO_PLAYER_DEFAULT_TASK_PRIO,                   \
    .mix_en = false,                                                    \
    .play_threshold = BLUE_AUDIO_PLAYER_DEFAULT_PLAY_THRESHOLD,         \
    .decoder_cfg = {                                                    \
        .sbc_dec_cfg = BLUE_AUDIO_PLAYER_DEFAULT_SBC_DEC_CONFIG()       \
    },                                                                  \
    .speaker_cfg = {                                                    \
        .i2s_spk_cfg = BLUE_AUDIO_PLAYER_DEFAULT_I2S_SPK_CONFIG()       \
    },                                                                  \
    .mix_alg_cfg = BLUE_AUDIO_PLAYER_DEFAULT_MIX_ALGORITHM_CONFIG(),    \
    .event_handle = NULL,                                               \
    .args = NULL,                                                       \
}

/**
 * @brief Default PCM player configuration
 */
#define DEFAULT_BLUE_AUDIO_PLAYER_PCM_ONBOARD_SPK_CONFIG() {            \
    .decoder_type = BLUE_AUDIO_DECODER_TYPE_PCM,                        \
    .speaker_type = BLUE_AUDIO_SPEAKER_TYPE_ONBOARD,                    \
    .raw_strm_cfg = {                                                   \
        .type = AUDIO_STREAM_WRITER,                                    \
        .out_block_size = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_SIZE,         \
        .out_block_num = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_NUM,           \
        .output_port_type = PORT_TYPE_RB                                \
    },                                                                  \
    .task_stack = BLUE_AUDIO_PLAYER_DEFAULT_TASK_STACK,                 \
    .task_core = BLUE_AUDIO_PLAYER_DEFAULT_TASK_CORE,                   \
    .task_prio = BLUE_AUDIO_PLAYER_DEFAULT_TASK_PRIO,                   \
    .mix_en = false,                                                     \
    .play_threshold = BLUE_AUDIO_PLAYER_DEFAULT_PLAY_THRESHOLD,         \
    .speaker_cfg = {                                                    \
        .ob_spk_cfg =                                               \
        {                                                           \
            .chl_num = 1,                                           \
            .sample_rate = 8000,                                    \
            .dig_gain = 0x2d,                                       \
            .ana_gain = 0x07,                                       \
            .work_mode = AUD_DAC_WORK_MODE_DIFFEN,                  \
            .bits = 16,                                             \
            .clk_src = AUD_CLK_XTAL,                                \
            .multi_in_port_num = 0,                                 \
            .multi_out_port_num = 0,                                \
            .frame_size = 640,                                      \
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
    },                                                                  \
    .mix_alg_cfg = BLUE_AUDIO_PLAYER_DEFAULT_MIX_ALGORITHM_CONFIG(),    \
    .event_handle = NULL,                                               \
    .args = NULL,                                                       \
}

/**
 * @brief Default PCM player with I2S speaker configuration
 */
#define DEFAULT_BLUE_AUDIO_PLAYER_PCM_I2S_SPK_CONFIG() {                \
    .decoder_type = BLUE_AUDIO_DECODER_TYPE_PCM,                        \
    .speaker_type = BLUE_AUDIO_SPEAKER_TYPE_I2S,                        \
    .raw_strm_cfg = {                                                   \
        .type = AUDIO_STREAM_WRITER,                                    \
        .out_block_size = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_SIZE,         \
        .out_block_num = BLUE_AUDIO_PLAYER_DEFAULT_FRAME_NUM,           \
        .output_port_type = PORT_TYPE_RB                                \
    },                                                                  \
    .task_stack = BLUE_AUDIO_PLAYER_DEFAULT_TASK_STACK,                 \
    .task_core = BLUE_AUDIO_PLAYER_DEFAULT_TASK_CORE,                   \
    .task_prio = BLUE_AUDIO_PLAYER_DEFAULT_TASK_PRIO,                   \
    .mix_en = false,                                                    \
    .play_threshold = BLUE_AUDIO_PLAYER_DEFAULT_PLAY_THRESHOLD,         \
    .speaker_cfg = {                                                    \
        .i2s_spk_cfg = {                                                \
            .gpio_group = I2S_STREAM_GPIO_GROUP,                        \
            .role = I2S_STREAM_ROLE,                                    \
            .work_mode = I2S_STREAM_WORK_MODE,                          \
            .lrck_invert = I2S_LRCK_INVERT_DISABLE,                     \
            .sck_invert = I2S_SCK_INVERT_DISABLE,                       \
            .lsb_first_en = I2S_LSB_FIRST_DISABLE,                      \
            .sync_length = 0,                                           \
            .data_length = 16,                                          \
            .pcm_dlength = 0,                                           \
            .store_mode = I2S_LRCOM_STORE_16R16L,                       \
            .samp_rate = I2S_SAMP_RATE_8000,                            \
            .pcm_chl_num = 1,                                           \
            .channel_id = I2S_STREAM_CHANNEL_ID,                        \
            .type = AUDIO_STREAM_WRITER,                                \
            .buff_size = 640,                                           \
            .out_block_size = 640,                                      \
            .out_block_num = I2S_STREAM_BLOCK_NUM,                      \
            .task_stack = I2S_STREAM_TASK_STACK,                        \
            .task_core = I2S_STREAM_TASK_CORE,                          \
            .task_prio = I2S_STREAM_TASK_PRIO,                          \
            .multi_in_port_num = 0,                                     \
            .multi_out_port_num = 0,                                    \
            .manual_config_gpio_en = 0,                                 \
        },                                                              \
    },                                                                  \
    .mix_alg_cfg = BLUE_AUDIO_PLAYER_DEFAULT_MIX_ALGORITHM_CONFIG(),    \
    .event_handle = NULL,                                               \
    .args = NULL,                                                       \
}

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
/**
 * @brief Default EQ algorithm configuration
 */
#define DEFAULT_BLUE_AUDIO_PLAYER_EQ_CONFIG() DEFAULT_EQ_ALGORITHM_CONFIG()
#endif

/**
 * @brief Create blue audio player with configuration
 *
 * This API creates blue audio player handle according to configuration.
 * This API should be called before other APIs.
 *
 * @param[in] config    Blue audio player configuration
 *
 * @return
 *    - Not NULL: success
 *    - NULL: failed
 */
blue_audio_player_handle_t blue_audio_player_create(blue_audio_player_cfg_t *config);

/**
 * @brief Destroy blue audio player
 *
 * This API destroys blue audio player according to player handle.
 *
 * @param[in] player  The blue audio player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_player_destroy(blue_audio_player_handle_t player);

/**
 * @brief Start blue audio player
 *
 * This API starts blue audio player and begins decoding and playing audio data.
 *
 * @param[in] player  The blue audio player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_player_start(blue_audio_player_handle_t player);

/**
 * @brief Stop blue audio player
 *
 * This API stops blue audio player.
 *
 * @param[in] player  The blue audio player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_player_stop(blue_audio_player_handle_t player);

/**
 * @brief Write frame data to blue audio player
 *
 * This API writes frame audio data to the decoder.
 * Data is directly output to the raw_stream's output frame buffer without using a pool cache.
 *
 * @param[in] player      The blue audio player handle
 * @param[in] buffer      The audio data buffer
 * @param[in] len         The length (byte) of audio data
 *
 * @return
 *    - len: length of the written data
 *    - Others: failed
 */
bk_err_t blue_audio_player_write_frame_data(blue_audio_player_handle_t player, char *buffer, uint32_t len);

/**
 * @brief Get free frame number of blue audio player
 *
 * This API gets free frame number of blue audio player.
 * To avoid blocking when writing data, you can call this API before calling the blue_audio_player_write_frame_data
 * API to write data frames to confirm if there are free nodes available for writing data.
 *
 * @param[in] player  The blue audio player handle
 *
 * @return
 *    - free frame number: success
 *    - Others: failed
 */
bk_err_t blue_audio_player_get_free_frame_num(blue_audio_player_handle_t player);

/**
 * @brief Set volume of blue audio player
 *
 * This API sets volume of blue audio player.
 *
 * @param[in] player  The blue audio player handle
 * @param[in] volume  The volume value
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_player_set_volume(blue_audio_player_handle_t player, uint8_t volume);

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
/**
 * @brief Get the EQ algorithm element handle
 *
 * This API gets the EQ algorithm handle from the player.
 *
 * @param[in] player   The blue audio player handle
 * @param[out] eq_alg  The pointer to store the EQ algorithm handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_player_get_eq_algorithm(blue_audio_player_handle_t player, audio_element_handle_t *eq_alg);
#endif

#ifdef __cplusplus
}
#endif
#endif /* __BLUE_AUDIO_PLAYER_SERVICE_H__ */