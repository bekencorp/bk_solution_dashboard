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

#include <string.h>
#include <stdlib.h>
#include <components/bk_audio/audio_pipeline/audio_element.h>
#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_pipeline/audio_event_iface.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include <components/bk_audio/audio_pipeline/audio_thread.h>
#include <components/bk_audio/audio_pipeline/audio_error.h>

#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_UAC_MIC
#include <components/bk_audio/audio_streams/uac_mic_stream.h>
#endif

#if CONFIG_ADK_ONBOARD_MIC_STREAM_V2
#include <components/bk_audio/audio_streams/onboard_mic_stream_v2.h>
#else
#include <components/bk_audio/audio_streams/onboard_mic_stream.h>
#endif
#include <components/bk_audio/audio_streams/raw_stream.h>

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
#include <components/bk_audio/audio_algorithms/eq_algorithm.h>
#endif

#include "blue_audio_recorder_service.h"

#define TAG "blue_audio_recorder"

// Macro for null pointer checking
#define BLUE_AUDIO_RECORDER_CHECK_NULL(ptr, act) do {\
        if (ptr == NULL) {\
            BK_LOGE(TAG, "%s, %d, NULL pointer check failed\n", __func__, __LINE__);\
            {act;};\
        }\
    } while(0)

typedef enum
{
    LISTENER_IDLE = 0,
    LISTENER_START,
    LISTENER_EXIT
} listener_op_t;

typedef struct
{
    listener_op_t op;
    void *param;
} listener_msg_t;

struct blue_audio_recorder {
    audio_pipeline_handle_t     pipeline;           /*!< Audio pipeline handle */
    audio_element_handle_t      mic_stream;         /*!< Microphone stream handle */
    audio_element_handle_t      encoder;            /*!< Encoder handle */
    audio_element_handle_t      raw_stream;         /*!< Raw stream handle */
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    bool                        eq_en;              /*!< EQ enable flag */
    audio_element_handle_t      eq_alg;         /*!< EQ algorithm handle */
#endif

    audio_event_iface_handle_t  event_iface;        /*!< Event interface handle */

    blue_audio_recorder_state_t state;              /*!< Recorder state */
    blue_audio_recorder_encoder_type_t encoder_type; /*!< Encoder type */
    blue_audio_recorder_mic_type_t mic_type;        /*!< Microphone type */

    blue_audio_recorder_event_handle_cb event_handle; /*!< Event callback function */
    void *                      args;               /*!< User data for callback */

    beken_thread_t              listener_task;      /*!< Listener task handle */
    beken_queue_t               listener_msg_queue; /*!< Listener message queue */
    beken_semaphore_t           listener_sem;       /*!< Listener semaphore */
    bool                        listener_running;   /*!< Listener running flag */
};


static bk_err_t blue_audio_pipeline_stop(audio_pipeline_handle_t pipeline)
{
    BLUE_AUDIO_RECORDER_CHECK_NULL(pipeline, return BK_FAIL);

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
    BLUE_AUDIO_RECORDER_CHECK_NULL(pipeline, BK_FAIL);

    BK_LOGD(TAG, "%s", __func__);

    if (BK_OK != audio_pipeline_run(pipeline)) {
        BK_LOGE(TAG, "%s, %d, Pipeline run failed", __func__, __LINE__);
        return BK_FAIL;
    }

    return BK_OK;
}

static bk_err_t blue_audio_pipeline_deinit(blue_audio_recorder_handle_t recorder)
{
    BK_LOGD(TAG, "%s", __func__);

    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, BK_FAIL);

    if (!recorder->pipeline) {
        return BK_OK;
    }

    // Stop recording if running
    if (recorder->state == BLUE_AUDIO_RECORDER_STATE_RECORDING) {
        blue_audio_pipeline_stop(recorder->pipeline);
    }

    // Terminate pipeline
    if (BK_OK != audio_pipeline_terminate(recorder->pipeline)) {
        BK_LOGE(TAG, "%s, %d, Pipeline terminate failed", __func__, __LINE__);
        return BK_FAIL;
    }

    // Unregister elements
    if (recorder->mic_stream) {
        if (BK_OK != audio_pipeline_unregister(recorder->pipeline, recorder->mic_stream)) {
            BK_LOGE(TAG, "%s, %d, Unregister mic stream failed", __func__, __LINE__);
            return BK_FAIL;
        }
    }

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    if (recorder->eq_en && recorder->eq_alg)
    {
        if (BK_OK != audio_pipeline_unregister(recorder->pipeline, recorder->eq_alg))
        {
            BK_LOGE(TAG, "%s, %d, Unregister EQ element failed", __func__, __LINE__);
            return BK_FAIL;
        }
    }
