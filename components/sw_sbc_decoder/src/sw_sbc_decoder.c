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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include <sw_sbc_decoder.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_pipeline/audio_mem.h>
#include <components/bk_audio/audio_pipeline/audio_error.h>
#include <components/bk_audio/audio_pipeline/audio_element.h>
#include <os/os.h>
#include <os/mem.h>
#include <driver/uart.h>

#include <sbc.h>

#define TAG  "SW_SBC_D"

#define PCM_DUMP_UART_ID     UART_ID_2
#define PCM_DUMP_UART_BAUD   2000000
/* PIPELINE_DEBUG_STAGE=1 时：decoder 截止，dump 到 UART2 不 forward
 * PCM_DUMP_SKIP_FRAMES：跳过前 N 帧再开始 dump
 * PCM_DUMP_INTERVAL   ：每 N 帧 dump 一帧（1=连续） */
#define PCM_DUMP_SKIP_FRAMES 50
#define PCM_DUMP_INTERVAL    1

static uint32_t s_dbg_frame_cnt = 0;
static uint8_t  s_uart_inited   = 0;

typedef struct {
    sbc_t             sbc;
    struct sbc_frame  frame;
    uint32_t          sample_rate;
    uint8_t           channel_number;
    uint8_t           pending[2048];
    uint32_t          pending_len;
    int16_t           pcml[SBC_MAX_SAMPLES];
    int16_t           pcmr[SBC_MAX_SAMPLES];
    int32_t           pcm_out[SBC_MAX_SAMPLES];
} sw_sbc_decoder_t;

static bk_err_t music_info_report(audio_element_handle_t self,
                                  sw_sbc_decoder_t *dec,
                                  int sr, int nch)
{
    if ((uint32_t)sr != dec->sample_rate || (uint8_t)nch != dec->channel_number) {
        BK_LOGD(TAG, "[%s] new info sr=%d ch=%d\n",
                audio_element_get_tag(self), sr, nch);

        audio_element_info_t info = {0};
        bk_err_t ret = audio_element_getinfo(self, &info);
        if (ret != BK_OK) return BK_FAIL;

        info.bits         = 16;
        info.sample_rates = sr;
        info.channels     = nch;
        ret = audio_element_setinfo(self, &info);
        if (ret != BK_OK) return BK_FAIL;

        ret = audio_element_report_info(self);
        if (ret != BK_OK) return BK_FAIL;

        dec->sample_rate    = (uint32_t)sr;
        dec->channel_number = (uint8_t)nch;
    }

    return BK_OK;
}

static bk_err_t _sw_sbc_decoder_open(audio_element_handle_t self)
{
    sw_sbc_decoder_t *dec = (sw_sbc_decoder_t *)audio_element_getdata(self);

    sbc_reset(&dec->sbc);
    dec->sample_rate    = 0;
    dec->channel_number = 0;
    dec->pending_len    = 0;

    audio_element_set_input_timeout(self, 20 / portTICK_RATE_MS);

    s_dbg_frame_cnt = 0;

#if (CONFIG_PIPELINE_DEBUG_STAGE == 1)
    if (!s_uart_inited) {
        uart_config_t uart_cfg = {0};
        uart_cfg.baud_rate = PCM_DUMP_UART_BAUD;
        uart_cfg.data_bits = UART_DATA_8_BITS;
        uart_cfg.parity    = UART_PARITY_NONE;
        uart_cfg.stop_bits = UART_STOP_BITS_1;
        uart_cfg.flow_ctrl = UART_FLOWCTRL_DISABLE;
        uart_cfg.src_clk   = UART_SCLK_XTAL_26M;
        bk_uart_init(PCM_DUMP_UART_ID, &uart_cfg);
        s_uart_inited = 1;
        BK_LOGI(TAG, "PIPELINE_DEBUG_STAGE=1: decoder cutoff, UART%d dump\n",
                PCM_DUMP_UART_ID);
    }
#endif

    BK_LOGI(TAG, "[%s] opened (google-sbc)\n", audio_element_get_tag(self));
    return BK_OK;
}

