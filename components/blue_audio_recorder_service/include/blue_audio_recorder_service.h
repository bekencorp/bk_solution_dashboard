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

#ifndef __BLUE_AUDIO_RECORDER_SERVICE_H__
#define __BLUE_AUDIO_RECORDER_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ADK_ONBOARD_MIC_STREAM_V2
#include <components/bk_audio/audio_streams/onboard_mic_stream_v2.h>
#else
#include <components/bk_audio/audio_streams/onboard_mic_stream.h>
#endif
#include <components/bk_audio/audio_streams/raw_stream.h>
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_MSBC_ENCODER
#include "components/bk_audio/audio_encoders/sbc_enc.h"
#endif
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
#include "components/bk_audio/audio_algorithms/eq_algorithm.h"
#endif
/**
 * @brief Encoder type enum
 */
typedef enum
{
    BLUE_AUDIO_RECORDER_ENCODER_TYPE_SBC = 0,
    BLUE_AUDIO_RECORDER_ENCODER_TYPE_PCM,  /* PCM format, no encoding needed */
} blue_audio_recorder_encoder_type_t;

/**
 * @brief MIC type enum
 */
typedef enum
{
    BLUE_AUDIO_RECORDER_MIC_TYPE_ONBOARD = 0,
    BLUE_AUDIO_RECORDER_MIC_TYPE_UAC,
    BLUE_AUDIO_RECORDER_MIC_TYPE_I2S,  /* Reserved for future support */
} blue_audio_recorder_mic_type_t;

/**
 * @brief Recorder state enum
 */
typedef enum
{
    BLUE_AUDIO_RECORDER_STATE_NONE = 0,
    BLUE_AUDIO_RECORDER_STATE_IDLE,
    BLUE_AUDIO_RECORDER_STATE_STOPPED,
    BLUE_AUDIO_RECORDER_STATE_RECORDING,
    BLUE_AUDIO_RECORDER_STATE_PAUSED,
} blue_audio_recorder_state_t;

/**
 * @brief Recorder event enum
 */
typedef enum
{
    BLUE_AUDIO_RECORDER_EVENT_ERROR = 0,
    BLUE_AUDIO_RECORDER_EVENT_START,
    BLUE_AUDIO_RECORDER_EVENT_STOP,
} blue_audio_recorder_event_t;

/**
 * @brief Recorder handle type
 */
typedef struct blue_audio_recorder *blue_audio_recorder_handle_t;

/**
 * @brief Recorder event callback function type
 */
typedef int (*blue_audio_recorder_event_handle_cb)(int, void *, void *);

/**
 * @brief Recorder configuration structure
 */
typedef struct
{
    blue_audio_recorder_encoder_type_t encoder_type;      /*!< Encoder type */
    blue_audio_recorder_mic_type_t mic_type;             /*!< MIC type */
    raw_stream_cfg_t raw_strm_cfg;                       /*!< Raw stream configuration */
    uint32_t task_stack;                                 /*!< Task stack size */
    int task_core;                                       /*!< Task running core (0 or 1) */
    int task_prio;                                       /*!< Task priority */

    union {
#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_MSBC_ENCODER
        sbc_encoder_cfg_t sbc_enc_cfg;                   /*!< SBC encoder configuration */
#endif
        /* Reserved for other encoder configurations */
    } encoder_cfg;

    union {
        onboard_mic_stream_cfg_t ob_mic_cfg;             /*!< Onboard mic configuration */
#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_UAC_MIC
        uac_mic_stream_cfg_t uac_mic_cfg;                /*!< UAC mic configuration */
#endif
        /* Reserved for I2S mic configuration */
    } mic_cfg;

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    bool eq_en;                                          /*!< Enable EQ algorithm */
    union {
        eq_algorithm_cfg_t eq_alg_cfg;                  /*!< EQ algorithm configuration */
    } eq_cfg;                                           /*!< EQ configuration union */
#endif

    blue_audio_recorder_event_handle_cb event_handle;    /*!< Event callback function */
    void *args;                                          /*!< User data for callback */
} blue_audio_recorder_cfg_t;

#define BLUE_AUDIO_RECORDER_DEFAULT_TASK_STACK      (4 * 1024)
#define BLUE_AUDIO_RECORDER_DEFAULT_TASK_CORE       (1)
#define BLUE_AUDIO_RECORDER_DEFAULT_TASK_PRIO       (BEKEN_DEFAULT_WORKER_PRIORITY - 1)
#define BLUE_AUDIO_RECORDER_DEFAULT_FRAME_SIZE      (512)
#define BLUE_AUDIO_RECORDER_DEFAULT_FRAME_NUM       (25)

#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_MSBC_ENCODER
/**
 * @brief Default SBC encoder configuration
 */