#endif

    if (recorder->encoder) {
        if (BK_OK != audio_pipeline_unregister(recorder->pipeline, recorder->encoder)) {
            BK_LOGE(TAG, "%s, %d, Unregister encoder failed", __func__, __LINE__);
            return BK_FAIL;
        }
    }

    if (recorder->raw_stream) {
        if (BK_OK != audio_pipeline_unregister(recorder->pipeline, recorder->raw_stream)) {
            BK_LOGE(TAG, "%s, %d, Unregister raw stream failed", __func__, __LINE__);
            return BK_FAIL;
        }
    }

    // Deinitialize event interface
    if (recorder->event_iface)
    {
        if (BK_OK != audio_pipeline_remove_listener(recorder->pipeline))
        {
            BK_LOGE(TAG, "%s, %d, Remove listener failed\n", __func__, __LINE__);
            return BK_FAIL;
        }

        if (BK_OK != audio_event_iface_destroy(recorder->event_iface))
        {
            BK_LOGE(TAG, "%s, %d, Event interface destroy failed\n", __func__, __LINE__);
            return BK_FAIL;
        }
        recorder->event_iface = NULL;
    }

    // Deinitialize pipeline
    if (BK_OK != audio_pipeline_deinit(recorder->pipeline)) {
        BK_LOGE(TAG, "%s, %d, Pipeline deinit failed", __func__, __LINE__);
        return BK_FAIL;
    }
    recorder->pipeline = NULL;

    // Deinitialize elements
    if (recorder->mic_stream) {
        if (BK_OK != audio_element_deinit(recorder->mic_stream)) {
            BK_LOGE(TAG, "%s, %d, Mic stream deinit failed", __func__, __LINE__);
            return BK_FAIL;
        }
        recorder->mic_stream = NULL;
    }

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    if (recorder->eq_en && recorder->eq_alg)
    {
        if (BK_OK != audio_element_deinit(recorder->eq_alg))
        {
            BK_LOGE(TAG, "%s, %d, EQ element deinit failed", __func__, __LINE__);
            return BK_FAIL;
        }
        recorder->eq_alg = NULL;
    }
#endif

    if (recorder->encoder) {
        if (BK_OK != audio_element_deinit(recorder->encoder)) {
            BK_LOGE(TAG, "%s, %d, Encoder deinit failed", __func__, __LINE__);
            return BK_FAIL;
        }
        recorder->encoder = NULL;
    }

    if (recorder->raw_stream) {
        if (BK_OK != audio_element_deinit(recorder->raw_stream)) {
            BK_LOGE(TAG, "%s, %d, Raw stream deinit failed", __func__, __LINE__);
            return BK_FAIL;
        }
        recorder->raw_stream = NULL;
    }

    return BK_OK;
}

static bk_err_t blue_audio_pipeline_init(blue_audio_recorder_handle_t recorder, blue_audio_recorder_cfg_t *config)
{
    bk_err_t ret = BK_OK;

    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, BK_FAIL);
    BLUE_AUDIO_RECORDER_CHECK_NULL(config, BK_FAIL);

    // Create audio pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    recorder->pipeline = audio_pipeline_init(&pipeline_cfg);
    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder->pipeline, BK_FAIL);

    // Create mic stream based on mic type
    switch (config->mic_type)
    {
        case BLUE_AUDIO_RECORDER_MIC_TYPE_ONBOARD:
            recorder->mic_stream = onboard_mic_stream_init(&config->mic_cfg.ob_mic_cfg);
            break;

            case BLUE_AUDIO_RECORDER_MIC_TYPE_UAC:
#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_UAC_MIC
            recorder->mic_stream = uac_mic_stream_init(&config->mic_cfg.uac_mic_cfg);
#else
            BK_LOGE(TAG, "%s, %d, UAC mic not enable, please config CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_UAC_MIC=y \n", __func__, __LINE__);
            goto fail;
#endif
            break;

        default:
            BK_LOGE(TAG, "%s, %d, Unsupported mic type: %d", __func__, __LINE__, config->mic_type);
            goto fail;
    }
    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder->mic_stream, goto fail);

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    // Create EQ element if enabled
    if (config->eq_en)
    {
        recorder->eq_alg = eq_algorithm_init(&config->eq_cfg.eq_alg_cfg);
        if (!recorder->eq_alg)
        {
            BK_LOGE(TAG, "%s, %d, EQ element init failed", __func__, __LINE__);
            goto fail;
        }
    }
