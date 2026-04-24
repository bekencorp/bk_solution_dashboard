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
#include "blue_audio_recorder_service.h"
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include "cli.h"

#define TAG  "blue_audio_recorder_test"

// Default frame size for recording
#define RECORDER_DEFAULT_FRAME_SIZE 512

// Operation type enumeration
typedef enum
{
    BLUE_AUDIO_RECORDER_OP_IDLE = 0,
    BLUE_AUDIO_RECORDER_OP_START,
    BLUE_AUDIO_RECORDER_OP_EXIT,
    BLUE_AUDIO_RECORDER_OP_STOP,
} blue_audio_recorder_op_t;

// Message structure
typedef struct
{
    blue_audio_recorder_op_t op;
    void *param;
} blue_audio_recorder_msg_t;

// Global variables
static blue_audio_recorder_handle_t gl_blue_audio_recorder_handle = NULL;
static bool audio_record_running = false;
static beken_thread_t audio_record_task_hdl = NULL;
static beken_semaphore_t audio_record_sem = NULL;
static beken_queue_t blue_audio_recorder_msg_que = NULL;
static uint8_t *recording_buffer = NULL;
static uint32_t frames_to_record = 0; // Number of frames to record, 0 means record until stopped

/**
 * @brief Send message to queue
 */
static bk_err_t blue_audio_recorder_send_msg(beken_queue_t *queue, blue_audio_recorder_op_t op, void *param)
{
    bk_err_t ret;
    blue_audio_recorder_msg_t msg;

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(queue, &msg, BEKEN_NO_WAIT);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, blue_audio_recorder send message: %d fail, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief Audio recorder event handling callback function
 */
int blue_audio_recorder_event_handle_callback(int event, void *args, void *user_data)
{
    BK_LOGD(TAG, "blue_audio_recorder_event_handle_callback, event: %d, args: %p, user_data: %p\n", event, args, user_data);
    
    switch (event)
    {
        case BLUE_AUDIO_RECORDER_EVENT_ERROR:
            BK_LOGE(TAG, "Recording error event received\n");
            blue_audio_recorder_stop(gl_blue_audio_recorder_handle);
            break;

        case BLUE_AUDIO_RECORDER_EVENT_START:
            BK_LOGI(TAG, "Recording started event received\n");
            break;

        case BLUE_AUDIO_RECORDER_EVENT_STOP:
            BK_LOGI(TAG, "Recording stopped event received\n");
            break;

        default:
            break;
    }

    return BK_OK;
}

/**
 * @brief Audio recording main task
 */
static void blue_audio_recorder_main(beken_thread_arg_t param_data)
{
    bk_err_t ret = BK_OK;
    uint32_t frames_recorded = 0;

    // Initialize recorder configuration
    blue_audio_recorder_cfg_t config = DEFAULT_BLUE_AUDIO_RECORDER_PCM_ONBOARD_MIC_CONFIG();
    config.event_handle = blue_audio_recorder_event_handle_callback;
    config.args = NULL;

    // Allocate recording buffer
    recording_buffer = (uint8_t *)audio_malloc(RECORDER_DEFAULT_FRAME_SIZE);
    if (recording_buffer == NULL)
    {
        BK_LOGE(TAG, "Allocate recording buffer failed\n");
        goto exit;
    }

    // Create audio recorder
    gl_blue_audio_recorder_handle = blue_audio_recorder_create(&config);
    if (!gl_blue_audio_recorder_handle)
    {
        BK_LOGE(TAG, "create blue_audio_recorder fail\n");
        goto exit;
    }

    rtos_set_semaphore(&audio_record_sem);

    audio_record_running = false;
    uint32_t wait_time = BEKEN_WAIT_FOREVER;
    
    while (1)
    {
        blue_audio_recorder_msg_t blue_audio_recorder_msg;
        ret = rtos_pop_from_queue(&blue_audio_recorder_msg_que, &blue_audio_recorder_msg, wait_time);
        if (BK_OK == ret)
        {
            switch (blue_audio_recorder_msg.op)
            {
                case BLUE_AUDIO_RECORDER_OP_IDLE:
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case BLUE_AUDIO_RECORDER_OP_EXIT:
                    goto exit;
                    break;

                case BLUE_AUDIO_RECORDER_OP_START:
                    // Reset recorded frames count
                    frames_recorded = 0;
 
                    // Start recorder
                    ret = blue_audio_recorder_start(gl_blue_audio_recorder_handle);
                    if (ret != BK_OK)
                    {
                        BK_LOGE(TAG, "start blue_audio_recorder fail\n");
                        goto exit;
                    }
                    audio_record_running = true;
                    wait_time = 0;
                    break;

                case BLUE_AUDIO_RECORDER_OP_STOP:
                    blue_audio_recorder_stop(gl_blue_audio_recorder_handle);
                    frames_recorded = 0;
                    audio_record_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                default:
                    break;
            }
        }

        if (audio_record_running)
        {
            // Check if reached specified frame count
            if (frames_to_record > 0 && frames_recorded >= frames_to_record)
            {
                BK_LOGI(TAG, "Reached specified frame count: %d\n", frames_to_record);
                blue_audio_recorder_stop(gl_blue_audio_recorder_handle);
                audio_record_running = false;
                wait_time = BEKEN_WAIT_FOREVER;
                continue;
            }

            // Read one frame of audio data
            ret = blue_audio_recorder_read_frame_data(
                gl_blue_audio_recorder_handle, 
                (char *)recording_buffer, 
                RECORDER_DEFAULT_FRAME_SIZE
            );

            if (ret > 0)
            {
                frames_recorded++;

                // Print information every 10 frames recorded
                if (frames_recorded % 10 == 0)
                {
                    BK_LOGD(TAG, "Recorded %d frames, data length: %d bytes\n", frames_recorded, ret);
                }
            }
            else if (ret != BK_OK)
            {
                BK_LOGE(TAG, "read audio frame data fail, ret = %d\n", ret);
                // Continue to try again
            }
        }
    }

exit:
    audio_record_running = false;

    if (recording_buffer)
    {
        audio_free(recording_buffer);
        recording_buffer = NULL;
    }

    blue_audio_recorder_stop(gl_blue_audio_recorder_handle);
    blue_audio_recorder_destroy(gl_blue_audio_recorder_handle);
    gl_blue_audio_recorder_handle = NULL;

    rtos_deinit_queue(&blue_audio_recorder_msg_que);
    blue_audio_recorder_msg_que = NULL;

    rtos_set_semaphore(&audio_record_sem);

    audio_record_task_hdl = NULL;
    rtos_delete_thread(NULL);
}

/**
 * @brief Initialize audio recorder test
 */
static bk_err_t blue_audio_recorder_init(uint32_t frames)
{
    bk_err_t ret = BK_OK;

    frames_to_record = frames;

    ret = rtos_init_semaphore(&audio_record_sem, 1);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, create semaphore fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    ret = rtos_init_queue(&blue_audio_recorder_msg_que,
                          "blue_audio_recorder_msg_que",
                          sizeof(blue_audio_recorder_msg_t),
                          5);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, create blue audio recorder message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_create_thread(&audio_record_task_hdl,
                             (BEKEN_DEFAULT_WORKER_PRIORITY - 1),
                             "blue_audio_record",
                             (beken_thread_function_t)blue_audio_recorder_main,
                             2048 * 2,
                             (beken_thread_arg_t)frames_to_record);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, create blue audio recorder task fail\n", __func__, __LINE__);
        goto fail;
    }

    rtos_get_semaphore(&audio_record_sem, BEKEN_NEVER_TIMEOUT);

    return BK_OK;

fail:
    if (audio_record_sem)
    {
        rtos_deinit_semaphore(&audio_record_sem);
        audio_record_sem = NULL;
    }

    if (blue_audio_recorder_msg_que)
    {
        rtos_deinit_queue(&blue_audio_recorder_msg_que);
        blue_audio_recorder_msg_que = NULL;
    }

    return BK_FAIL;
}

