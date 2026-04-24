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

#ifndef _SW_NULL_SINK_H_
#define _SW_NULL_SINK_H_

/*
 * sw_null_sink — Pipeline 调试用空消费元素
 *
 * 功能：
 *   与 speaker 同样从 ringbuf 读取并消费，但不做重采样和 DMA，仅读后丢弃。
 *   用于验证 send queue failed 是否来自 speaker 的重采样/DMA，还是任意下游消费者。
 *
 * 使用：开启 CONFIG_PIPELINE_DEBUG_USE_NULL_SINK 时，替代 onboard_speaker 注册为 "speaker"。
 */

#include <components/bk_audio/audio_pipeline/audio_element.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  null_sink 配置（与 speaker 对齐的简化配置）
 */
typedef struct
{
    int buffer_len;   /*!< 输入缓冲区大小，建议与 frame_size * chl_num 一致 */
    int task_stack;   /*!< 任务栈大小 */
    int task_prio;    /*!< 任务优先级 */
    int task_core;    /*!< 运行核心 (0 或 1) */
} sw_null_sink_cfg_t;

/**
 * @brief  创建 null_sink 音频元素。
 *         从输入 ringbuf 读取数据并丢弃，不进行重采样和 DMA。
 *
 * @param  config  配置，NULL 时使用默认值
 * @return Audio element handle，失败返回 NULL
 */
audio_element_handle_t sw_null_sink_init(const sw_null_sink_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif /* _SW_NULL_SINK_H_ */
