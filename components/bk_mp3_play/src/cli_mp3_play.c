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

#include "stdio.h"
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <modules/pm.h>
#include "bk_mp3_play.h"
#include "cli.h"
#include "test_mp3_array.h"

#define TAG  "mp3_play_test"


typedef enum
{
    MP3_PLAY_OP_IDLE = 0,
    MP3_PLAY_OP_START,
    MP3_PLAY_OP_EXIT,
    MP3_PLAY_OP_STOP,
} mp3_play_op_t;

typedef struct
{
    mp3_play_op_t op;
    void *param;
} mp3_play_msg_t;


static bk_mp3_play_handle_t gl_mp3_play_handle = NULL;
static bool audio_play_running = false;
static beken_thread_t audio_play_task_hdl = NULL;
static beken_semaphore_t audio_play_sem = NULL;
static beken_queue_t mp3_play_msg_que = NULL;

static bk_err_t mp3_play_send_msg(beken_queue_t *queue, mp3_play_op_t op, void *param)
{
    bk_err_t ret;
    mp3_play_msg_t msg;

    msg.op = op;
    msg.param = param;
    ret = rtos_push_to_queue(queue, &msg, BEKEN_NO_WAIT);
    if (BK_OK != ret)
    {
        BK_LOGE(TAG, "%s, %d, mp3_play send message: %d fail, ret: %d\n", __func__, __LINE__, op, ret);
        return BK_FAIL;
    }

    return BK_OK;
}

int mp3_play_event_handle_callback(int event, void *args, void *user_data)
{
    BK_LOGD(TAG, "mp3_play_event_handle_callback, event: %d, args: %p, user_data: %p\n", event, args, user_data);
    if (event == MP3_PLAY_EVENT_FINISH)
    {
        bk_mp3_play_stop(gl_mp3_play_handle);
    }

    return BK_OK;
}

static bk_err_t get_mp3_music_ptr(uint32_t mp3_music_id, char **mp3_music_ptr, uint32_t *mp3_music_size)
{
	if (mp3_music_id > MP3_MUSIC_MAX)
    {
		BK_LOGE(TAG, "mp3_music_id: %d is not support\n", mp3_music_id);
		return BK_FAIL;
	}
    else
    {
        switch (mp3_music_id)
        {
            case 0:
                *mp3_music_ptr = (char *)mp3_0102_44100_16bit_mono;
                *mp3_music_size = sizeof(mp3_0102_44100_16bit_mono);
                break;

            case 1:
                *mp3_music_ptr = (char *)mp3_0104_44100_16bit_mono;
                *mp3_music_size = sizeof(mp3_0104_44100_16bit_mono);
                break;

            case 2:
                *mp3_music_ptr = (char *)mp3_0112_44100_16bit_mono;
                *mp3_music_size = sizeof(mp3_0112_44100_16bit_mono);
                break;

            case 3:
                *mp3_music_ptr = (char *)mp3_0205_44100_16bit_mono;
                *mp3_music_size = sizeof(mp3_0205_44100_16bit_mono);
                break;

            default:
                break;
        }
    }

    BK_LOGD(TAG, "mp3_music_size: %d ----------\n", *mp3_music_size);

    return BK_OK;
}