#endif

    // Create encoder based on encoder type
    switch (config->encoder_type)
    {
        case BLUE_AUDIO_RECORDER_ENCODER_TYPE_SBC:
#ifdef CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_MSBC_ENCODER
            recorder->encoder = sbc_enc_init(&config->encoder_cfg.sbc_enc_cfg);
#else
            BK_LOGE(TAG, "%s, %d, SBC encoder not enable, please config CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_MSBC_ENCODER=y \n", __func__, __LINE__);
            goto fail;
#endif
            break;
        case BLUE_AUDIO_RECORDER_ENCODER_TYPE_PCM:
            break;

        default:
            BK_LOGE(TAG, "%s, %d, Unsupported encoder type: %d", __func__, __LINE__, config->encoder_type);
            goto fail;
    }
    if (config->encoder_type != BLUE_AUDIO_RECORDER_ENCODER_TYPE_PCM)
    {
        BLUE_AUDIO_RECORDER_CHECK_NULL(recorder->encoder, goto fail);
    }

    // Create raw stream in read mode
    raw_stream_cfg_t raw_cfg = config->raw_strm_cfg;
    raw_cfg.type = AUDIO_STREAM_READER;
    recorder->raw_stream = raw_stream_init(&raw_cfg);
    if (recorder->raw_stream == NULL)
    {
        BK_LOGE(TAG, "%s, %d, Raw stream init failed", __func__, __LINE__);
        goto fail;
    }

    if (recorder->mic_stream)
    {
        ret = audio_pipeline_register(recorder->pipeline, recorder->mic_stream, "mic_stream");
        if (ret != BK_OK)
        {
            BK_LOGE(TAG, "%s, %d, Register mic stream failed, ret: %d", __func__, __LINE__, ret);
            goto fail;
        }
    }

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    if (recorder->eq_en && recorder->eq_alg)
    {
        ret = audio_pipeline_register(recorder->pipeline, recorder->eq_alg, "eq_element");
        if (ret != BK_OK)
        {
            BK_LOGE(TAG, "%s, %d, Register EQ element failed, ret: %d", __func__, __LINE__, ret);
            goto fail;
        }
    }
#endif

    if (recorder->encoder)
    {
        ret = audio_pipeline_register(recorder->pipeline, recorder->encoder, "encoder");
        if (ret != BK_OK)
        {
            BK_LOGE(TAG, "%s, %d, Register encoder failed, ret: %d", __func__, __LINE__, ret);
            goto fail;
        }
    }

    if (recorder->raw_stream)
    {
        ret = audio_pipeline_register(recorder->pipeline, recorder->raw_stream, "raw_stream");
        if (ret != BK_OK)
        {
            BK_LOGE(TAG, "%s, %d, Register raw stream failed, ret: %d", __func__, __LINE__, ret);
            goto fail;
        }
    }

    // Link pipeline elements based on configuration
    // Data flow: mic_stream -> [eq_element] -> [encoder] -> raw_stream
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    if (recorder->eq_en && recorder->eq_alg)
    {
        if (recorder->encoder)
        {
            // mic_stream -> eq_element -> encoder -> raw_stream
            ret = audio_pipeline_link(recorder->pipeline, (const char *[]) {"mic_stream", "eq_element", "encoder", "raw_stream"}, 4);
            if (ret != BK_OK)
            {
                BK_LOGE(TAG, "%s, %d, Pipeline link failed with EQ and encoder, ret: %d", __func__, __LINE__, ret);
                goto fail;
            }
        }
        else
        {
            // mic_stream -> eq_element -> raw_stream
            ret = audio_pipeline_link(recorder->pipeline, (const char *[]) {"mic_stream", "eq_element", "raw_stream"}, 3);
            if (ret != BK_OK)
            {
                BK_LOGE(TAG, "%s, %d, Pipeline link failed with EQ only, ret: %d", __func__, __LINE__, ret);
                goto fail;
            }
        }
    }
    else
