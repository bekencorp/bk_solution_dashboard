// Copyright 2024-2025 Beken
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

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t version;
    uint32_t total_length;
    uint16_t packet_count;
    uint16_t expected_crc;
    uint8_t file_operation;
    uint8_t file_type;
    const char *file_name;
    uint32_t file_name_length;
} media_navigation_transfer_cfg_t;

bk_err_t media_navigation_transfer_begin(const media_navigation_transfer_cfg_t *cfg);
bk_err_t media_navigation_transfer_cancel(void);

/* Push one packet of the current frame.
 * frame_done (optional, may be NULL): set to true when this packet completes the
 * whole frame (assembled, CRC-checked and pushed to the decode pipeline); false
 * for mid-frame packets. On error the return value is non-BK_OK and frame_done
 * is set to false. */
bk_err_t media_navigation_transfer_push(uint16_t packet_num, const uint8_t *data, uint16_t data_length, bool *frame_done);
bool media_navigation_transfer_is_active(void);

#ifdef __cplusplus
}
#endif


