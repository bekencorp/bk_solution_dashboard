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

#ifndef _SW_SBC_DECODER_H_
#define _SW_SBC_DECODER_H_

#include <components/bk_audio/audio_pipeline/audio_element.h>
#include <components/bk_audio/audio_decoders/sbc_dec.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create a software SBC decoder audio element.
 *         Provides the same interface as the hardware sbc_decoder_init()
 *         so it can be used as a drop-in replacement on chips that lack
 *         a hardware SBC accelerator (e.g. BK7259).
 *
 * @param  config  Pointer to sbc_decoder_cfg_t (same config struct as HW decoder)
 * @return Audio element handle, or NULL on failure
 */
audio_element_handle_t sw_sbc_decoder_init(sbc_decoder_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif /* _SW_SBC_DECODER_H_ */