#endif
    {
        if (recorder->encoder)
        {
            // mic_stream -> encoder -> raw_stream
            ret = audio_pipeline_link(recorder->pipeline, (const char *[]) {"mic_stream", "encoder", "raw_stream"}, 3);
            if (ret != BK_OK)
            {
                BK_LOGE(TAG, "%s, %d, Pipeline link failed with encoder, ret: %d", __func__, __LINE__, ret);
                goto fail;
            }
        }
        else
        {
            // mic_stream -> raw_stream
            ret = audio_pipeline_link(recorder->pipeline, (const char *[]) {"mic_stream", "raw_stream"}, 2);
            if (ret != BK_OK)
            {
                BK_LOGE(TAG, "%s, %d, Pipeline link failed without encoder, ret: %d", __func__, __LINE__, ret);
                goto fail;
            }
        }
    }

    // Create event interface
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    recorder->event_iface = audio_event_iface_init(&evt_cfg);
    if (recorder->event_iface == NULL)
    {
        BK_LOGE(TAG, "%s, %d, Event interface init failed", __func__, __LINE__);
        goto fail;
    }

    ret = audio_pipeline_set_listener(recorder->pipeline, recorder->event_iface);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Set event listener failed, ret: %d", __func__, __LINE__, ret);
        goto fail;
    }

    BK_LOGD(TAG, "%s, %d, Blue audio pipeline initialized successfully", __func__, __LINE__);
    return BK_OK;

fail:
    blue_audio_pipeline_deinit(recorder);
    return BK_FAIL;
}

static bk_err_t blue_audio_listener_send_msg(beken_queue_t *queue, listener_op_t op, void *param)
{
    bk_err_t ret;
    listener_msg_t msg;

    if (!queue || !(*queue))
    {
        BK_LOGE(TAG, "%s, %d, NULL queue\n", __func__, __LINE__);
        return BK_FAIL;
    }

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(queue, &msg, BEKEN_NO_WAIT);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, Send message failed, op: %d, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

static void blue_audio_listener_task_main(beken_thread_arg_t param_data)
{
    blue_audio_recorder_handle_t recorder = (blue_audio_recorder_handle_t)param_data;
    bk_err_t ret = BK_OK;

    recorder->listener_running = false;
    long unsigned int wait_time = BEKEN_WAIT_FOREVER;

    rtos_set_semaphore(&recorder->listener_sem);

    while (1)
    {
        listener_msg_t msg;
        ret = rtos_pop_from_queue(&recorder->listener_msg_queue, &msg, wait_time);
        if (BK_OK == ret)
        {
            switch (msg.op)
            {
                case LISTENER_IDLE:
                    rtos_set_semaphore(&recorder->listener_sem);
                    recorder->listener_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case LISTENER_EXIT:
                    goto exit;
                    break;

                case LISTENER_START:
                    recorder->listener_running = true;
                    wait_time = 0;
                    break;

                default:
                    break;
            }
        }

        audio_event_iface_msg_t event_msg;
        audio_element_status_t el_status = AEL_STATUS_NONE;
        if (recorder->listener_running)
        {
            ret = audio_event_iface_listen(recorder->event_iface, &event_msg, 10 / portTICK_RATE_MS);
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
                            if (recorder->state == BLUE_AUDIO_RECORDER_STATE_RECORDING)
                            {
                                BK_LOGW(TAG, "%s, %d, Pipeline error event received, state: %d, ele: %p\n",
                                        __func__, __LINE__, (int)el_status, event_msg.source);

                                // Stop recorder and notify error
                                blue_audio_recorder_stop(recorder);
                                if (recorder->event_handle)
                                {
                                    recorder->event_handle(BLUE_AUDIO_RECORDER_EVENT_ERROR, NULL, recorder->args);
                                }

                                recorder->listener_running = false;
                                wait_time = BEKEN_WAIT_FOREVER;
                                continue;
                            }
                            break;

                        default:
                            break;
                    }
                }
                else
                {
                    BK_LOGW(TAG, "%s, %d, ++>>recorder pipeline event received, state: %d, ele: %p\n", __func__, __LINE__, (int)el_status, event_msg.source);
                    //TODO
                }
            }
        }
    }

 exit:
    BK_LOGD(TAG, "%s, listener task exit\n", __func__);
    rtos_set_semaphore(&recorder->listener_sem);
    recorder->listener_task = NULL;
    rtos_delete_thread(NULL);
}

