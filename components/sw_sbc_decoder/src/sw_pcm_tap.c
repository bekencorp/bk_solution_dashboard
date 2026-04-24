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
#include <sw_pcm_tap.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include <components/bk_audio/audio_pipeline/audio_error.h>
#include <components/bk_audio/audio_pipeline/audio_element.h>
#include <os/os.h>
#include <driver/uart.h>

#define TAG  "SW_PCM_TAP"

/* -----------------------------------------------------------------------
 * PIPELINE_DEBUG_STAGE=2 时：pcm_tap 截止，dump 到 UART2 不 forward
 * 与 decoder stage=1 共用 Kconfig，二者互斥（不同时 dump）
 * ----------------------------------------------------------------------- */

#define TAP_UART_ID   UART_ID_2
#define TAP_UART_BAUD 2000000U

static uint8_t s_tap_dump_en = 0;
static uint8_t s_uart_inited = 0;
static uint32_t s_tap_frame_cnt = 0;

void sw_pcm_tap_enable(int en)
{
    return;
    if (en && !s_uart_inited) 
    {
        uart_config_t uart_cfg = {0};
        uart_cfg.baud_rate = TAP_UART_BAUD;
        uart_cfg.data_bits = UART_DATA_8_BITS;
        uart_cfg.parity    = UART_PARITY_NONE;
        uart_cfg.stop_bits = UART_STOP_BITS_1;
        uart_cfg.flow_ctrl = UART_FLOWCTRL_DISABLE;
        uart_cfg.src_clk   = UART_SCLK_XTAL_26M;
        bk_uart_init(TAP_UART_ID, &uart_cfg);
        s_uart_inited = 1;
        BK_LOGI(TAG, "PCM tap UART%d @%u baud ready (16-bit mono 44100Hz)\n",
                TAP_UART_ID, TAP_UART_BAUD);
    } else if (!en && s_uart_inited) {
        bk_uart_deinit(TAP_UART_ID);
        s_uart_inited = 0;
        BK_LOGI(TAG, "PCM tap UART%d closed\n", TAP_UART_ID);
    }
    s_tap_dump_en = (uint8_t)(en ? 1 : 0);
}

static bk_err_t _tap_open(audio_element_handle_t self)
{
    audio_element_set_input_timeout(self, 20 / portTICK_RATE_MS);
    s_tap_frame_cnt = 0;
#if (CONFIG_PIPELINE_DEBUG_STAGE == 2)
    sw_pcm_tap_enable(1);
    BK_LOGI(TAG, "PIPELINE_DEBUG_STAGE=2: pcm_tap cutoff, UART dump\n");
#endif
    BK_LOGI(TAG, "[pcm_tap] opened, dump=%d\n", s_tap_dump_en);
    return BK_OK;
}

static bk_err_t _tap_close(audio_element_handle_t self)
{
#if (CONFIG_PIPELINE_DEBUG_STAGE == 2)
    sw_pcm_tap_enable(0);
#endif
    BK_LOGI(TAG, "[pcm_tap] closed\n");
    return BK_OK;
}

static bk_err_t _tap_destroy(audio_element_handle_t self)
{
    (void)self;
    return BK_OK;
}

static int _tap_process(audio_element_handle_t self, char *buf, int len)
{
    int r = audio_element_input(self, buf, len);
    if (r > 0) {
        s_tap_frame_cnt++;
        if (s_tap_frame_cnt <= 5 || (s_tap_frame_cnt % 200) == 0) {
            int16_t *p = (int16_t *)buf;
            int16_t max_v = 0;
            int n16 = r / 2;
            for (int i = 0; i < n16 && i < 64; i++) {
                int16_t v = p[i];
                if (v < 0) v = -v;
                if (v > max_v) max_v = v;
            }
            BK_LOGI(TAG, "TAP_IN #%u: len=%d s[0..3]={0x%04x,0x%04x,0x%04x,0x%04x} max=%d\n",
                    s_tap_frame_cnt, r,
                    n16 >= 1 ? (unsigned)(uint16_t)p[0] : 0,
                    n16 >= 2 ? (unsigned)(uint16_t)p[1] : 0,
                    n16 >= 3 ? (unsigned)(uint16_t)p[2] : 0,
                    n16 >= 4 ? (unsigned)(uint16_t)p[3] : 0,
                    (int)max_v);
        }
        /* Stage 2: cutoff at pcm_tap, dump only, no forward */
        // if (s_tap_dump_en && s_uart_inited) {
        //     bk_uart_write_bytes(TAP_UART_ID, (const uint8_t *)buf, (uint32_t)r);
        //     return r;
        // }
        return audio_element_output(self, buf, r);
    }
    return r;
}

audio_element_handle_t sw_pcm_tap_init(void)
{
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open        = _tap_open;
    cfg.close       = _tap_close;
    cfg.seek        = NULL;
    cfg.process     = _tap_process;
    cfg.destroy     = _tap_destroy;
    cfg.in_type     = PORT_TYPE_RB;
    cfg.read        = NULL;
    cfg.out_type    = PORT_TYPE_RB;
    cfg.write       = NULL;
    cfg.task_prio   = 3;
    cfg.tag         = "pcm_tap";

    audio_element_handle_t el = audio_element_init(&cfg);
    if (!el) {
        BK_LOGE(TAG, "pcm_tap element init failed\n");
    }
    return el;
}