static void mp3_play_main(beken_thread_arg_t param_data)
{
	bk_err_t ret = BK_OK;
    uint32_t mp3_music_id = 0;
    char *mp3_music_ptr = NULL;
    uint32_t mp3_music_size = 0;
    uint32_t mp3_music_read_offset = 0;

    bk_mp3_play_cfg_t config = DEFAULT_MP3_PLAY_CONFIG();
    config.event_handle = mp3_play_event_handle_callback;
    config.args = NULL;

    gl_mp3_play_handle = bk_mp3_play_create(&config);
    if (!gl_mp3_play_handle)
    {
        BK_LOGE(TAG, "create mp3_play fail\n");
        goto exit;
    }

    rtos_set_semaphore(&audio_play_sem);

    audio_play_running = false;
    uint32_t wait_time = BEKEN_WAIT_FOREVER;
    uint32_t write_size = 0;
    while (1)
    {
        mp3_play_msg_t mp3_play_msg;
        ret = rtos_pop_from_queue(&mp3_play_msg_que, &mp3_play_msg, wait_time);
        if (BK_OK == ret)
        {
            switch (mp3_play_msg.op)
            {
                case MP3_PLAY_OP_IDLE:
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                case MP3_PLAY_OP_EXIT:
                    goto exit;
                    break;

                case MP3_PLAY_OP_START:
                    mp3_music_id = (uint32_t)mp3_play_msg.param;
                    ret = get_mp3_music_ptr(mp3_music_id, &mp3_music_ptr, &mp3_music_size);
                    if (ret != BK_OK)
                    {
                        BK_LOGE(TAG, "get mp3 music ptr fail, ret: %d\n", ret);
                        break;
                    }

                    ret = bk_mp3_play_start(gl_mp3_play_handle);
                    if (ret != BK_OK)
                    {
                        BK_LOGE(TAG, "start mp3_play fail\n");
                        goto exit;
                    }
                    audio_play_running = true;
                    wait_time = 0;
                    break;

                case MP3_PLAY_OP_STOP:
                    bk_mp3_play_stop(gl_mp3_play_handle);
                    mp3_music_ptr = NULL;
                    mp3_music_size = 0;
                    mp3_music_read_offset = 0;
                    audio_play_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                    break;

                default:
                    break;
            }
        }

        if (audio_play_running)
        {
            /* check whether mp3 play finished */
            if (mp3_music_size > mp3_music_read_offset)
            {
                write_size = mp3_music_size - mp3_music_read_offset;
                BK_LOGE(TAG, "bk_mp3_play_write_data write: %d ----------\n", write_size);
                int32_t ret = bk_mp3_play_write_data(gl_mp3_play_handle, mp3_music_ptr + mp3_music_read_offset, write_size);
                BK_LOGE(TAG, "bk_mp3_play_write_data ret: %d ----------\n", ret);
                if (ret >= 0)
                {
                    mp3_music_read_offset += ret;
                    if (ret != write_size)
                    {
                        BK_LOGW(TAG, "write mp3 data not all, write_size = %d, ret = %d\n", write_size, ret);
                        audio_play_running = false;
                        wait_time = BEKEN_WAIT_FOREVER;
                    }
                }
                else
                {
                    BK_LOGE(TAG, "write mp3 data fail, ret = %d\n", ret);
                    audio_play_running = false;
                    wait_time = BEKEN_WAIT_FOREVER;
                }
            }
            else
            {
                bk_mp3_play_write_data_done(gl_mp3_play_handle);
                BK_LOGD(TAG, "mp3 music is empty, and stop play\n");
                /* stop write mp3 data, and wait other message */
                audio_play_running = false;
                wait_time = BEKEN_WAIT_FOREVER;
            }
        }
    }

exit:
    audio_play_running = false;

    bk_mp3_play_stop(gl_mp3_play_handle);
    bk_mp3_play_destroy(gl_mp3_play_handle);
    gl_mp3_play_handle = NULL;

    rtos_deinit_queue(&mp3_play_msg_que);
    mp3_play_msg_que = NULL;

    rtos_set_semaphore(&audio_play_sem);

    audio_play_task_hdl = NULL;
    rtos_delete_thread(NULL);
}


static bk_err_t mp3_play_init(void)
{
    bk_err_t ret = BK_OK;

    ret = rtos_init_semaphore(&audio_play_sem, 1);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, ceate semaphore fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    ret = rtos_init_queue(&mp3_play_msg_que,
                          "mp3_play_msg_que",
                          sizeof(mp3_play_msg_t),
                          5);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, ceate mp3 play message queue fail\n", __func__, __LINE__);
        goto fail;
    }

    ret = rtos_create_thread(&audio_play_task_hdl,
                             (BEKEN_DEFAULT_WORKER_PRIORITY - 1),
                             "audio_play",
                             (beken_thread_function_t)mp3_play_main,
                             2048 * 2,
                             NULL);
    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "%s, %d, create mp3 play task fail\n", __func__, __LINE__);
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

    if (mp3_play_msg_que)
    {
        rtos_deinit_queue(&mp3_play_msg_que);
        mp3_play_msg_que = NULL;
    }

    return BK_FAIL;
}

