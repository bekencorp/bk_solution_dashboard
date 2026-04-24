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
#include "FreeRTOS.h"
#include "task.h"
#include <sw_null_sink.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include <components/bk_audio/audio_pipeline/audio_error.h>
#include <components/bk_audio/audio_pipeline/audio_element.h>
#include <os/os.h>

#define TAG  "SW_NULL_SINK"

#define NULL_SINK_DEFAULT_BUFFER_LEN  3528
#define NULL_SINK_DEFAULT_TASK_STACK  1536
#define NULL_SINK_DEFAULT_TASK_PRIO   5
#define NULL_SINK_DEFAULT_TASK_CORE   1

static int _null_sink_write(audio_port_handle_t self, char *buffer, int len,
                            TickType_t ticks_to_wait, void *context)
{
    (void)self;
    (void)buffer;
    (void)ticks_to_wait;
    (void)context;
    /* 丢弃数据，仅返回成功 */
    return len > 0 ? len : 0;
}

static bk_err_t _null_sink_open(audio_element_handle_t self)
{
    audio_element_set_input_timeout(self, 20 / portTICK_RATE_MS);
    BK_LOGI(TAG, "[null_sink] opened\n");
    return BK_OK;
}

static bk_err_t _null_sink_close(audio_element_handle_t self)
{
    (void)self;
    BK_LOGI(TAG, "[null_sink] closed\n");
    return BK_OK;
}

static bk_err_t _null_sink_destroy(audio_element_handle_t self)
{
    (void)self;
    return BK_OK;
}

static int _null_sink_process(audio_element_handle_t self, char *buf, int len)
{
    int r = audio_element_input(self, buf, len);
    if (r > 0) {
        /* 读入后通过 output 写出，write 回调会丢弃数据 */
        return audio_element_output(self, buf, r);
    }
    return r;
}

audio_element_handle_t sw_null_sink_init(const sw_null_sink_cfg_t *config)
{
    int buffer_len = config ? config->buffer_len : NULL_SINK_DEFAULT_BUFFER_LEN;
    int task_stack = config ? config->task_stack : NULL_SINK_DEFAULT_TASK_STACK;
    int task_prio  = config ? config->task_prio  : NULL_SINK_DEFAULT_TASK_PRIO;
    int task_core  = config ? config->task_core  : NULL_SINK_DEFAULT_TASK_CORE;

    if (buffer_len <= 0) {
        buffer_len = NULL_SINK_DEFAULT_BUFFER_LEN;
    }

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open        = _null_sink_open;
    cfg.close       = _null_sink_close;
    cfg.seek        = NULL;
    cfg.process     = _null_sink_process;
    cfg.destroy     = _null_sink_destroy;
    cfg.in_type     = PORT_TYPE_RB;
    cfg.read        = NULL;
    cfg.out_type    = PORT_TYPE_CB;
    cfg.write       = _null_sink_write;
    cfg.buffer_len  = buffer_len;
    cfg.task_stack  = task_stack;
    cfg.task_prio   = task_prio;
    cfg.task_core   = task_core;
    cfg.tag         = "null_sink";

    audio_element_handle_t el = audio_element_init(&cfg);
    if (!el) {
        BK_LOGE(TAG, "null_sink element init failed\n");
    }
    return el;
}