static bk_err_t blue_audio_listener_init(blue_audio_recorder_handle_t recorder, blue_audio_recorder_cfg_t *config)
{
    bk_err_t ret = BK_OK;

    ret = rtos_init_semaphore(&recorder->listener_sem, 1);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, Create semaphore failed, ret: %d\n", __func__, __LINE__, ret);
        return BK_FAIL;
    }

    ret = rtos_init_queue(&recorder->listener_msg_queue,
                        "recorder_listener_que",
                        sizeof(listener_msg_t),
                        5);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, Create queue failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    ret = audio_create_thread(&recorder->listener_task,
                            config->task_prio,
                            "blue_audio_listener",
                            (beken_thread_function_t)blue_audio_listener_task_main,
                            config->task_stack,
                            (beken_thread_arg_t)recorder,
                            config->task_core);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, Create listener task failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    /* Wait for the listener task to initialize */
    ret = rtos_get_semaphore(&recorder->listener_sem, BEKEN_WAIT_FOREVER);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, Wait listener task init failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    recorder->listener_running = false;
    return BK_OK;

fail:
    if (recorder->listener_msg_queue)
    {
        rtos_deinit_queue(&recorder->listener_msg_queue);
    }
    if (recorder->listener_sem)
    {
        rtos_deinit_semaphore(&recorder->listener_sem);
    }
    return BK_FAIL;
}

static bk_err_t blue_audio_listener_deinit(blue_audio_recorder_handle_t recorder)
{
    bk_err_t ret = BK_OK;

    if (recorder->listener_task)
    {
        blue_audio_listener_send_msg(&recorder->listener_msg_queue, LISTENER_EXIT, NULL);
        ret = rtos_get_semaphore(&recorder->listener_sem, BEKEN_WAIT_FOREVER);
        if (BK_OK != ret)
        {
            BK_LOGE(TAG, "%s, %d, Wait listener task exit failed, ret: %d\n", __func__, __LINE__, ret);
        }
        recorder->listener_task = NULL;
    }

    if (recorder->listener_msg_queue)
    {
        rtos_deinit_queue(&recorder->listener_msg_queue);
        recorder->listener_msg_queue = NULL;
    }

    if (recorder->listener_sem)
    {
        rtos_deinit_semaphore(&recorder->listener_sem);
        recorder->listener_sem = NULL;
    }

    return BK_OK;
}

blue_audio_recorder_handle_t blue_audio_recorder_create(blue_audio_recorder_cfg_t *config)
{
    bk_err_t ret = BK_OK;
    blue_audio_recorder_handle_t recorder = NULL;

    BLUE_AUDIO_RECORDER_CHECK_NULL(config, return NULL);

    // Allocate recorder handle
    recorder = (blue_audio_recorder_handle_t)audio_calloc(1, sizeof(struct blue_audio_recorder));
    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, return NULL);

    // Initialize recorder state and necessary configuration
    recorder->state = BLUE_AUDIO_RECORDER_STATE_IDLE;
    recorder->encoder_type = config->encoder_type;
    recorder->mic_type = config->mic_type;
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
    recorder->eq_en = config->eq_en;
#endif
    recorder->event_handle = config->event_handle;
    recorder->args = config->args;

    // Initialize pipeline
    ret = blue_audio_pipeline_init(recorder, config);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Pipeline init failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    // Initialize listener
    ret = blue_audio_listener_init(recorder, config);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Listener init failed, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    return recorder;

fail:
    if (recorder)
    {
        blue_audio_listener_deinit(recorder);
        blue_audio_pipeline_deinit(recorder);
        audio_free(recorder);
    }
    return NULL;
}

bk_err_t blue_audio_recorder_destroy(blue_audio_recorder_handle_t recorder)
{
    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, return BK_FAIL);

    // Stop player if running
    if (recorder->state == BLUE_AUDIO_RECORDER_STATE_RECORDING)
    {
        blue_audio_recorder_stop(recorder);
    }

    // Deinitialize resources
    blue_audio_listener_deinit(recorder);
    blue_audio_pipeline_deinit(recorder);

    // Free player handle
    audio_free(recorder);

    return BK_OK;
}

