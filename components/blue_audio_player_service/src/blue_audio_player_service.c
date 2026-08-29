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

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include <components/bk_audio/audio_pipeline/audio_thread.h>
#include <components/bk_audio/audio_streams/raw_stream.h>
#include <components/bk_audio/audio_streams/onboard_speaker_stream.h>
#include <components/bk_audio/audio_streams/uac_speaker_stream.h>
#include <components/bk_audio/audio_streams/i2s_stream.h>
#include <components/bk_audio/audio_decoders/sbc_decoder.h>
#include <components/bk_audio/audio_decoders/aac_decoder.h>
#include <components/bk_audio/audio_pipeline/audio_event_iface.h>
#include <components/bk_audio/audio_pipeline/audio_error.h>
#include <components/bk_audio/audio_algorithms/mix_algorithm.h>
#include <components/bk_audio/audio_pipeline/bsd_queue.h>

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
#include <components/bk_audio/audio_algorithms/eq_algorithm.h>
#endif

#include <blue_audio_player_service.h>

#define TAG "blue_audio_player"

#define BLUE_AUDIO_PLAYER_CHECK_NULL(ptr, act) do {\
        if (ptr == NULL) {\
            BK_LOGE(TAG, "%s, %d, NULL pointer check failed\n", __func__, __LINE__);\
            {act;};\
        }\
    } while(0)

/**
 * @brief Listener operation enum
 */
typedef enum
{
    LISTENER_IDLE = 0,
    LISTENER_START,
    LISTENER_EXIT
} listener_op_t;

/**
 * @brief Listener message structure
 */
typedef struct
{
    listener_op_t op;
    void *param;
} listener_msg_t;

/**
 * @brief Audio port information
 */
typedef struct blue_audio_frame_info
{
    char *                  buffer;            /*!< bluetooth audio frame buffer */
    uint32_t                len;               /*!< bluetooth audio frame length(byte) */
} blue_audio_frame_info_t;

typedef struct blue_audio_frame_info_item
{
    STAILQ_ENTRY(blue_audio_frame_info_item)    next;
    blue_audio_frame_info_t                     frame_info;
} blue_audio_frame_info_item_t;

typedef STAILQ_HEAD(blue_audio_frame_info_list, blue_audio_frame_info_item) blue_audio_frame_info_list_t;

/**
 * @brief Blue audio player structure
 */
struct blue_audio_player
{
    audio_pipeline_handle_t     pipeline;           /*!< Audio pipeline handle */
    audio_element_handle_t      raw_strm;           /*!< Raw stream handle */
    audio_element_handle_t      decoder;            /*!< Decoder handle */
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    bool                        eq_en;              /*!< EQ enable flag */
    audio_element_handle_t      eq_alg;             /*!< EQ algorithm handle */
#endif
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    audio_element_handle_t      mix_alg;            /*!< Mix algorithm handle */
#endif
    audio_element_handle_t      speaker;            /*!< Speaker handle */

    audio_event_iface_handle_t  event_iface;        /*!< Event interface handle */

    blue_audio_player_state_t   state;              /*!< Player state */
    blue_audio_decoder_type_t   decoder_type;       /*!< Decoder type */
    blue_audio_speaker_type_t   speaker_type;       /*!< Speaker type */

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    bool                        mix_en;             /*!< Mix algorithm enable flag */
#endif

    blue_audio_player_event_handle_cb event_handle; /*!< Event callback function */
    void *                      args;               /*!< User data for callback */

    beken_thread_t              listener_task;      /*!< Listener task handle */
    beken_queue_t               listener_msg_queue; /*!< Listener message queue */
    beken_semaphore_t           listener_sem;       /*!< Listener semaphore */
    bool                        listener_running;   /*!< Listener running flag */

    uint32_t                    play_threshold;     /*!< Play threshold */
    bool                        pipeline_is_running;/*!< Pipeline is running flag */
    blue_audio_frame_info_list_t    frame_list;     /*!< bluetooth audio frame list */
};



