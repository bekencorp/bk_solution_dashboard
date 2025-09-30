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

#include "stdio.h"
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <modules/pm.h>
#include "blue_audio_player_service.h"
#include "cli.h"
#include "test_sbc_array.h"
#include <components/bk_audio/audio_pipeline/audio_element.h>

#define TAG  "blue_audio_player_test"

// Fixed size of each SBC frame data
#define SBC_FRAME_SIZE 119

// Operation type enumeration
typedef enum
{
    BLUE_AUDIO_PLAYER_OP_IDLE = 0,
    BLUE_AUDIO_PLAYER_OP_START,
    BLUE_AUDIO_PLAYER_OP_EXIT,
    BLUE_AUDIO_PLAYER_OP_STOP,
} blue_audio_player_op_t;

// Message structure
typedef struct
{
    blue_audio_player_op_t op;
    void *param;
} blue_audio_player_msg_t;

// Global variables
static blue_audio_player_handle_t gl_blue_audio_player_handle = NULL;
static bool audio_play_running = false;
static beken_thread_t audio_play_task_hdl = NULL;
static beken_semaphore_t audio_play_sem = NULL;
static beken_queue_t blue_audio_player_msg_que = NULL;

/**
 * @brief Send message to queue
 */
static bk_err_t blue_audio_player_send_msg(beken_queue_t *queue, blue_audio_player_op_t op, void *param)
{
    bk_err_t ret;
    blue_audio_player_msg_t msg;

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(queue, &msg, BEKEN_NO_WAIT);
    if (kNoErr != ret)
    {
        BK_LOGE(TAG, "%s, %d, blue_audio_player send message: %d fail, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief Audio player event handling callback function
 */
int blue_audio_player_event_handle_callback(int event, void *args, void *user_data)
{
    BK_LOGD(TAG, "blue_audio_player_event_handle_callback, event: %d, args: %p, user_data: %p\n", event, args, user_data);
    
    switch (event)
    {
        case BLUE_AUDIO_PLAYER_EVENT_MUSIC_INFO:
            {
                audio_element_info_t *music_info = (audio_element_info_t *)args;
                BK_LOGI(TAG, "Playback music info event received, sample_rates: %d, channels: %d, bits: %d\n", 
                        music_info->sample_rates, music_info->channels, music_info->bits);
            }
            break;

        case BLUE_AUDIO_PLAYER_EVENT_FINISH:
            BK_LOGI(TAG, "Playback finished event received\n");
            blue_audio_player_stop(gl_blue_audio_player_handle);
            break;

        case BLUE_AUDIO_PLAYER_EVENT_ERROR:
            BK_LOGE(TAG, "Playback error event received\n");
            blue_audio_player_stop(gl_blue_audio_player_handle);
            break;

        default:
            break;
    }

    return BK_OK;
}

/**
 * @brief Audio playback main task
 */
static void blue_audio_player_main(beken_thread_arg_t param_data)
{
    bk_err_t ret = BK_OK;
    uint32_t sbc_music_read_offset = 0;
    uint32_t sbc_music_size = sizeof(a2dp_sbc_music);
    uint32_t frames_to_play = (uint32_t)param_data; // Number of frames to play, 0 means play all

    // Initialize player configuration
    blue_audio_player_cfg_t config = DEFAULT_BLUE_AUDIO_PLAYER_SBC_ONBOARD_SPK_CONFIG();
    config.event_handle = blue_audio_player_event_handle_callback;
    config.args = NULL;

    // Create audio player
    gl_blue_audio_player_handle = blue_audio_player_create(&config);
    if (!gl_blue_audio_player_handle)
    {
        BK_LOGE(TAG, "create blue_audio_player fail\n");
        goto exit;
    }

    rtos_set_semaphore(&audio_play_sem);

    audio_play_running = false;
    uint32_t wait_time = BEKEN_WAIT_FOREVER;
    uint32_t frames_played = 0;
    
    while (1)
    {
        blue_audio_player_msg_t blue_audio_player_msg;
        ret = rtos_pop_from_queue(&blue_audio_player_msg_que, &blue_audio_player_msg, wait_time);
        if (kNoErr == ret)
        {
            switch (blue_audio_player_msg.op)
            {
                case BLUE_AUDIO_PLAYER_OP_IDLE:
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case BLUE_AUDIO_PLAYER_OP_EXIT:
                    goto exit;
                    break;

                case BLUE_AUDIO_PLAYER_OP_START:
                    // Reset read offset and played frames count
                    sbc_music_read_offset = 0;
                    frames_played = 0;
                    
                    // Start player
                    ret = blue_audio_player_start(gl_blue_audio_player_handle);
                    if (ret != BK_OK)
                    {
                        BK_LOGE(TAG, "start blue_audio_player fail\n");
                        goto exit;
                    }
                    audio_play_running = true;
                    wait_time = 0;
                    break;

                case BLUE_AUDIO_PLAYER_OP_STOP:
                    blue_audio_player_stop(gl_blue_audio_player_handle);
                    sbc_music_read_offset = 0;
                    frames_played = 0;
                    audio_play_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                default:
                    break;
            }
        }

        if (audio_play_running)
        {
            /* Check if there is data to play */
            if (sbc_music_size > sbc_music_read_offset + SBC_FRAME_SIZE)
            {
                // Check if reached specified frame count
                if (frames_to_play > 0 && frames_played >= frames_to_play)
                {
                    BK_LOGI(TAG, "Reached specified frame count: %d\n", frames_to_play);
                    blue_audio_player_stop(gl_blue_audio_player_handle);
                    audio_play_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    continue;
                }
                
                // Write one frame of SBC data
                ret = blue_audio_player_write_frame_data(
                    gl_blue_audio_player_handle, 
                    (char *)(a2dp_sbc_music + sbc_music_read_offset), 
                    SBC_FRAME_SIZE
                );
                
                if (ret == SBC_FRAME_SIZE)
                {
                    sbc_music_read_offset += SBC_FRAME_SIZE;
                    frames_played++;
                    
                    // Print information every 100 frames played
                    if (frames_played % 100 == 0)
                    {
                        BK_LOGD(TAG, "Played %d frames, offset: %d\n", frames_played, sbc_music_read_offset);
                    }
                }
                else
                {
                    BK_LOGE(TAG, "write sbc frame data fail, ret = %d\n", ret);
                    audio_play_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                }
            }
            else
            {
                BK_LOGD(TAG, "SBC music is empty, played all %d frames\n", frames_played);
                /* Stop playing and wait for other messages */
                blue_audio_player_stop(gl_blue_audio_player_handle);
                audio_play_running = false;
                wait_time = BEKEN_WAIT_FOREVER;
            }
        }
    }

exit:
    audio_play_running = false;

    blue_audio_player_stop(gl_blue_audio_player_handle);
    blue_audio_player_destroy(gl_blue_audio_player_handle);
    gl_blue_audio_player_handle = NULL;

    rtos_deinit_queue(&blue_audio_player_msg_que);
    blue_audio_player_msg_que = NULL;

    rtos_set_semaphore(&audio_play_sem);

    audio_play_task_hdl = NULL;
    rtos_delete_thread(NULL);
}

/**
 * @brief Initialize audio player test
 */
static bk_err_t blue_audio_player_init(uint32_t frames_to_play)
{
    bk_err_t ret = BK_OK;

    ret = rtos_init_semaphore(&audio_play_sem, 1);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, create semaphore fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    ret = rtos_init_queue(&blue_audio_player_msg_que,
                          "blue_audio_player_msg_que",
                          sizeof(blue_audio_player_msg_t),
                          5);
    if (ret != kNoErr)
    {
        BK_LOGE(TAG, "%s, %d, create blue audio player message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_create_thread(&audio_play_task_hdl,
                             (BEKEN_DEFAULT_WORKER_PRIORITY - 1),
                             "blue_audio_play",
                             (beken_thread_function_t)blue_audio_player_main,
                             2048 * 2,
                             (beken_thread_arg_t)frames_to_play);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, create blue audio player task fail\n", __func__, __LINE__);
        goto fail;
    }

    rtos_get_semaphore(&audio_play_sem, BEKEN_NEVER_TIMEOUT);

    return BK_OK;

fail:
    if (audio_play_sem)
    {
        rtos_deinit_semaphore(&audio_play_sem);
        audio_play_sem = NULL;
    }

    if (blue_audio_player_msg_que)
    {
        rtos_deinit_queue(&blue_audio_player_msg_que);
        blue_audio_player_msg_que = NULL;
    }

    return BK_FAIL;
}

/**
 * @brief CLI command processing function
 */
void cli_blue_audio_player_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2)
    {
        BK_LOGI(TAG, "blue_audio_player ...\n");
        return;
    }

    if (os_strcmp(argv[1], "init") == 0)
    {
        uint32_t frames_to_play = 0; // Default to play all
        if (argc >= 3)
        {
            frames_to_play = os_strtoul((char *)argv[2], NULL, 10);
        }
        
        if (BK_OK != blue_audio_player_init(frames_to_play))
        {
            BK_LOGE(TAG, "init blue audio player fail\n");
        }
    }
    else if (os_strcmp(argv[1], "start") == 0)
    {
        blue_audio_player_send_msg(&blue_audio_player_msg_que, BLUE_AUDIO_PLAYER_OP_START, NULL);
    }
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        blue_audio_player_send_msg(&blue_audio_player_msg_que, BLUE_AUDIO_PLAYER_OP_STOP, NULL);
    }
    else if (os_strcmp(argv[1], "deinit") == 0)
    {
        blue_audio_player_send_msg(&blue_audio_player_msg_que, BLUE_AUDIO_PLAYER_OP_EXIT, NULL);

        rtos_get_semaphore(&audio_play_sem, BEKEN_NEVER_TIMEOUT);

        rtos_deinit_semaphore(&audio_play_sem);
        audio_play_sem = NULL;
    }
    else if (os_strcmp(argv[1], "api") == 0)
    {
        if (argc < 3)
        {
            BK_LOGI(TAG, "blue_audio_player api ...\n");
            return;
        }

        if (os_strcmp(argv[2], "create") == 0)
        {
            blue_audio_player_cfg_t cfg = DEFAULT_BLUE_AUDIO_PLAYER_SBC_ONBOARD_SPK_CONFIG();
            gl_blue_audio_player_handle = blue_audio_player_create(&cfg);
            if (gl_blue_audio_player_handle == NULL)
            {
                BK_LOGE(TAG, "create blue audio player fail\n");
            }
        }
        else if (os_strcmp(argv[2], "start") == 0)
        {
            if (gl_blue_audio_player_handle)
            {
                blue_audio_player_start(gl_blue_audio_player_handle);
            }
            else
            {
                BK_LOGE(TAG, "player handle is NULL, please create first\n");
            }
        }
        else if (os_strcmp(argv[2], "stop") == 0)
        {
            if (gl_blue_audio_player_handle)
            {
                blue_audio_player_stop(gl_blue_audio_player_handle);
            }
            else
            {
                BK_LOGE(TAG, "player handle is NULL\n");
            }
        }
        else if (os_strcmp(argv[2], "destroy") == 0)
        {
            if (gl_blue_audio_player_handle)
            {
                blue_audio_player_destroy(gl_blue_audio_player_handle);
                gl_blue_audio_player_handle = NULL;
            }
        }
        else if (os_strcmp(argv[2], "write") == 0)
        {
            if (gl_blue_audio_player_handle)
            {
                uint32_t frame_count = 10; // Default to write 10 frames
                if (argc >= 4)
                {
                    frame_count = os_strtoul((char *)argv[3], NULL, 10);
                }
                
                for (uint32_t i = 0; i < frame_count; i++)
                {
                    uint32_t offset = i * SBC_FRAME_SIZE;
                    if (offset + SBC_FRAME_SIZE <= sizeof(a2dp_sbc_music))
                    {
                        blue_audio_player_write_frame_data(
                            gl_blue_audio_player_handle, 
                            (char *)(a2dp_sbc_music + offset), 
                            SBC_FRAME_SIZE
                        );
                    }
                    else
                    {
                        BK_LOGW(TAG, "Reached end of SBC data after %d frames\n", i);
                        break;
                    }
                }
            }
            else
            {
                BK_LOGE(TAG, "player handle is NULL, please create first\n");
            }
        }
        else
        {
            BK_LOGI(TAG, "blue_audio_player api ...\n");
        }
    }
    else if (os_strcmp(argv[1], "help") == 0)
    {
        BK_LOGI(TAG, "blue_audio_player commands:\n");
        BK_LOGI(TAG, "  init [frames] - Initialize blue audio player, optional frames to play\n");
        BK_LOGI(TAG, "  start - Start playing SBC audio\n");
        BK_LOGI(TAG, "  stop - Stop playing\n");
        BK_LOGI(TAG, "  deinit - Deinitialize blue audio player\n");
        BK_LOGI(TAG, "  api create - Create player handle\n");
        BK_LOGI(TAG, "  api start - Start player\n");
        BK_LOGI(TAG, "  api stop - Stop player\n");
        BK_LOGI(TAG, "  api destroy - Destroy player handle\n");
        BK_LOGI(TAG, "  api write [frames] - Write SBC frames to player\n");
    }
}

#define BLUE_AUDIO_PLAYER_CMD_CNT  (sizeof(s_blue_audio_player_commands) / sizeof(struct cli_command))
static const struct cli_command s_blue_audio_player_commands[] =
{
    {"blue_audio_player", "blue_audio_player ...", cli_blue_audio_player_cmd},
};

/**
 * @brief CLI command initialization function
 */
int cli_blue_audio_player_init(void)
{
    BK_LOGI(TAG, "cli_blue_audio_player_init\n");

    return cli_register_commands(s_blue_audio_player_commands, BLUE_AUDIO_PLAYER_CMD_CNT);
}