/**
 * @brief CLI command processing function
 */
void cli_blue_audio_recorder_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2)
    {
        BK_LOGI(TAG, "blue_audio_recorder ...\n");
        return;
    }

    if (os_strcmp(argv[1], "init") == 0)
    {
        uint32_t frames = 0; // Default to record until stopped
        if (argc >= 3)
        {
            frames = os_strtoul((char *)argv[2], NULL, 10);
        }
        
        if (BK_OK != blue_audio_recorder_init(frames))
        {
            BK_LOGE(TAG, "init blue audio recorder fail\n");
        }
    }
    else if (os_strcmp(argv[1], "start") == 0)
    {
        blue_audio_recorder_send_msg(&blue_audio_recorder_msg_que, BLUE_AUDIO_RECORDER_OP_START, NULL);
    }
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        blue_audio_recorder_send_msg(&blue_audio_recorder_msg_que, BLUE_AUDIO_RECORDER_OP_STOP, NULL);
    }
    else if (os_strcmp(argv[1], "deinit") == 0)
    {
        blue_audio_recorder_send_msg(&blue_audio_recorder_msg_que, BLUE_AUDIO_RECORDER_OP_EXIT, NULL);

        rtos_get_semaphore(&audio_record_sem, BEKEN_NEVER_TIMEOUT);

        rtos_deinit_semaphore(&audio_record_sem);
        audio_record_sem = NULL;
    }
    else if (os_strcmp(argv[1], "api") == 0)
    {
        if (argc < 3)
        {
            BK_LOGI(TAG, "blue_audio_recorder api ...\n");
            return;
        }

        if (os_strcmp(argv[2], "create") == 0)
        {
            blue_audio_recorder_cfg_t cfg = DEFAULT_BLUE_AUDIO_RECORDER_PCM_ONBOARD_MIC_CONFIG();
            gl_blue_audio_recorder_handle = blue_audio_recorder_create(&cfg);
            if (gl_blue_audio_recorder_handle == NULL)
            {
                BK_LOGE(TAG, "create blue audio recorder fail\n");
            }
        }
        else if (os_strcmp(argv[2], "start") == 0)
        {
            if (gl_blue_audio_recorder_handle)
            {
                blue_audio_recorder_start(gl_blue_audio_recorder_handle);
            }
            else
            {
                BK_LOGE(TAG, "recorder handle is NULL, please create first\n");
            }
        }
        else if (os_strcmp(argv[2], "stop") == 0)
        {
            if (gl_blue_audio_recorder_handle)
            {
                blue_audio_recorder_stop(gl_blue_audio_recorder_handle);
            }
            else
            {
                BK_LOGE(TAG, "recorder handle is NULL\n");
            }
        }
        else if (os_strcmp(argv[2], "destroy") == 0)
        {
            if (gl_blue_audio_recorder_handle)
            {
                blue_audio_recorder_destroy(gl_blue_audio_recorder_handle);
                gl_blue_audio_recorder_handle = NULL;
            }
        }
        else if (os_strcmp(argv[2], "read") == 0)
        {
            if (gl_blue_audio_recorder_handle)
            {
                uint32_t frame_count = 10; // Default to read 10 frames
                if (argc >= 4)
                {
                    frame_count = os_strtoul((char *)argv[3], NULL, 10);
                }
                
                // Allocate temporary buffer for reading
                uint8_t *temp_buffer = (uint8_t *)audio_malloc(RECORDER_DEFAULT_FRAME_SIZE);
                if (temp_buffer == NULL)
                {
                    BK_LOGE(TAG, "Allocate temporary buffer failed\n");
                    return;
                }
                
                for (uint32_t i = 0; i < frame_count; i++)
                {
                    int read_len = blue_audio_recorder_read_frame_data(
                        gl_blue_audio_recorder_handle, 
                        (char *)temp_buffer, 
                        RECORDER_DEFAULT_FRAME_SIZE
                    );
                    
                    if (read_len > 0)
                    {
                        BK_LOGD(TAG, "Read frame %d, length: %d bytes\n", i+1, read_len);
                    }
                    else
                    {
                        BK_LOGE(TAG, "Read frame %d failed, ret: %d\n", i+1, read_len);
                        break;
                    }
                }
                
                audio_free(temp_buffer);
            }
            else
            {
                BK_LOGE(TAG, "recorder handle is NULL, please create first\n");
            }
        }
        else
        {
            BK_LOGI(TAG, "blue_audio_recorder api ...\n");
        }
    }
    else if (os_strcmp(argv[1], "help") == 0)
    {
        BK_LOGI(TAG, "blue_audio_recorder commands:\n");
        BK_LOGI(TAG, "  init [frames] - Initialize blue audio recorder, optional frames to record\n");
        BK_LOGI(TAG, "  start - Start recording audio\n");
        BK_LOGI(TAG, "  stop - Stop recording\n");
        BK_LOGI(TAG, "  deinit - Deinitialize blue audio recorder\n");
        BK_LOGI(TAG, "  api create - Create recorder handle\n");
        BK_LOGI(TAG, "  api start - Start recorder\n");
        BK_LOGI(TAG, "  api stop - Stop recorder\n");
        BK_LOGI(TAG, "  api destroy - Destroy recorder handle\n");
        BK_LOGI(TAG, "  api read [frames] - Read recorded audio frames\n");
    }
}

#define BLUE_AUDIO_RECORDER_CMD_CNT  (sizeof(s_blue_audio_recorder_commands) / sizeof(struct cli_command))
static const struct cli_command s_blue_audio_recorder_commands[] =
{
    {"blue_audio_recorder", "blue_audio_recorder ...", cli_blue_audio_recorder_cmd},
};

/**
 * @brief CLI command initialization function
 */
int cli_blue_audio_recorder_init(void)
{
    BK_LOGI(TAG, "cli_blue_audio_recorder_init\n");

    return cli_register_commands(s_blue_audio_recorder_commands, BLUE_AUDIO_RECORDER_CMD_CNT);
}