void cli_mp3_play_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
	if (argc < 2)
    {
		BK_LOGI(TAG, "mp3_play ...\n");
		return;
	}

	if (os_strcmp(argv[1], "init") == 0)
    {
        if (BK_OK != mp3_play_init())
        {
			BK_LOGE(TAG, "init mp3 play fail\n");
		}
	}
    else if (os_strcmp(argv[1], "start") == 0)
    {
        mp3_play_send_msg(&mp3_play_msg_que, MP3_PLAY_OP_START, (void *)os_strtoul((char *)argv[2], NULL, 10));
	}
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        mp3_play_send_msg(&mp3_play_msg_que, MP3_PLAY_OP_STOP, NULL);
	}
    else if (os_strcmp(argv[1], "deinit") == 0)
    {
        mp3_play_send_msg(&mp3_play_msg_que, MP3_PLAY_OP_EXIT, NULL);

        rtos_get_semaphore(&audio_play_sem, BEKEN_NEVER_TIMEOUT);

        rtos_deinit_semaphore(&audio_play_sem);
        audio_play_sem = NULL;
	}
    else if (os_strcmp(argv[1], "test_exit_after_write_mp3_header") == 0)
    {
        /* 
            Test Scenario 1:
            After writing an MP3 data packet without an ID3 header (at this time, the MP3 decoder reads the 3-byte packet header but has not started decoding), immediately stop playback and check if it can exit normally.
         */
        /* init mp3 play */
        if (BK_OK != mp3_play_init())
        {
			BK_LOGE(TAG, "init mp3 play fail\n");
		}

        /* start mp3 play */
        bk_mp3_play_start(gl_mp3_play_handle);
        /* write frame_header(3 bytes) */
        bk_mp3_play_write_data(gl_mp3_play_handle, (char *)mp3_0205_44100_16bit_mono, 3);

        rtos_delay_milliseconds(100);
        /* stop mp3 play when mp3 decoder only read ID3 header */
        bk_mp3_play_stop(gl_mp3_play_handle);

        mp3_play_send_msg(&mp3_play_msg_que, MP3_PLAY_OP_EXIT, NULL);
        rtos_get_semaphore(&audio_play_sem, BEKEN_NEVER_TIMEOUT);

        rtos_deinit_semaphore(&audio_play_sem);
        audio_play_sem = NULL;
	}
    else if (os_strcmp(argv[1], "api") == 0)
    {
        if (argc < 3)
        {
            BK_LOGI(TAG, "mp3_play ...\n");
            return;
        }

        if (os_strcmp(argv[2], "create") == 0)
        {
            bk_mp3_play_cfg_t cfg = DEFAULT_MP3_PLAY_CONFIG();
            gl_mp3_play_handle = bk_mp3_play_create(&cfg);
            if (gl_mp3_play_handle == NULL)
            {
                BK_LOGE(TAG, "create mp3 play fail\n");
            }
        }
        else if (os_strcmp(argv[2], "start") == 0)
        {
            bk_mp3_play_start(gl_mp3_play_handle);
            char *temp_mp3_music_ptr = NULL;
            uint32_t temp_mp3_music_size = 0;
            if (argc < 4)
            {
                BK_LOGI(TAG, "mp3_play ...\n");
                return;
            }
            get_mp3_music_ptr(os_strtoul((char *)argv[3], NULL, 10), &temp_mp3_music_ptr, &temp_mp3_music_size);
            BK_LOGD(TAG, "temp_mp3_music_size: %d\n", temp_mp3_music_size);
            bk_mp3_play_write_data(gl_mp3_play_handle, temp_mp3_music_ptr, temp_mp3_music_size);
            bk_mp3_play_write_data_done(gl_mp3_play_handle);
        }
        else if (os_strcmp(argv[2], "stop") == 0)
        {
            bk_mp3_play_stop(gl_mp3_play_handle);
        }
        else if (os_strcmp(argv[2], "destroy") == 0)
        {
            bk_mp3_play_destroy(gl_mp3_play_handle);
            gl_mp3_play_handle = NULL;
        }
        else
        {
            BK_LOGI(TAG, "mp3_play ...\n");
        }
	}
    else if (os_strcmp(argv[1], "help") == 0)
    {
		BK_LOGI(TAG, "mp3_play ...\n");
	}
}


#define MP3_PLAY_CMD_CNT  (sizeof(s_mp3_play_commands) / sizeof(struct cli_command))
static const struct cli_command s_mp3_play_commands[] =
{
	{"mp3_play", "mp3_play ...", cli_mp3_play_cmd},
};

int cli_mp3_play_init(void)
{
	BK_LOGI(TAG, "cli_mp3_play_init \n");

	return cli_register_commands(s_mp3_play_commands, MP3_PLAY_CMD_CNT);
}
