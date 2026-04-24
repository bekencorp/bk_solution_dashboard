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

#ifndef _SW_PCM_TAP_H_
#define _SW_PCM_TAP_H_

/*
 * sw_pcm_tap — Stage-3 调试用透传音频元素
 *
 * 功能：
 *   插入 mix_alg 与 speaker 之间，数据原样透传，同时可选将
 *   16-bit 单声道 PCM（44100 Hz）dump 到 UART2 用于 PC 端分析。
 *
 * 使用方法：
 *   1. 将 PCM_TAP_DUMP_EN 改为 1，重新编译烧录
 *   2. 用 pyserial 捕获 UART2 (@2000000 baud) 约 5 秒数据到 stage3.raw
 *   3. PC 端验证：
 *        ffmpeg -f s16le -ar 44100 -ac 1 -i stage3.raw stage3.wav
 *        ffplay stage3.wav
 */

#include <components/bk_audio/audio_pipeline/audio_element.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  创建 PCM tap 透传音频元素。
 *         该元素读取输入数据，可选地 dump 到 UART2，然后原样写出。
 *
 * @return Audio element handle，失败返回 NULL
 */
audio_element_handle_t sw_pcm_tap_init(void);

/**
 * @brief  运行时控制 UART2 dump 开关（无需重新编译）。
 *
 * @param  en  1=开启 dump，0=关闭 dump
 */
void sw_pcm_tap_enable(int en);

#ifdef __cplusplus
}
#endif

#endif /* _SW_PCM_TAP_H_ */