static bk_err_t _add_blue_audio_frame_to_list(blue_audio_frame_info_list_t *frame_list, char *buffer, uint32_t len)
{
    blue_audio_frame_info_item_t *audio_port_info_item = NULL;
    blue_audio_frame_info_item_t *prev_audio_port_info_item = NULL;

    STAILQ_FOREACH(audio_port_info_item, frame_list, next)
    {
        if (audio_port_info_item && audio_port_info_item->frame_info.len >= len)
        {
            prev_audio_port_info_item = audio_port_info_item;
        }
    }

    blue_audio_frame_info_item_t *new_audio_port_info_item = audio_calloc(1, sizeof(blue_audio_frame_info_item_t));
    AUDIO_MEM_CHECK(TAG, new_audio_port_info_item, return BK_FAIL);
    os_memset(new_audio_port_info_item, 0, sizeof(blue_audio_frame_info_item_t));
    new_audio_port_info_item->frame_info.buffer = (char *)audio_malloc(len);
    if (!new_audio_port_info_item->frame_info.buffer)
    {
        audio_free(new_audio_port_info_item);
        return BK_FAIL;
    }
    os_memcpy(new_audio_port_info_item->frame_info.buffer, buffer, len);
    new_audio_port_info_item->frame_info.len = len;

    if (prev_audio_port_info_item)
    {
        STAILQ_INSERT_AFTER(frame_list, prev_audio_port_info_item, new_audio_port_info_item, next);
    }
    else
    {
        STAILQ_INSERT_HEAD(frame_list, new_audio_port_info_item, next);
    }

    return BK_OK;
}

static blue_audio_frame_info_item_t *_get_blue_audio_frame_info(blue_audio_frame_info_list_t *frame_list)
{
    blue_audio_frame_info_item_t *audio_port_info_item = NULL;

    if (STAILQ_EMPTY(frame_list))
    {
        return NULL;
    }

    /* Get the head element of the linked list */
    audio_port_info_item = STAILQ_FIRST(frame_list);
    /* Remove the head element of the linked list */
    STAILQ_REMOVE_HEAD(frame_list, next);
    /* Note: Here, only the reference to the node in the linked list is removed,
     * and the memory of frame_info->buffer and audio_port_info_item is not freed,
     * the caller needs to responsible for freeing the memory of frame_info->buffer and audio_port_info_item */

    return audio_port_info_item;
}