bk_err_t blue_audio_recorder_start(blue_audio_recorder_handle_t recorder)
{
    bk_err_t ret = BK_OK;

    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, return BK_FAIL);

    if (recorder->state == BLUE_AUDIO_RECORDER_STATE_RECORDING)
    {
        BK_LOGW(TAG, "%s, %d, Recorder is already recording\n", __func__, __LINE__);
        return BK_OK;
    }

    // Reset pipeline elements
    audio_pipeline_reset_elements(recorder->pipeline);
    audio_pipeline_reset_port(recorder->pipeline);

    // Start pipeline
    ret = blue_audio_pipeline_start(recorder->pipeline);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Start pipeline failed, ret: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    // Update recorder state
    recorder->state = BLUE_AUDIO_RECORDER_STATE_RECORDING;

    // Start listener
    ret = blue_audio_listener_send_msg(&recorder->listener_msg_queue, LISTENER_START, NULL);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Start listener failed, ret: %d\n", __func__, __LINE__, ret);
        blue_audio_pipeline_stop(recorder->pipeline);
        recorder->state = BLUE_AUDIO_RECORDER_STATE_IDLE;
        return ret;
    }

    return BK_OK;
}

bk_err_t blue_audio_recorder_stop(blue_audio_recorder_handle_t recorder)
{
    bk_err_t ret = BK_OK;

    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, return BK_FAIL);

    if (recorder->state != BLUE_AUDIO_RECORDER_STATE_RECORDING)
    {
        BK_LOGW(TAG, "%s, %d, Recorder is not recording\n", __func__, __LINE__);
        return BK_OK;
    }

    // Stop pipeline
    ret = blue_audio_pipeline_stop(recorder->pipeline);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Stop pipeline failed, ret: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    // Stop listener
    ret = blue_audio_listener_send_msg(&recorder->listener_msg_queue, LISTENER_IDLE, NULL);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, Stop listener failed, ret: %d\n", __func__, __LINE__, ret);
    }

    // Wait listener task idle
    rtos_get_semaphore(&recorder->listener_sem, BEKEN_WAIT_FOREVER);

    // Update recorder state
    recorder->state = BLUE_AUDIO_RECORDER_STATE_STOPPED;

    return BK_OK;
}

bk_err_t blue_audio_recorder_read_frame_data(blue_audio_recorder_handle_t recorder, char *buffer, uint32_t len)
{
    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, return BK_FAIL);
    BLUE_AUDIO_RECORDER_CHECK_NULL(buffer, return BK_FAIL);

    if (len == 0)
    {
        BK_LOGE(TAG, "%s, %d, Invalid buffer length: %d", __func__, __LINE__, len);
        return BK_FAIL;
    }

    int read_len = 0;
    // Check recorder state
    if (recorder->state != BLUE_AUDIO_RECORDER_STATE_RECORDING)
    {
        BK_LOGE(TAG, "%s, %d, Recorder is not recording, current state: %d", __func__, __LINE__, recorder->state);
        return BK_FAIL;
    }

    // Read data from raw stream
    read_len = raw_stream_read(recorder->raw_stream, buffer, len);
    if (read_len < 0)
    {
        BK_LOGE(TAG, "%s, %d, Read data failed, read_len: %d", __func__, __LINE__, read_len);
        return read_len;
    }

    BK_LOGV(TAG, "%s, %d, Read %d bytes successfully", __func__, __LINE__, read_len);
    return read_len;
}

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
bk_err_t blue_audio_recorder_get_eq_algorithm(blue_audio_recorder_handle_t recorder, audio_element_handle_t *eq_alg)
{
    BLUE_AUDIO_RECORDER_CHECK_NULL(recorder, return BK_FAIL);
    BLUE_AUDIO_RECORDER_CHECK_NULL(eq_alg, return BK_FAIL);

    if (recorder->eq_en)
    {
        if (recorder->eq_alg)
        {
            *eq_alg = recorder->eq_alg;
            return BK_OK;
        }
        else
        {
            BK_LOGE(TAG, "%s, %d, EQ algorithm is NULL", __func__, __LINE__);
            return BK_FAIL;
        }
    }
    else
    {
        BK_LOGE(TAG, "%s, %d, EQ is not enabled", __func__, __LINE__);
        return BK_FAIL;
    }
}
#endif