#define BLUE_AUDIO_RECORDER_DEFAULT_SBC_ENC_CONFIG() {    \
    .buf_sz             = SBC_ENCODER_BUFFER_SIZE,        \
    .out_block_size     = SBC_ENCODER_OUT_BLOCK_SIZE,     \
    .out_block_num      = SBC_ENCODER_OUT_BLOCK_NUM,      \
    .task_stack         = SBC_ENCODER_TASK_STACK,         \
    .task_core          = SBC_ENCODER_TASK_CORE,          \
    .task_prio          = SBC_ENCODER_TASK_PRIO,          \
    .sample_rate        = 16000,                          \
    .channels           = 1,                              \
    .bitpool            = 26,                             \
    .channel_mode       = SBC_ENC_CHANNEL_MODE_MONO,      \
    .subbands           = SBC_ENC_SUBBANDS_8,             \
    .blocks             = SBC_ENC_BLOCKS_16,              \
    .allocation_method  = SBC_ENC_ALLOCATION_METHOD_LOUDNESS, \
    .msbc_mode          = true,                           \
}
#endif

/**
 * @brief Default onboard mic configuration
 */
#if CONFIG_ADK_ONBOARD_MIC_STREAM_V2
#define BLUE_AUDIO_RECORDER_DEFAULT_ONBOARD_MIC_CONFIG() {   \
    .adc_cfg = {                                            \
                   .chl_num = 1,                            \
                   .sample_rate = 8000,                     \
                   .adc_samp_edge = AUD_ADC_SAMP_EDGE_RISING, \
                   .clk_src = AUD_CLK_APLL,                  \
                   .chl_cfg = {                              \
                       {                                     \
                           .dig_gain = 0x1c000,              \
                           .ana_gain = 0x07,                 \
                           .adc_mode = AUD_ADC_MODE_DIFFEN,  \
                           .bits = 16,                       \
                       },                                    \
                       {                                     \
                           .dig_gain = 0x4000,               \
                           .ana_gain = 0x07,                 \
                           .adc_mode = AUD_ADC_MODE_DIFFEN,  \
                           .bits = 16,                       \
                       },                                    \
                       {                                     \
                           .dig_gain = 0x4000,               \
                           .ana_gain = 0x07,                 \
                           .adc_mode = AUD_ADC_MODE_DIFFEN,  \
                           .bits = 16,                       \
                       },                                    \
                   },                                        \
               },                                            \
    .frame_size = 320,                                       \
    .out_block_size = 320,                                   \
    .out_block_num = 2,                                      \
    .multi_out_port_num = 0,                                 \
    .task_stack = ONBOARD_MIC_STREAM_TASK_STACK,             \
    .task_core = ONBOARD_MIC_STREAM_TASK_CORE,               \
    .task_prio = ONBOARD_MIC_STREAM_TASK_PRIO,               \
    .ch_bitmap = (1 << AUD_ADC_CHL_0),                       \
}
#else
#define BLUE_AUDIO_RECORDER_DEFAULT_ONBOARD_MIC_CONFIG() {   \
    .adc_cfg = {                                            \
                   .chl_num = 1,                            \
                   .bits = 16,                              \
                   .sample_rate = 8000,                     \
                   .dig_gain = 0x2D,                        \
                   .ana_gain = 0x00,                        \
                   .mode = AUD_ADC_MODE_DIFFEN,             \
                   .clk_src = AUD_CLK_XTAL,                 \
               },                                           \
    .frame_size = 320,                                      \
    .out_block_size = 320,                                  \
    .out_block_num = 2,                                     \
    .multi_out_port_num = 0,                                \
    .task_stack = ONBOARD_MIC_STREAM_TASK_STACK,            \
    .task_core = ONBOARD_MIC_STREAM_TASK_CORE,              \
    .task_prio = ONBOARD_MIC_STREAM_TASK_PRIO,              \
}
#endif

#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_UAC_MIC
/**
 * @brief Default UAC mic configuration
 */
#define BLUE_AUDIO_RECORDER_DEFAULT_UAC_MIC_CONFIG() {       \
    .port_index = USB_HUB_PORT_1,                           \
    .format = AUDIO_FORMAT_PCM,                             \
    .chl_num = 1,                                           \
    .bits = 16,                                             \
    .samp_rate = 8000,                                      \
    .frame_size = 320,                                      \
    .out_block_size = 320,                                  \
    .out_block_num = 1,                                     \
    .auto_connect = true,                                   \
    .multi_out_port_num = 0,                                \
    .task_stack = UAC_MIC_STREAM_TASK_STACK,                \
    .task_core = UAC_MIC_STREAM_TASK_CORE,                  \
    .task_prio = UAC_MIC_STREAM_TASK_PRIO,                  \
}
#endif

#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_MSBC_ENCODER
/**
 * @brief Default recorder configuration
 */
