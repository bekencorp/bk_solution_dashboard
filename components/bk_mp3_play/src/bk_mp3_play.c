// Copyright 2025-2026 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//	   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include <components/bk_audio/audio_pipeline/audio_thread.h>
#include <components/bk_audio/audio_streams/onboard_speaker_stream.h>
#include <components/bk_audio/audio_streams/raw_stream.h>
#include <components/bk_audio/audio_decoders/mp3_decoder.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include "bk_mp3_play.h"

#define TAG "mp3_play"

#define MP3_PLAY_CHECK_NULL(ptr, act) do {\
        if (ptr == NULL) {\
            BK_LOGE(TAG, "%s, %d, MP3_PLAY_CHECK_NULL fail \n", __func__, __LINE__);\
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

struct mp3_play
{
    audio_pipeline_handle_t     play_pipeline;          /**< play pipeline handle, [raw_stream]-->[mp3_decoder]-->[onboard_speaker_stream] */
    audio_element_handle_t      raw_strm;               /**< raw_stream handle, used to write data to decoder */
    audio_element_handle_t      mp3_dec;                /**< mp3_decoder handle */
    audio_element_handle_t      ob_spk_strm;            /**< onboard_speaker_stream handle */

    audio_event_iface_handle_t  play_evt;               /**< play listener event handle */

    bk_mp3_play_state_t         state;                  /**< player state */

    mp3_play_event_handle_cb    event_handle;           /**< player event handle callback */
    void *                      args;                   /**< the parameter of event_handle func */

    beken_thread_t              listener_task_hdl;      /**< listener task handle */
    beken_queue_t               listener_msg_que;       /**< listener message queue */
    beken_semaphore_t           listener_sem;           /**< listener semaphore */
    bool                        listener_is_running;    /**< listener is running */
};


static bk_err_t play_pipeline_start(audio_pipeline_handle_t play_pipeline)
{
    MP3_PLAY_CHECK_NULL(play_pipeline, return BK_FAIL);

    if (BK_OK != audio_pipeline_run(play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline run fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    return BK_OK;
}

static bk_err_t play_pipeline_stop(audio_pipeline_handle_t play_pipeline)
{
    MP3_PLAY_CHECK_NULL(play_pipeline, return BK_FAIL);

    BK_LOGD(TAG, "%s\n", __func__);

    if (BK_OK != audio_pipeline_stop(play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline stop fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (BK_OK != audio_pipeline_wait_for_stop(play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline wait stop fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    return BK_OK;
}

static bk_err_t play_pipeline_deinit(bk_mp3_play_handle_t play_handle)
{
    BK_LOGD(TAG, "%s\n", __func__);

    MP3_PLAY_CHECK_NULL(play_handle, return BK_FAIL);

    if (!play_handle->play_pipeline)
    {
        return BK_OK;
    }

    //audio_element_set_input_timeout(player_handle->spk_dec, 0);

    if (play_handle->state == MP3_PLAY_STATE_PLAYING)
    {
        play_pipeline_stop(play_handle->play_pipeline);
    }

    if (BK_OK != audio_pipeline_terminate(play_handle->play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline terminate fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (BK_OK != audio_pipeline_unregister(play_handle->play_pipeline, play_handle->raw_strm))
    {
        BK_LOGE(TAG, "%s, %d, unregister raw_write stream fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (BK_OK != audio_pipeline_unregister(play_handle->play_pipeline, play_handle->mp3_dec))
    {
        BK_LOGE(TAG, "%s, %d, unregister mp3_decoder fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (BK_OK != audio_pipeline_unregister(play_handle->play_pipeline, play_handle->ob_spk_strm))
    {
        BK_LOGE(TAG, "%s, %d, unregister onboard_speaker_stream fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    if (play_handle->play_evt)
    {
        /* deinit listener */
        if (BK_OK != audio_pipeline_remove_listener(play_handle->play_pipeline))
        {
            BK_LOGE(TAG, "%s, %d, remove listener fail\n", __func__, __LINE__);
            return BK_FAIL;
        }

        if (BK_OK != audio_event_iface_destroy(play_handle->play_evt))
        {
            BK_LOGE(TAG, "%s, %d, listener event destroy fail\n", __func__, __LINE__);
            return BK_FAIL;
        }
    }

    if (BK_OK != audio_pipeline_deinit(play_handle->play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline deinit fail\n", __func__, __LINE__);
        return BK_FAIL;
    }
    else
    {
        play_handle->play_pipeline = NULL;
    }

    if (BK_OK != audio_element_deinit(play_handle->raw_strm))
    {
        BK_LOGE(TAG, "%s, %d, raw_write stream deinit fail\n", __func__, __LINE__);
        return BK_FAIL;
    }
    else
    {
        play_handle->raw_strm = NULL;
    }

    if (BK_OK != audio_element_deinit(play_handle->mp3_dec))
    {
        BK_LOGE(TAG, "%s, %d, mp3_decoder deinit fail\n", __func__, __LINE__);
        return BK_FAIL;
    }
    else
    {
        play_handle->mp3_dec = NULL;
    }

    if (BK_OK != audio_element_deinit(play_handle->ob_spk_strm))
    {
        BK_LOGE(TAG, "%s, %d, onboard_speaker_stream deinit fail\n", __func__, __LINE__);
        return BK_FAIL;
    }
    else
    {
        play_handle->ob_spk_strm = NULL;
    }

    return BK_OK;
}

static bk_err_t play_pipeline_init(bk_mp3_play_handle_t play_handle, bk_mp3_play_cfg_t *cfg)
{
    bk_err_t ret = BK_OK;

    BK_LOGD(TAG, "%s\n", __func__);
    MP3_PLAY_CHECK_NULL(play_handle, return BK_FAIL);

    BK_LOGD(TAG, "step1: play pipeline init\n");
    audio_pipeline_cfg_t play_pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    play_handle->play_pipeline = audio_pipeline_init(&play_pipeline_cfg);
    MP3_PLAY_CHECK_NULL(play_handle->play_pipeline, return BK_FAIL);

    BK_LOGD(TAG, "step2: init play elements\n");
    raw_stream_cfg_t raw_write_cfg = DEFAULT_RAW_STREAM_CONFIG();
    raw_write_cfg.type = AUDIO_STREAM_WRITER;
    raw_write_cfg.out_block_size = cfg->pool_size;
    play_handle->raw_strm = raw_stream_init(&raw_write_cfg);
    MP3_PLAY_CHECK_NULL(play_handle->raw_strm, goto fail);

    play_handle->mp3_dec = mp3_decoder_init(&cfg->mp3_dec_cfg);
    MP3_PLAY_CHECK_NULL(play_handle->mp3_dec, goto fail);

    play_handle->ob_spk_strm = onboard_speaker_stream_init(&cfg->ob_spk_cfg);
    MP3_PLAY_CHECK_NULL(play_handle->ob_spk_strm, goto fail);

    BK_LOGD(TAG, "step3: play elements register\n");
    if (BK_OK != audio_pipeline_register(play_handle->play_pipeline, play_handle->raw_strm, "raw_strm"))
    {
        BK_LOGE(TAG, "%s, %d, register raw_stream stream fail", __func__, __LINE__);
        goto fail;
    }

    if (BK_OK != audio_pipeline_register(play_handle->play_pipeline, play_handle->mp3_dec, "mp3_dec"))
    {
        BK_LOGE(TAG, "%s, %d, register mp3_decoder fail", __func__, __LINE__);
        goto fail;
    }

    if (BK_OK != audio_pipeline_register(play_handle->play_pipeline, play_handle->ob_spk_strm, "ob_spk_strm"))
    {
        BK_LOGE(TAG, "%s, %d, register onboard_speaker_stream fail", __func__, __LINE__);
        goto fail;
    }

    BK_LOGD(TAG, "step4: play pipeline link\n");
    ret = audio_pipeline_link(play_handle->play_pipeline, (const char *[]){"raw_strm", "mp3_dec", "ob_spk_strm"}, 3);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline link fail, ret: %d\n", __func__, __LINE__, ret);
        goto fail;
    }

    BK_LOGD(TAG, "step5: init play event listener\n");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    play_handle->play_evt = audio_event_iface_init(&evt_cfg);
    if (play_handle->play_evt == NULL)
    {
        BK_LOGE(TAG, "%s, %d, play_event init fail\n", __func__, __LINE__);
        goto fail;
    }

    if (BK_OK != audio_pipeline_set_listener(play_handle->play_pipeline, play_handle->play_evt))
    {
        BK_LOGE(TAG, "%s, %d, init play pipeline listener fail\n", __func__, __LINE__);
        goto fail;
    }

    return BK_OK;

fail:
    play_pipeline_deinit(play_handle);

    return BK_FAIL;
}

static bk_err_t listener_send_msg(beken_queue_t queue, listener_op_t op, void *param)
{
    bk_err_t ret;
    listener_msg_t msg;

    if (!queue)
    {
        BK_LOGE(TAG, "%s, %d, queue: %p \n", __func__, __LINE__, queue);
        return BK_FAIL;
    }

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(&queue, &msg, BEKEN_NO_WAIT);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, listener send message: %d fail, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

static void listener_task_main(beken_thread_arg_t param_data)
{
    bk_mp3_play_handle_t play_handle = (bk_mp3_play_handle_t)param_data;
    bk_err_t ret = BK_OK;

    play_handle->listener_is_running = false;
    long unsigned int wait_time = BEKEN_WAIT_FOREVER;

    rtos_set_semaphore(&play_handle->listener_sem);

    while (1)
    {
        listener_msg_t listener_msg;
        ret = rtos_pop_from_queue(&play_handle->listener_msg_que, &listener_msg, wait_time);
        if (kNoErr == ret)
        {
            switch (listener_msg.op)
            {
                case LISTENER_IDLE:
                    play_handle->listener_is_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case LISTENER_EXIT:
                    goto exit;
                    break;

                case LISTENER_START:
                    play_handle->listener_is_running = true;
                    wait_time = 0;
                    break;

                default:
                    break;
            }
        }

        audio_event_iface_msg_t event_msg;
        audio_element_status_t el_status = AEL_STATUS_NONE;
        if (play_handle->listener_is_running)
        {
            ret = audio_event_iface_listen(play_handle->play_evt, &event_msg, 10 / portTICK_RATE_MS);//portMAX_DELAY
            if (ret == BK_OK)
            {
                //BK_LOGW(TAG, "%s, %d, ++>>play pipeline event received, state: %d, ele: %p, player state: %d\n", __func__, __LINE__, (int)event_msg.data, event_msg.source, play_handle->state);
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
                            if (play_handle->state == MP3_PLAY_STATE_PLAYING)
                            {
                                BK_LOGW(TAG, "%s, %d, ++>>record pipeline event received, state: %d, ele: %p\n", __func__, __LINE__, (int)event_msg.data, event_msg.source);
                                /* stop voice pipeline */
                                bk_mp3_play_stop(play_handle);
                                //audio_pipeline_reset_port(play_handle->play_pipeline);
                                //audio_pipeline_reset_elements(play_handle->play_pipeline);
                                //audio_pipeline_change_state(play_handle->play_pipeline, AEL_STATE_INIT);
                                /* stop listener */
                                play_handle->listener_is_running = false;
                                wait_time = BEKEN_WAIT_FOREVER;
                                continue;
                            }
                            break;

                        case AEL_STATUS_STATE_STOPPED:
                        case AEL_STATUS_STATE_FINISHED:
                            //BK_LOGW(TAG, "%s, %d, ++>>play pipeline event received, state: %d, ele: %p\n", __func__, __LINE__, (int)event_msg.data, event_msg.source);
                            /* Stop the player when receiving a finish status report from the speaker stream */
                            if (el_status == AEL_STATUS_STATE_FINISHED && event_msg.source == play_handle->ob_spk_strm && play_handle->state == MP3_PLAY_STATE_PLAYING)
                            {
                                BK_LOGW(TAG, "%s, %d, ++>>play pipeline event received, state: %d, ele: %p\n", __func__, __LINE__, (int)event_msg.data, event_msg.source);
                                /* stop play pipeline */
                                bk_mp3_play_stop(play_handle);
                                //audio_pipeline_reset_port(play_handle->play_pipeline);
                                //audio_pipeline_reset_elements(play_handle->play_pipeline);
                                //audio_pipeline_change_state(play_handle->play_pipeline, AEL_STATE_INIT);
                                if (play_handle->event_handle)
                                {
                                    play_handle->event_handle(MP3_PLAY_EVENT_FINISH, NULL, play_handle->args);
                                }
                                /* stop listener */
                                play_handle->listener_is_running = false;
                                wait_time = BEKEN_WAIT_FOREVER;
                                continue;
                            }
                            break;

                        default:
                            break;
                    }
                }
                else if (event_msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
                {
                    audio_element_info_t music_info = {0};
                    audio_element_getinfo(play_handle->mp3_dec, &music_info);
                    BK_LOGD(TAG, "[ * ] Receive music info from spk decoder, sample_rates=%d, bits=%d, ch=%d \n", music_info.sample_rates, music_info.bits, music_info.channels);

                        //audio_element_setinfo(player_handle->spk_str, &music_info);
                        onboard_speaker_stream_set_param(play_handle->ob_spk_strm, music_info.sample_rates, music_info.bits, music_info.channels);
#if 0
                        if (player_handle->event_handle)
                        {
                            player_handle->event_handle(MP3_PLAY_EVENT_MUSIC_INFO,  &music_info, player_handle->args);
                        }
#endif
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

    if (play_handle->listener_msg_que)
    {
        rtos_deinit_queue(&play_handle->listener_msg_que);
        play_handle->listener_msg_que = NULL;
    }

    /* delete task */
    play_handle->listener_task_hdl = NULL;

    rtos_set_semaphore(&play_handle->listener_sem);

    rtos_delete_thread(NULL);
}

static bk_err_t listener_init(bk_mp3_play_handle_t mp3_play)
{
    bk_err_t ret = BK_OK;

    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    ret = rtos_init_semaphore(&mp3_play->listener_sem, 1);
    if (ret != kNoErr)
    {
        BK_LOGE(TAG, "%s, %d, ceate listener semaphore fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_init_queue(&mp3_play->listener_msg_que,
                          "player_listener_que",
                          sizeof(listener_msg_t),
                          5);
    if (ret != kNoErr)
    {
        BK_LOGE(TAG, "%s, %d, ceate play listener message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = audio_create_thread(&mp3_play->listener_task_hdl,
                             BEKEN_DEFAULT_WORKER_PRIORITY - 1,
                             "play_listener",
                             (beken_thread_function_t)listener_task_main,
                             2048,
                             (beken_thread_arg_t)mp3_play,
                             1);
    if (ret != kNoErr)
    {
        BK_LOGE(TAG, "%s, %d, create mp3 play listener task fail\n", __func__, __LINE__);
        goto fail;
    }

    rtos_get_semaphore(&mp3_play->listener_sem, BEKEN_NEVER_TIMEOUT);

    BK_LOGD(TAG, "init mp3 play listener task complete\n");

    return BK_OK;

fail:

    if (mp3_play->listener_sem)
    {
        rtos_deinit_semaphore(&mp3_play->listener_sem);
        mp3_play->listener_sem = NULL;
    }

    if (mp3_play->listener_msg_que)
    {
        rtos_deinit_queue(&mp3_play->listener_msg_que);
        mp3_play->listener_msg_que = NULL;
    }

    mp3_play->listener_task_hdl = NULL;

    return BK_FAIL;
}

static bk_err_t listener_deinit(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    BK_LOGD(TAG, "%s\n", __func__);

    if (BK_OK != listener_send_msg(mp3_play->listener_msg_que, LISTENER_EXIT, NULL))
    {
        return BK_FAIL;
    }

    rtos_get_semaphore(&mp3_play->listener_sem, BEKEN_NEVER_TIMEOUT);

    rtos_deinit_semaphore(&mp3_play->listener_sem);
    mp3_play->listener_sem = NULL;

    BK_LOGD(TAG, "deinit mp3 play listener complete\n");

    return BK_OK;
}

static bk_err_t listener_start(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    BK_LOGD(TAG, "%s\n", __func__);

    bk_err_t ret = listener_send_msg(mp3_play->listener_msg_que, LISTENER_START, NULL);
    if (ret != BK_OK)
    {
        return BK_FAIL;
    }

    return BK_OK;
}

static bk_err_t listener_stop(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    BK_LOGD(TAG, "%s\n", __func__);

    bk_err_t ret = listener_send_msg(mp3_play->listener_msg_que, LISTENER_IDLE, NULL);
    if (ret != BK_OK)
    {
        return BK_FAIL;
    }

    return BK_OK;
}

bk_mp3_play_handle_t bk_mp3_play_create(bk_mp3_play_cfg_t *cfg)
{
    bk_mp3_play_handle_t mp3_play_handle = (bk_mp3_play_handle_t)psram_malloc(sizeof(struct mp3_play));
    MP3_PLAY_CHECK_NULL(mp3_play_handle, return NULL);
    os_memset(mp3_play_handle, 0, sizeof(struct mp3_play));

    /* copy config */
    mp3_play_handle->event_handle = cfg->event_handle;
    mp3_play_handle->args = cfg->args;

    if (BK_OK != play_pipeline_init(mp3_play_handle, cfg))
    {
        BK_LOGE(TAG, "%s, %d, play pipeline init fail\n", __func__, __LINE__);
        goto fail;
    }

    if (BK_OK != listener_init(mp3_play_handle))
    {
        BK_LOGE(TAG, "%s, %d, player listener init fail\n", __func__, __LINE__);
        goto fail;
    }

    //bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_480M);
    //bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_AUDP, 0, 0);

    mp3_play_handle->state = MP3_PLAY_STATE_IDLE;

    return mp3_play_handle;

fail:


    if (mp3_play_handle->listener_task_hdl)
    {
        listener_deinit(mp3_play_handle);
    }

    play_pipeline_deinit(mp3_play_handle);

    os_free(mp3_play_handle);

    //bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_DEFAULT);
    //bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_AUDP, 1, 0);

    return NULL;
}

bk_err_t bk_mp3_play_destroy(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    BK_LOGD(TAG, "%s\n", __func__);

    if (mp3_play->state == MP3_PLAY_STATE_PLAYING)
    {
        bk_mp3_play_stop(mp3_play);
    }

    //listener_stop(mp3_play);

    play_pipeline_deinit(mp3_play);
    BK_LOGD(TAG, "%s, play_pipeline deinit complete\n", __func__);

    listener_deinit(mp3_play);

    mp3_play->state = MP3_PLAY_STATE_NONE;

    os_free(mp3_play);

    //bk_pm_module_vote_cpu_freq(PM_DEV_ID_AUDIO, PM_CPU_FRQ_DEFAULT);
    //bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_AUDP, 1, 0);

    return BK_OK;
}

bk_err_t bk_mp3_play_start(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    if (mp3_play->state == MP3_PLAY_STATE_PLAYING)
    {
        return BK_OK;
    }

    BK_LOGD(TAG, "%s\n", __func__);

    if (mp3_play->state == MP3_PLAY_STATE_NONE)
    {
        BK_LOGE(TAG, "%s, %d, player state: %d is error\n", __func__, __LINE__, mp3_play->state);
        return BK_FAIL;
    }

    listener_start(mp3_play);

    if (BK_OK != play_pipeline_start(mp3_play->play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline run fail\n", __func__, __LINE__);
        goto fail;
    }

    mp3_play->state = MP3_PLAY_STATE_PLAYING;

    return BK_OK;

fail:

    listener_stop(mp3_play);

    play_pipeline_stop(mp3_play->play_pipeline);

    return BK_FAIL;
}

bk_err_t bk_mp3_play_stop(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    BK_LOGD(TAG, "%s, STATE: %d\n", __func__, mp3_play->state);
    if (mp3_play->state == MP3_PLAY_STATE_NONE || mp3_play->state == MP3_PLAY_STATE_IDLE || mp3_play->state == MP3_PLAY_STATE_STOPED)
    {
        return BK_OK;
    }

    BK_LOGD(TAG, "%s\n", __func__);

    listener_stop(mp3_play);

    if (BK_OK != play_pipeline_stop(mp3_play->play_pipeline))
    {
        BK_LOGE(TAG, "%s, %d, play_pipeline stop fail\n", __func__, __LINE__);
    }

    audio_pipeline_reset_port(mp3_play->play_pipeline);
    audio_pipeline_reset_elements(mp3_play->play_pipeline);
    audio_pipeline_change_state(mp3_play->play_pipeline, AEL_STATE_INIT);

    mp3_play->state = MP3_PLAY_STATE_STOPED;

    return BK_OK;
}

bk_err_t bk_mp3_play_write_data(bk_mp3_play_handle_t mp3_play, char *buffer, uint32_t len)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);
    MP3_PLAY_CHECK_NULL(buffer, return BK_FAIL);

    return raw_stream_write(mp3_play->raw_strm, buffer, len);
}

bk_err_t bk_mp3_play_write_data_done(bk_mp3_play_handle_t mp3_play)
{
    MP3_PLAY_CHECK_NULL(mp3_play, return BK_FAIL);

    return audio_element_set_port_done(mp3_play->raw_strm);
}