static uint32_t _get_blue_audio_frame_count(blue_audio_frame_info_list_t *frame_list)
{
    blue_audio_frame_info_item_t *audio_port_info_item = NULL;
    uint32_t count = 0;

    BLUE_AUDIO_PLAYER_CHECK_NULL(frame_list, return 0);

    STAILQ_FOREACH(audio_port_info_item, frame_list, next)
    {
        if (audio_port_info_item && audio_port_info_item->frame_info.buffer && audio_port_info_item->frame_info.len > 0)
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief Start audio pipeline
 *
 * @param pipeline Audio pipeline handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_pipeline_start(audio_pipeline_handle_t pipeline)
{
    BLUE_AUDIO_PLAYER_CHECK_NULL(pipeline, return BK_FAIL);

    if (BK_OK != audio_pipeline_run(pipeline))
    {
        BK_LOGE(TAG, "%s, %d, Pipeline run failed\n", __func__, __LINE__);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief Stop audio pipeline
 *
 * @param pipeline Audio pipeline handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_pipeline_stop(audio_pipeline_handle_t pipeline)
{
    BLUE_AUDIO_PLAYER_CHECK_NULL(pipeline, return BK_FAIL);

    BK_LOGD(TAG, "%s\n", __func__);

    if (BK_OK != audio_pipeline_stop(pipeline))
    {
        BK_LOGE(TAG, "%s, %d, Pipeline stop failed\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (BK_OK != audio_pipeline_wait_for_stop(pipeline))
    {
        BK_LOGE(TAG, "%s, %d, Pipeline wait for stop failed\n", __func__, __LINE__);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief Deinitialize audio pipeline
 *
 * @param player Blue audio player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_pipeline_deinit(blue_audio_player_handle_t player)
{
    BK_LOGD(TAG, "%s\n", __func__);

    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);

    if (!player->pipeline)
    {
        return BK_OK;
    }

    if (player->state == BLUE_AUDIO_PLAYER_STATE_PLAYING)
    {
        blue_audio_pipeline_stop(player->pipeline);
    }

    if (BK_OK != audio_pipeline_terminate(player->pipeline))
    {
        BK_LOGE(TAG, "%s, %d, Pipeline terminate failed\n", __func__, __LINE__);
        return BK_FAIL;
    }

    // Unregister elements
    if (player->raw_strm)
    {
        if (BK_OK != audio_pipeline_unregister(player->pipeline, player->raw_strm))
        {
            BK_LOGE(TAG, "%s, %d, Unregister raw stream failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }

    if (player->decoder)
    {
        if (BK_OK != audio_pipeline_unregister(player->pipeline, player->decoder))
        {
            BK_LOGE(TAG, "%s, %d, Unregister decoder failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    if (player->eq_en && player->eq_alg)
    {
        if (BK_OK != audio_pipeline_unregister(player->pipeline, player->eq_alg))
        {
            BK_LOGE(TAG, "%s, %d, Unregister EQ algorithm failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }
#endif

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    if (player->mix_alg)
    {
        if (BK_OK != audio_pipeline_unregister(player->pipeline, player->mix_alg))
        {
            BK_LOGE(TAG, "%s, %d, Unregister mix algorithm failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }
#endif

    if (player->speaker)
    {
        if (BK_OK != audio_pipeline_unregister(player->pipeline, player->speaker))
        {
            BK_LOGE(TAG, "%s, %d, Unregister speaker failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }

    // Deinitialize event interface
    if (player->event_iface)
    {
        if (BK_OK != audio_pipeline_remove_listener(player->pipeline))
        {
            BK_LOGE(TAG, "%s, %d, Remove listener failed\n", __func__, __LINE__);
            return BK_FAIL;
        }

        if (BK_OK != audio_event_iface_destroy(player->event_iface))
        {
            BK_LOGE(TAG, "%s, %d, Event interface destroy failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        player->event_iface = NULL;
    }

    // Deinitialize pipeline
    if (BK_OK != audio_pipeline_deinit(player->pipeline))
    {
        BK_LOGE(TAG, "%s, %d, Pipeline deinit failed\n", __func__, __LINE__);
        return BK_FAIL;
    }
    player->pipeline = NULL;

    if (player->raw_strm)
    {
        if (BK_OK != audio_element_deinit(player->raw_strm))
        {
            BK_LOGE(TAG, "%s, %d, Raw stream deinit failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        player->raw_strm = NULL;
    }

    if (player->decoder)
    {
        if (BK_OK != audio_element_deinit(player->decoder))
        {
            BK_LOGE(TAG, "%s, %d, Decoder deinit failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        player->decoder = NULL;
    }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    if (player->eq_en && player->eq_alg)
    {
        if (BK_OK != audio_element_deinit(player->eq_alg))
        {
            BK_LOGE(TAG, "%s, %d, EQ algorithm deinit failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        player->eq_alg = NULL;
    }
#endif

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    if (player->mix_alg)
    {
        if (BK_OK != audio_element_deinit(player->mix_alg))
        {
            BK_LOGE(TAG, "%s, %d, Mix algorithm deinit failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        player->mix_alg = NULL;
    }
#endif

    if (player->speaker)
    {
        if (BK_OK != audio_element_deinit(player->speaker))
        {
            BK_LOGE(TAG, "%s, %d, Speaker deinit failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        player->speaker = NULL;
    }

    return BK_OK;
}

/**
 * @brief Initialize audio pipeline
 *
 * @param player Blue audio player handle
 * @param config Blue audio player configuration
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_pipeline_init(blue_audio_player_handle_t player, blue_audio_player_cfg_t *config)
{
    bk_err_t ret = BK_OK;

    BK_LOGD(TAG, "%s\n", __func__);
    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);
    BLUE_AUDIO_PLAYER_CHECK_NULL(config, return BK_FAIL);

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    // Store EQ enable flag
    player->eq_en = config->eq_en;
#endif

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    // Store mix enable flag
    player->mix_en = config->mix_en;
#else
    if (config->mix_en)
    {
        BK_LOGE(TAG, "%s, %d, Mix algorithm not enable, please config CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX=y \n", __func__, __LINE__);
        return BK_FAIL;
    }
#endif

    // Initialize pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    player->pipeline = audio_pipeline_init(&pipeline_cfg);
    BLUE_AUDIO_PLAYER_CHECK_NULL(player->pipeline, return BK_FAIL);

    // Initialize raw stream
    raw_stream_cfg_t raw_cfg = config->raw_strm_cfg;
    player->raw_strm = raw_stream_init(&raw_cfg);
    BLUE_AUDIO_PLAYER_CHECK_NULL(player->raw_strm, goto fail);

    // Initialize decoder based on type if not PCM
    if (config->decoder_type != BLUE_AUDIO_DECODER_TYPE_PCM)
    {
        switch (config->decoder_type)
        {
            case BLUE_AUDIO_DECODER_TYPE_SBC:
                player->decoder = sbc_decoder_init(&config->decoder_cfg.sbc_dec_cfg);
                break;

            case BLUE_AUDIO_DECODER_TYPE_AAC:
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_AAC_DECODER
                player->decoder = aac_decoder_init(&config->decoder_cfg.aac_dec_cfg);
#else
                BK_LOGE(TAG, "%s, %d, AAC decoder not enable, please config CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_AAC_DECODER=y \n", __func__, __LINE__);
                goto fail;
#endif
                break;

            case BLUE_AUDIO_DECODER_TYPE_LC3:
                // Reserved for future support
                BK_LOGE(TAG, "%s, %d, LC3 decoder not supported yet\n", __func__, __LINE__);
                goto fail;

            default:
                BK_LOGE(TAG, "%s, %d, Unsupported decoder type: %d\n", __func__, __LINE__, config->decoder_type);
                goto fail;
        }
        BLUE_AUDIO_PLAYER_CHECK_NULL(player->decoder, goto fail);
    }
    else
    {
        // For PCM format, no decoder is needed
        player->decoder = NULL;
        BK_LOGD(TAG, "%s, PCM format selected, no decoder needed\n", __func__);
    }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    // Initialize EQ algorithm if enabled
    if (player->eq_en)
    {
        player->eq_alg = eq_algorithm_init(&config->eq_cfg.eq_alg_cfg);
        if (!player->eq_alg)
        {
            BK_LOGE(TAG, "%s, %d, EQ algorithm init failed\n", __func__, __LINE__);
            goto fail;
        }
    }
#endif

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    // Initialize mix algorithm if enabled
    if (player->mix_en)
    {
        player->mix_alg = mix_algorithm_init(&config->mix_alg_cfg);
        BLUE_AUDIO_PLAYER_CHECK_NULL(player->mix_alg, goto fail);
    }
#endif

    // Initialize speaker based on type
    switch (config->speaker_type)
    {
        case BLUE_AUDIO_SPEAKER_TYPE_ONBOARD:
            player->speaker = onboard_speaker_stream_init(&config->speaker_cfg.ob_spk_cfg);
            break;

        case BLUE_AUDIO_SPEAKER_TYPE_UAC:
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_UAC_SPEAKER
            player->speaker = uac_speaker_stream_init(&config->speaker_cfg.uac_spk_cfg);
#else
            BK_LOGE(TAG, "%s, %d, UAC speaker not enable, please config CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_UAC_SPEAKER=y \n", __func__, __LINE__);
            goto fail;
#endif
            break;

        case BLUE_AUDIO_SPEAKER_TYPE_I2S:
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_I2S_SPEAKER
            player->speaker = i2s_stream_init(&config->speaker_cfg.i2s_spk_cfg);
#else
            BK_LOGE(TAG, "%s, %d, I2S speaker not enable, please config CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_I2S_SPEAKER=y \n", __func__, __LINE__);
            goto fail;
#endif
            break;

        default:
            BK_LOGE(TAG, "%s, %d, Unsupported speaker type: %d\n", __func__, __LINE__, config->speaker_type);
            goto fail;
    }
    BLUE_AUDIO_PLAYER_CHECK_NULL(player->speaker, goto fail);

    // Register elements to pipeline
    if (BK_OK != audio_pipeline_register(player->pipeline, player->raw_strm, "raw_strm"))
    {
        BK_LOGE(TAG, "%s, %d, Register raw stream failed\n", __func__, __LINE__);
        goto fail;
    }

    // For PCM format, decoder is not needed
    if (player->decoder)
    {
        if (BK_OK != audio_pipeline_register(player->pipeline, player->decoder, "decoder"))
        {
            BK_LOGE(TAG, "%s, %d, Register decoder failed\n", __func__, __LINE__);
            goto fail;
        }
    }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    if (player->eq_en && player->eq_alg)
    {
        if (BK_OK != audio_pipeline_register(player->pipeline, player->eq_alg, "eq_alg"))
        {
            BK_LOGE(TAG, "%s, %d, Register EQ algorithm failed\n", __func__, __LINE__);
            goto fail;
        }
    }
#endif

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
    if (player->mix_en)
    {
        if (BK_OK != audio_pipeline_register(player->pipeline, player->mix_alg, "mix_alg"))
        {
            BK_LOGE(TAG, "%s, %d, Register mix algorithm failed\n", __func__, __LINE__);
            goto fail;
        }
    }
#endif

    if (BK_OK != audio_pipeline_register(player->pipeline, player->speaker, "speaker"))
    {
        BK_LOGE(TAG, "%s, %d, Register speaker failed\n", __func__, __LINE__);
        goto fail;
    }

    // Link elements in pipeline based on decoder type, EQ enable, and mix enable
    // Data flow: raw_strm -> [decoder] -> [eq_alg] -> [mix_alg] -> speaker
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    if (player->eq_en && player->eq_alg)
    {
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
        if (player->mix_en)
        {
            if (player->decoder)
            {
                // raw_strm -> decoder -> eq_alg -> mix_alg -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "decoder", "eq_alg", "mix_alg", "speaker"}, 5);
            }
            else
            {
                // raw_strm -> eq_alg -> mix_alg -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "eq_alg", "mix_alg", "speaker"}, 4);
            }
        }
        else
#endif
        {
            if (player->decoder)
            {
                // raw_strm -> decoder -> eq_alg -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "decoder", "eq_alg", "speaker"}, 4);
            }
            else
            {
                // raw_strm -> eq_alg -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "eq_alg", "speaker"}, 3);
            }
        }
    }
    else
#endif
    {
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
        if (player->mix_en)
        {
            if (player->decoder)
            {
                // raw_strm -> decoder -> mix_alg -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "decoder", "mix_alg", "speaker"}, 4);
            }
            else
            {
                // raw_strm -> mix_alg -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "mix_alg", "speaker"}, 3);
            }
        }
        else
#endif
        {
            if (player->decoder)
            {
                // raw_strm -> decoder -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "decoder", "speaker"}, 3);
            }
            else
            {
                // raw_strm -> speaker
                ret = audio_pipeline_link(player->pipeline, (const char *[]) {"raw_strm", "speaker"}, 2);
            }
        }
    }

    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Pipeline link failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    // Initialize event interface for listening to pipeline events
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    player->event_iface = audio_event_iface_init(&evt_cfg);
    if (player->event_iface == NULL)
    {
        BK_LOGE(TAG, "%s, %d, Event interface init failed\n", __func__, __LINE__);
        goto fail;
    }

    if (BK_OK != audio_pipeline_set_listener(player->pipeline, player->event_iface))
    {
        BK_LOGE(TAG, "%s, %d, Set pipeline listener failed\n", __func__, __LINE__);
        goto fail;
    }

    return BK_OK;

fail:
    blue_audio_pipeline_deinit(player);
    return BK_FAIL;
}

/**
 * @brief Send message to listener task
 *
 * @param queue Message queue handle
 * @param op Operation type
 * @param param Parameter
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_listener_send_msg(beken_queue_t queue, listener_op_t op, void *param)
{
    bk_err_t ret;
    listener_msg_t msg;

    if (!queue)
    {
        BK_LOGE(TAG, "%s, %d, NULL queue\n", __func__, __LINE__);
        return BK_FAIL;
    }

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(&queue, &msg, BEKEN_NO_WAIT);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, Send message failed, op: %d, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief Listener task main function
 *
 * @param param_data User parameter
 */
static void blue_audio_listener_task_main(beken_thread_arg_t param_data)
{
    blue_audio_player_handle_t player = (blue_audio_player_handle_t)param_data;
    bk_err_t ret = BK_OK;

    player->listener_running = false;
    long unsigned int wait_time = BEKEN_WAIT_FOREVER;

    rtos_set_semaphore(&player->listener_sem);

    while (1)
    {
        listener_msg_t msg;
        ret = rtos_pop_from_queue(&player->listener_msg_queue, &msg, wait_time);
        if (kNoErr == ret)
        {
            switch (msg.op)
            {
                case LISTENER_IDLE:
                    rtos_set_semaphore(&player->listener_sem);
                    player->listener_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case LISTENER_EXIT:
                    goto exit;
                    break;

                case LISTENER_START:
                    player->listener_running = true;
                    wait_time = 0;
                    break;

                default:
                    break;
            }
        }

        audio_event_iface_msg_t event_msg;
        audio_element_status_t el_status = AEL_STATUS_NONE;
        if (player->listener_running)
        {
            ret = audio_event_iface_listen(player->event_iface, &event_msg, 10 / portTICK_RATE_MS);
            if (ret == BK_OK)
            {
                if (event_msg.cmd == AEL_MSG_CMD_REPORT_STATUS)
                {
                    el_status = (int)(uintptr_t)event_msg.data;
                    switch (el_status)
                    {
                        case AEL_STATUS_ERROR_OPEN:
                        case AEL_STATUS_ERROR_INPUT:
                        case AEL_STATUS_ERROR_PROCESS:
                        case AEL_STATUS_ERROR_OUTPUT:
                        case AEL_STATUS_ERROR_CLOSE:
                        case AEL_STATUS_ERROR_TIMEOUT:
                        case AEL_STATUS_ERROR_UNKNOWN:
                            if (player->state == BLUE_AUDIO_PLAYER_STATE_PLAYING)
                            {
                                BK_LOGW(TAG, "%s, %d, Pipeline error event received, state: %d, ele: %p\n", 
                                        __func__, __LINE__, (int)event_msg.data, event_msg.source);
                                
                                // Stop player and notify error
                                blue_audio_player_stop(player);
                                if (player->event_handle)
                                {
                                    player->event_handle(BLUE_AUDIO_PLAYER_EVENT_ERROR, NULL, player->args);
                                }
                                
                                player->listener_running = false;
                                wait_time = BEKEN_WAIT_FOREVER;
                                continue;
                            }
                            break;

                        case AEL_STATUS_STATE_FINISHED:
                            if (event_msg.source == player->speaker && player->state == BLUE_AUDIO_PLAYER_STATE_PLAYING)
                            {
                                BK_LOGW(TAG, "%s, %d, Pipeline finish event received\n", __func__, __LINE__);
                                
                                // Stop player and notify finish
                                blue_audio_player_stop(player);
                                if (player->event_handle)
                                {
                                    player->event_handle(BLUE_AUDIO_PLAYER_EVENT_FINISH, NULL, player->args);
                                }
                                
                                player->listener_running = false;
                                wait_time = BEKEN_WAIT_FOREVER;
                                continue;
                            }
                            break;

                        default:
                            break;
                    }
                }
                else if (event_msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO && event_msg.source == player->decoder)
                {
                    audio_element_info_t music_info = {0};
                    audio_element_getinfo(player->decoder, &music_info);
                    BK_LOGD(TAG, "[ * ] Receive music info from spk decoder, sample_rates=%d, bits=%d, ch=%d \n", music_info.sample_rates, music_info.bits, music_info.channels);

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_MIX
                    if (!player->mix_en && player->speaker)
#else
                    if (player->speaker)
#endif
                    {
                        if (player->speaker_type == BLUE_AUDIO_SPEAKER_TYPE_ONBOARD)
                        {
                            onboard_speaker_stream_set_param(player->speaker, music_info.sample_rates, music_info.bits, music_info.channels);
                        }
                        else if (player->speaker_type == BLUE_AUDIO_SPEAKER_TYPE_UAC)
                        {
                            /* Not support */
                            //TODO
                            BK_LOGD(TAG, "Dynamic switching of UAC speaker format information is not supported \n");
                        }
                        else if (player->speaker_type == BLUE_AUDIO_SPEAKER_TYPE_I2S)
                        {
                            /* Not support */
                            //TODO
                            BK_LOGD(TAG, "Dynamic switching of I2S speaker format information is not supported \n");
                        }
                        else
                        {
                            BK_LOGW(TAG, "%s, %d, Unknown speaker type: %d\n", __func__, __LINE__, player->speaker_type);
                        }
                    }
                    else
                    {
                        if (!player->speaker && player->event_handle)
                        {
                            player->event_handle(BLUE_AUDIO_PLAYER_EVENT_MUSIC_INFO,  &music_info, player->args);
                        }
                    }
                }
                else
                {
                    BK_LOGW(TAG, "%s, %d, ++>>play pipeline event received, state: %d, ele: %p\n", __func__, __LINE__, (int)event_msg.data, event_msg.source);
                    //TODO
                }
            }
        }
    }

 exit:
    BK_LOGD(TAG, "%s, listener task exit\n", __func__);
    rtos_set_semaphore(&player->listener_sem);
    player->listener_task = NULL;
    rtos_delete_thread(NULL);
}

/**
 * @brief Initialize listener task
 *
 * @param player Blue audio player handle
 * @param config Blue audio player configuration
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_listener_init(blue_audio_player_handle_t player, blue_audio_player_cfg_t *config)
{
    bk_err_t ret = BK_OK;

    ret = rtos_init_semaphore(&player->listener_sem, 1);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, Create semaphore failed, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    ret = rtos_init_queue(&player->listener_msg_queue,
                        "player_listener_que",
                        sizeof(listener_msg_t),
                        5);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, Create queue failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    ret = audio_create_thread(&player->listener_task,
                            config->task_prio,
                            "blue_audio_listener",
                            (beken_thread_function_t)blue_audio_listener_task_main,
                            config->task_stack,
                            (beken_thread_arg_t)player,
                            config->task_core);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, Create listener task failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    /* Wait for the listener task to initialize */
    ret = rtos_get_semaphore(&player->listener_sem, BEKEN_WAIT_FOREVER);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, Wait listener task init failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    player->listener_running = false;
    return BK_OK;

fail:
    if (player->listener_msg_queue)
    {
        rtos_deinit_queue(&player->listener_msg_queue);
    }
    if (player->listener_sem)
    {
        rtos_deinit_semaphore(&player->listener_sem);
    }
    return BK_FAIL;
}

/**
 * @brief Deinitialize listener task
 *
 * @param player Blue audio player handle
 *
 * @return
 *    - BK_OK: success
 *    - Others: failed
 */
static bk_err_t blue_audio_listener_deinit(blue_audio_player_handle_t player)
{
    bk_err_t ret = BK_OK;

    if (player->listener_task)
    {
        blue_audio_listener_send_msg(player->listener_msg_queue, LISTENER_EXIT, NULL);
        ret = rtos_get_semaphore(&player->listener_sem, BEKEN_WAIT_FOREVER);
        if (kNoErr != ret)
        {
            BK_LOGE(TAG, "%s, %d, Wait listener task exit failed, ret: %d\n", __func__, __LINE__, ret);
        }
        player->listener_task = NULL;
    }

    if (player->listener_msg_queue)
    {
        rtos_deinit_queue(&player->listener_msg_queue);
        player->listener_msg_queue = NULL;
    }

    if (player->listener_sem)
    {
        rtos_deinit_semaphore(&player->listener_sem);
        player->listener_sem = NULL;
    }

    return BK_OK;
}

blue_audio_player_handle_t blue_audio_player_create(blue_audio_player_cfg_t *config)
{
    bk_err_t ret = BK_OK;
    blue_audio_player_handle_t player = NULL;

    BLUE_AUDIO_PLAYER_CHECK_NULL(config, return NULL);

    // Allocate player handle
    player = (blue_audio_player_handle_t)audio_malloc(sizeof(struct blue_audio_player));
    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return NULL);
    memset(player, 0, sizeof(struct blue_audio_player));

    // Initialize player state and type
    player->state = BLUE_AUDIO_PLAYER_STATE_IDLE;
    player->decoder_type = config->decoder_type;
    player->speaker_type = config->speaker_type;
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
    player->eq_en = config->eq_en;
#endif
    player->event_handle = config->event_handle;
    player->args = config->args;
    player->play_threshold = config->play_threshold; // Set play threshold
    player->pipeline_is_running = false; // Initialize pipeline running flag
    STAILQ_INIT(&player->frame_list); // Initialize frame list

    // Initialize pipeline
    ret = blue_audio_pipeline_init(player, config);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Pipeline init failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    // Initialize listener
    ret = blue_audio_listener_init(player, config);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Listener init failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    return player;

fail:
    if (player)
    {
        blue_audio_listener_deinit(player);
        blue_audio_pipeline_deinit(player);
        audio_free(player);
    }
    return NULL;
}

bk_err_t blue_audio_player_destroy(blue_audio_player_handle_t player)
{
    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);

    // Stop player if running
    if (player->state == BLUE_AUDIO_PLAYER_STATE_PLAYING)
    {
        blue_audio_player_stop(player);
    }

    // Deinitialize resources
    blue_audio_listener_deinit(player);
    blue_audio_pipeline_deinit(player);

    // Free player handle
    audio_free(player);
    
    return BK_OK;
}

bk_err_t blue_audio_player_start(blue_audio_player_handle_t player)
{
    bk_err_t ret = BK_OK;

    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);

    if (player->state == BLUE_AUDIO_PLAYER_STATE_PLAYING)
    {
        BK_LOGW(TAG, "%s, %d, Player is already playing\n", __func__, __LINE__);
        return BK_OK;
    }

    // Reset pipeline elements
    audio_pipeline_reset_elements(player->pipeline);
    audio_pipeline_reset_port(player->pipeline);

    // Start pipeline
    ret = blue_audio_pipeline_start(player->pipeline);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Start pipeline failed, ret: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    // Update player state
    player->state = BLUE_AUDIO_PLAYER_STATE_PLAYING;

    // Start listener
    ret = blue_audio_listener_send_msg(player->listener_msg_queue, LISTENER_START, NULL);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Start listener failed, ret: %d\n", __func__, __LINE__, ret);
        blue_audio_pipeline_stop(player->pipeline);
        player->state = BLUE_AUDIO_PLAYER_STATE_IDLE;
        return ret;
    }

    return BK_OK;
}

bk_err_t blue_audio_player_stop(blue_audio_player_handle_t player)
{
    bk_err_t ret = BK_OK;

    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);

    if (player->state != BLUE_AUDIO_PLAYER_STATE_PLAYING)
    {
        BK_LOGW(TAG, "%s, %d, Player is not playing\n", __func__, __LINE__);
        return BK_OK;
    }

    // Stop pipeline
    ret = blue_audio_pipeline_stop(player->pipeline);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Stop pipeline failed, ret: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    // Stop listener
    ret = blue_audio_listener_send_msg(player->listener_msg_queue, LISTENER_IDLE, NULL);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Stop listener failed, ret: %d\n", __func__, __LINE__, ret);
    }

    // Wait listener task idle
    rtos_get_semaphore(&player->listener_sem, BEKEN_WAIT_FOREVER);

    // Reset pipeline running flag
    player->pipeline_is_running = false;

    // Clean up frame list
    blue_audio_frame_info_item_t *frame_item = NULL;
    while ((frame_item = _get_blue_audio_frame_info(&player->frame_list)) != NULL)
    {
        if (frame_item->frame_info.buffer)
        {
            audio_free(frame_item->frame_info.buffer);
        }
        audio_free(frame_item);
    }

    // Update player state
    player->state = BLUE_AUDIO_PLAYER_STATE_STOPPED;

    return BK_OK;
}

bk_err_t blue_audio_player_write_frame_data(blue_audio_player_handle_t player, char *buffer, uint32_t len)
{
    int write_len = 0;

    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);
    BLUE_AUDIO_PLAYER_CHECK_NULL(buffer, return BK_FAIL);

    if (player->state != BLUE_AUDIO_PLAYER_STATE_PLAYING)
    {
        BK_LOGE(TAG, "%s, %d, Player is not in playing state: %d\n", __func__, __LINE__, player->state);
        return BK_FAIL;
    }

    // Check if pipeline is running
    if (player->pipeline_is_running)
    {
        // Pipeline is running, write data directly
        write_len = raw_stream_write(player->raw_strm, buffer, len);
        if (write_len != (int)len)
        {
            BK_LOGE(TAG, "%s, %d, Write data failed, write_len: %d, len: %d\n", __func__, __LINE__, write_len, len);
            return BK_FAIL;
        }
    }
    else
    {
        // Pipeline is not running, cache the data
        bk_err_t ret = _add_blue_audio_frame_to_list(&player->frame_list, buffer, len);
        if (ret != BK_OK)
        {
            BK_LOGE(TAG, "%s, %d, Add frame to list failed\n", __func__, __LINE__);
            return ret;
        }

        // Get the number of frames in the list
        uint32_t frame_count = _get_blue_audio_frame_count(&player->frame_list);

        // Check if the number of frames reaches the play threshold
        if (frame_count >= player->play_threshold)
        {
            // Number of frames reaches threshold, write all cached data to pipeline
            blue_audio_frame_info_item_t *frame_item = NULL;
            while ((frame_item = _get_blue_audio_frame_info(&player->frame_list)) != NULL)
            {
                write_len = raw_stream_write(player->raw_strm, frame_item->frame_info.buffer, frame_item->frame_info.len);
                if (write_len != (int)frame_item->frame_info.len)
                {
                    BK_LOGE(TAG, "%s, %d, Write cached data failed, write_len: %d, len: %d\n", __func__, __LINE__, write_len, frame_item->frame_info.len);
                    // Free the buffer and item
                    audio_free(frame_item->frame_info.buffer);
                    audio_free(frame_item);
                    return BK_FAIL;
                }

                // Free the buffer and item
                audio_free(frame_item->frame_info.buffer);
                audio_free(frame_item);
            }

            // Set pipeline_is_running to true
            player->pipeline_is_running = true;
            BK_LOGD(TAG, "%s, %d, Pipeline started, frame count: %d\n", __func__, __LINE__, frame_count);
        }
    }

    return len;
}

bk_err_t blue_audio_player_get_free_frame_num(blue_audio_player_handle_t player)
{
    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);

    return audio_port_get_free_size(audio_element_get_output_port(player->raw_strm));
}

bk_err_t blue_audio_player_set_volume(blue_audio_player_handle_t player, uint8_t volume)
{
    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);
    bk_err_t ret = BK_FAIL;

    switch (player->speaker_type)
    {
        case BLUE_AUDIO_SPEAKER_TYPE_ONBOARD:
            ret = onboard_speaker_stream_set_digital_gain(player->speaker, volume);
            break;

        case BLUE_AUDIO_SPEAKER_TYPE_UAC:
            //TODO
            BK_LOGW(TAG, "%s, %d, uac not support control volume \n", __func__, __LINE__);
            break;

        case BLUE_AUDIO_SPEAKER_TYPE_I2S:
            //TODO
            BK_LOGW(TAG, "%s, %d, i2s speaker not support \n", __func__, __LINE__);
            break;

        default:
            break;
    }

    return ret;
}

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
bk_err_t blue_audio_player_get_eq_algorithm(blue_audio_player_handle_t player, audio_element_handle_t *eq_alg)
{
    BLUE_AUDIO_PLAYER_CHECK_NULL(player, return BK_FAIL);
    BLUE_AUDIO_PLAYER_CHECK_NULL(eq_alg, return BK_FAIL);

    if (player->eq_en)
    {
        if (player->eq_alg)
        {
            *eq_alg = player->eq_alg;
            return BK_OK;
        }
        else
        {
            BK_LOGE(TAG, "%s, %d, EQ algorithm is NULL\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }
    else
    {
        BK_LOGE(TAG, "%s, %d, EQ is not enabled\n", __func__, __LINE__);
        return BK_FAIL;
    }
}
#endif