#define DEFAULT_BLUE_AUDIO_RECORDER_SBC_ONBOARD_MIC_CONFIG() {           \
    .encoder_type = BLUE_AUDIO_RECORDER_ENCODER_TYPE_SBC,               \
    .mic_type = BLUE_AUDIO_RECORDER_MIC_TYPE_ONBOARD,                   \
    .raw_strm_cfg = {                                                   \
        .type = AUDIO_STREAM_READER,                                    \
        .out_block_size = BLUE_AUDIO_RECORDER_DEFAULT_FRAME_SIZE,       \
        .out_block_num = BLUE_AUDIO_RECORDER_DEFAULT_FRAME_NUM,         \
        .output_port_type = PORT_TYPE_FB                                \
    },                                                                  \
    .task_stack = BLUE_AUDIO_RECORDER_DEFAULT_TASK_STACK,               \
    .task_core = BLUE_AUDIO_RECORDER_DEFAULT_TASK_CORE,                 \
    .task_prio = BLUE_AUDIO_RECORDER_DEFAULT_TASK_PRIO,                 \
    .encoder_cfg = {                                                    \
        .sbc_enc_cfg = BLUE_AUDIO_RECORDER_DEFAULT_SBC_ENC_CONFIG()     \
    },                                                                  \
    .mic_cfg = {                                                        \
        .ob_mic_cfg = BLUE_AUDIO_RECORDER_DEFAULT_ONBOARD_MIC_CONFIG()  \
    },                                                                  \
    .event_handle = NULL,                                               \
    .args = NULL,                                                       \
}
#endif

/**
 * @brief Default PCM recorder configuration
 */
#define DEFAULT_BLUE_AUDIO_RECORDER_PCM_ONBOARD_MIC_CONFIG() {          \
    .encoder_type = BLUE_AUDIO_RECORDER_ENCODER_TYPE_PCM,               \
    .mic_type = BLUE_AUDIO_RECORDER_MIC_TYPE_ONBOARD,                   \
    .raw_strm_cfg = {                                                   \
        .type = AUDIO_STREAM_READER,                                    \
        .out_block_size = BLUE_AUDIO_RECORDER_DEFAULT_FRAME_SIZE,       \
        .out_block_num = BLUE_AUDIO_RECORDER_DEFAULT_FRAME_NUM,         \
        .output_port_type = PORT_TYPE_FB                                \
    },                                                                  \
    .task_stack = BLUE_AUDIO_RECORDER_DEFAULT_TASK_STACK,               \
    .task_core = BLUE_AUDIO_RECORDER_DEFAULT_TASK_CORE,                 \
    .task_prio = BLUE_AUDIO_RECORDER_DEFAULT_TASK_PRIO,                 \
    .mic_cfg = {                                                        \
        .ob_mic_cfg = BLUE_AUDIO_RECORDER_DEFAULT_ONBOARD_MIC_CONFIG()  \
    },                                                                  \
    .event_handle = NULL,                                               \
    .args = NULL,                                                       \
}

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
/**
 * @brief Default EQ algorithm configuration
 */
#define DEFAULT_BLUE_AUDIO_RECORDER_EQ_CONFIG() DEFAULT_EQ_ALGORITHM_CONFIG()
#endif


/**
 * @brief Create bluetooth audio recorder with configuration
 *
 * This API creates blue audio recorder handle according to configuration.
 * This API should be called before other APIs.
 *
 * @param[in] config    Blue audio recorder configuration
 *
 * @return
 *    - Not NULL: success
 *    - NULL: failed
 */
blue_audio_recorder_handle_t blue_audio_recorder_create(blue_audio_recorder_cfg_t *config);

/**
 * @brief Destroy bluetooth audio recorder
 *
 * This API destroys bluetooth audio recorder according to recorder handle.
 *
 * @param[in] recorder  The bluetooth audio recorder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_recorder_destroy(blue_audio_recorder_handle_t recorder);

/**
 * @brief Start bluetooth audio recorder
 *
 * This API starts bluetooth audio recorder and begins recording and encoding audio data.
 *
 * @param[in] recorder  The bluetooth audio recorder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_recorder_start(blue_audio_recorder_handle_t recorder);

/**
 * @brief Stop bluetooth audio recorder
 *
 * This API stops bluetooth audio recorder.
 *
 * @param[in] recorder  The bluetooth audio recorder handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_recorder_stop(blue_audio_recorder_handle_t recorder);

/**
 * @brief Read frame data from bluetooth audio recorder
 *
 * This API reads frame audio data from the encoder.
 * Data is directly read from the raw_stream's output frame buffer.
 *
 * @param[in] recorder    The bluetooth audio recorder handle
 * @param[in] buffer      The buffer to store audio data
 * @param[in] len         The length (byte) of buffer
 *
 * @return
 *    - len: length of the read data
 *    - Others: failed
 */
bk_err_t blue_audio_recorder_read_frame_data(blue_audio_recorder_handle_t recorder, char *buffer, uint32_t len);

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
/**
 * @brief Get the EQ algorithm element handle
 *
 * This API gets the EQ algorithm element handle from the recorder.
 *
 * @param[in] recorder    The bluetooth audio recorder handle
 * @param[out] eq_alg The pointer to store the EQ algorithm handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
bk_err_t blue_audio_recorder_get_eq_algorithm(blue_audio_recorder_handle_t recorder, audio_element_handle_t *eq_alg);
#endif

#ifdef __cplusplus
}
#endif
#endif /* __BLUE_AUDIO_RECORDER_SERVICE_H__ */