static int _sw_sbc_decoder_process(audio_element_handle_t self,
                                   char *in_buffer, int in_len)
{
    sw_sbc_decoder_t *dec = (sw_sbc_decoder_t *)audio_element_getdata(self);

    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;

    if (r_size <= 0)
        return r_size;

    if ((dec->pending_len + (uint32_t)r_size) > sizeof(dec->pending))
        dec->pending_len = 0;

    os_memcpy(dec->pending + dec->pending_len, in_buffer, r_size);
    dec->pending_len += (uint32_t)r_size;

    uint32_t offset = 0;
    while (offset < dec->pending_len) {
        uint8_t *d = dec->pending + offset;
        uint32_t remain = dec->pending_len - offset;

        if (d[0] != 0x9C && d[0] != 0xAD) {
            offset++;
            continue;
        }

        if (remain < SBC_HEADER_SIZE)
            break;

        if (sbc_probe(d, &dec->frame) < 0) {
            offset++;
            continue;
        }

        unsigned fsize = sbc_get_frame_size(&dec->frame);
        if (fsize == 0 || remain < fsize)
            break;
        uint32_t time1 = rtos_get_time();
        int ret = sbc_decode(&dec->sbc, d, remain, &dec->frame,
                             dec->pcml, 1, dec->pcmr, 1);
        uint32_t time2 = rtos_get_time();
        if (time2 - time1 > 20) {
            BK_LOGE(TAG, "sbc_decode time: %d\r\n", time2 - time1);
        }
        // BK_LOGI(TAG, "sbc_decode time: %d\r\n", time2 - time1);
        if (ret < 0) {
            BK_LOGE(TAG, "sbc_decode err, resync\n");
            offset++;
            continue;
        }

        offset += fsize;

        int nch = (dec->frame.mode == SBC_MODE_MONO) ? 1 : 2;
        int sr  = sbc_get_freq_hz(dec->frame.freq);
        int pcm_len = dec->frame.nblocks * dec->frame.nsubbands;

        music_info_report(self, dec, sr, 1);

        int16_t *mono_buf = dec->pcml;
        if (nch == 2) {
            for (int i = 0; i < pcm_len; i++) {
                dec->pcml[i] = (int16_t)(((int32_t)dec->pcml[i] + (int32_t)dec->pcmr[i]) >> 1);
            }
        }
        int output_size = pcm_len * 2;

        s_dbg_frame_cnt++;

#if (CONFIG_PIPELINE_DEBUG_STAGE == 1)
        if (s_uart_inited && s_dbg_frame_cnt > PCM_DUMP_SKIP_FRAMES
            && (s_dbg_frame_cnt % PCM_DUMP_INTERVAL) == 0) {
            bk_uart_write_bytes(PCM_DUMP_UART_ID,
                                (const uint8_t *)mono_buf,
                                output_size);
        }
#endif

        // if (s_dbg_frame_cnt <= 5 || (s_dbg_frame_cnt % 200) == 0) {
        //     int16_t max_abs_mono = 0;
        //     for (int j = 0; j < pcm_len && j < 64; j++) {
        //         int16_t v = mono_buf[j];
        //         if (v < 0) v = -v;
        //         if (v > max_abs_mono) max_abs_mono = v;
        //     }
        //     BK_LOGI(TAG, "DBG #%u: src_ch=%d rpt_ch=1 pcm_len=%d out_sz=%d mono_max=%d s0=0x%04x\n",
        //             s_dbg_frame_cnt, nch, pcm_len, output_size,
        //             (int)max_abs_mono, pcm_len > 0 ? (unsigned)(uint16_t)mono_buf[0] : 0);
        // }

#if (CONFIG_PIPELINE_DEBUG_STAGE == 1)
        /* Stage 1: cutoff at decoder, dump only, no forward */
        w_size += output_size;
#else
        int out_len = audio_element_output(self,
                                           (char *)mono_buf,
                                           output_size);
        if (out_len > 0)
            w_size += out_len;
#endif
    }

    if (offset > 0 && offset <= dec->pending_len) {
        os_memmove(dec->pending, dec->pending + offset,
                   dec->pending_len - offset);
        dec->pending_len -= offset;
    }

    return (w_size > 0) ? w_size : r_size;
}

static bk_err_t _sw_sbc_decoder_close(audio_element_handle_t self)
{
    if (s_uart_inited) {
        bk_uart_deinit(PCM_DUMP_UART_ID);
        s_uart_inited = 0;
        BK_LOGI(TAG, "PCM dump UART%d closed\n", PCM_DUMP_UART_ID);
    }
    BK_LOGI(TAG, "[%s] closed, total frames: %u\n",
            audio_element_get_tag(self), s_dbg_frame_cnt);
    return BK_OK;
}

static bk_err_t _sw_sbc_decoder_destroy(audio_element_handle_t self)
{
    sw_sbc_decoder_t *dec = (sw_sbc_decoder_t *)audio_element_getdata(self);
    audio_free(dec);
    return BK_OK;
}

audio_element_handle_t sw_sbc_decoder_init(sbc_decoder_cfg_t *config)
{
    sw_sbc_decoder_t *dec = audio_calloc(1, sizeof(sw_sbc_decoder_t));
    AUDIO_MEM_CHECK(TAG, dec, return NULL);

    audio_element_cfg_t cfg   = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open                  = _sw_sbc_decoder_open;
    cfg.close                 = _sw_sbc_decoder_close;
    cfg.seek                  = NULL;
    cfg.process               = _sw_sbc_decoder_process;
    cfg.destroy               = _sw_sbc_decoder_destroy;
    cfg.in_type               = PORT_TYPE_RB;
    cfg.read                  = NULL;
    cfg.out_type              = PORT_TYPE_RB;
    cfg.write                 = NULL;
    cfg.task_stack             = config->task_stack;
    cfg.task_prio              = config->task_prio;
    cfg.task_core              = config->task_core;
    cfg.out_block_size         = config->out_block_size;
    cfg.out_block_num          = config->out_block_num;
    cfg.buffer_len             = config->buf_sz;
    cfg.tag                    = "sw_sbc_decoder";

    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, goto _exit);
    audio_element_setdata(el, dec);

    audio_element_info_t info = {0};
    audio_element_getinfo(el, &info);
    info.sample_rates = 0;
    info.channels     = 0;
    info.bits         = 0;
    info.codec_fmt    = BK_CODEC_TYPE_SBC;
    audio_element_setinfo(el, &info);

    return el;

_exit:
    audio_free(dec);
    return NULL;
}
