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

#include "media_navigation_transfer.h"

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>

#include "media_data_process_que.h"
#include "components/media_types.h"
#include "cast_jpeg_pipeline.h"

#define TAG "media-nav"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define FILE_TYPE_JPEG_VALUE    (0)
#define FILE_TYPE_PNG_VALUE     (1)

#define NAV_CRC16_POLY          (0x1021)
#define NAV_CRC16_INIT          (0xFFFF)

typedef struct
{
    bool active;
    uint8_t version;
    uint32_t total_length;
    uint16_t expected_packets;
    uint16_t expected_crc;
    uint8_t file_operation;
    uint8_t file_type;
    char file_name[64];
    frame_buffer_t *frame;
    uint32_t received_length;
    uint16_t last_packet_num;
    uint16_t running_crc;
} navigation_transfer_ctx_t;

static navigation_transfer_ctx_t s_nav_ctx = {0};
static uint32_t s_frame_sequence = 0;

static uint16_t gen_crc16(uint16_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
    {
         crc = ((crc >> 8) | (crc << 8)) & NAV_CRC16_INIT;
         crc ^= (data[i] & 0xFF);// byte to int, trunc sign
         crc ^= ((crc & 0xFF) >> 4);
         crc ^= (crc << 12) & NAV_CRC16_INIT;
         crc ^= ((crc & 0xFF) << 5) & NAV_CRC16_INIT;
    }
    return (crc & NAV_CRC16_INIT);
}


static pixel_format_t convert_file_type(uint8_t file_type)
{
    switch (file_type)
    {
        case FILE_TYPE_PNG_VALUE:
            return PIXEL_FMT_PNG;
        case FILE_TYPE_JPEG_VALUE:
        default:
            return PIXEL_FMT_JPEG;
    }
}

static void reset_context(void)
{
    s_nav_ctx.active = false;
    s_nav_ctx.version = 0;
    s_nav_ctx.total_length = 0;
    s_nav_ctx.expected_packets = 0;
    s_nav_ctx.expected_crc = 0;
    s_nav_ctx.file_operation = 0;
    s_nav_ctx.file_type = 0;
    os_memset(s_nav_ctx.file_name, 0, sizeof(s_nav_ctx.file_name));
    s_nav_ctx.frame = NULL;
    s_nav_ctx.received_length = 0;
    s_nav_ctx.last_packet_num = 0;
    s_nav_ctx.running_crc = NAV_CRC16_INIT;
}

static void cleanup_frame(void)
{
    if (s_nav_ctx.frame)
    {
        media_frame_queue_free(s_nav_ctx.frame);
        s_nav_ctx.frame = NULL;
    }
    reset_context();
}

bk_err_t media_navigation_transfer_begin(const media_navigation_transfer_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        LOGE("begin: cfg is NULL\n");
        return BK_FAIL;
    }

    if ((cfg->total_length == 0) || (cfg->packet_count == 0))
    {
        LOGE("begin: invalid total_length %u or packet_count %u\n", cfg->total_length, cfg->packet_count);
        return BK_FAIL;
    }

    if (s_nav_ctx.active)
    {
        LOGW("begin: previous transfer still active, cleaning up\n");
        cleanup_frame();
    }

    frame_buffer_t *frame = media_frame_queue_malloc(cfg->total_length);
    if (frame == NULL)
    {
        LOGE("begin: malloc frame failed, size=%u\n", cfg->total_length);
        return BK_FAIL;
    }

    os_memset(frame->frame, 0, frame->size);

    reset_context();

    s_nav_ctx.active = true;
    s_nav_ctx.version = cfg->version;
    s_nav_ctx.total_length = cfg->total_length;
    s_nav_ctx.expected_packets = cfg->packet_count;
    s_nav_ctx.expected_crc = cfg->expected_crc;
    s_nav_ctx.file_operation = cfg->file_operation;
    s_nav_ctx.file_type = cfg->file_type;
    s_nav_ctx.frame = frame;
    s_nav_ctx.received_length = 0;
    s_nav_ctx.last_packet_num = 0;
    s_nav_ctx.running_crc = NAV_CRC16_INIT;

    os_memset(s_nav_ctx.file_name, 0, sizeof(s_nav_ctx.file_name));
    if (cfg->file_name && cfg->file_name_length > 0)
    {
        uint32_t copy_len = cfg->file_name_length;
        if (copy_len >= sizeof(s_nav_ctx.file_name))
        {
            copy_len = sizeof(s_nav_ctx.file_name) - 1;
        }
        os_memcpy(s_nav_ctx.file_name, cfg->file_name, copy_len);
    }

    LOGV("begin: version=%u, len=%u, packets=%u, crc=0x%04X\n", s_nav_ctx.version,
         (unsigned int)s_nav_ctx.total_length, s_nav_ctx.expected_packets, s_nav_ctx.expected_crc);

    return BK_OK;
}

bk_err_t media_navigation_transfer_cancel(void)
{
    if (!s_nav_ctx.active)
    {
        return BK_OK;
    }

    LOGW("cancel: abort current transfer (received=%u/%u)\n", (unsigned int)s_nav_ctx.received_length,
         (unsigned int)s_nav_ctx.total_length);
    cleanup_frame();

    return BK_OK;
}

bool media_navigation_transfer_is_active(void)
{
    return s_nav_ctx.active;
}

bk_err_t media_navigation_transfer_push(uint16_t packet_num, const uint8_t *data, uint16_t data_length, bool *frame_done)
{
    if (frame_done)
    {
        *frame_done = false;
    }

    if (!s_nav_ctx.active)
    {
        LOGE("push: transfer not active\n");
        return BK_FAIL;
    }

    if (packet_num == 0 || packet_num > s_nav_ctx.expected_packets)
    {
        LOGE("push: invalid packet number %u (expected 1-%u)\n", packet_num, s_nav_ctx.expected_packets);
        cleanup_frame();
        return BK_FAIL;
    }

    if ((s_nav_ctx.received_length + data_length) > s_nav_ctx.total_length)
    {
        LOGE("push: data overflow, received=%u, add=%u, total=%u\n",
             (unsigned int)s_nav_ctx.received_length, data_length, (unsigned int)s_nav_ctx.total_length);
        cleanup_frame();
        return BK_FAIL;
    }

    if ((data_length > 0) && (data != NULL))
    {
        os_memcpy(s_nav_ctx.frame->frame + s_nav_ctx.received_length, data, data_length);
        s_nav_ctx.running_crc = gen_crc16(s_nav_ctx.running_crc, data, data_length);
    }

    s_nav_ctx.received_length += data_length;
    s_nav_ctx.last_packet_num = packet_num;

    if (packet_num == s_nav_ctx.expected_packets)
    {
        if (s_nav_ctx.received_length != s_nav_ctx.total_length)
        {
            LOGE("push: length mismatch, received=%u, expected=%u\n",
                 (unsigned int)s_nav_ctx.received_length, (unsigned int)s_nav_ctx.total_length);
            cleanup_frame();
            return BK_FAIL;
        }

        uint16_t computed_crc = s_nav_ctx.running_crc;
        if ((s_nav_ctx.expected_crc != 0) && (computed_crc != s_nav_ctx.expected_crc))
        {
            LOGE("push: crc mismatch, computed=0x%04X, expected=0x%04X\n", computed_crc, s_nav_ctx.expected_crc);
            cleanup_frame();
            return BK_FAIL;
        }

        s_nav_ctx.frame->length = s_nav_ctx.received_length;
        s_nav_ctx.frame->fmt = convert_file_type(s_nav_ctx.file_type);
        s_nav_ctx.frame->timestamp = rtos_get_time();
        s_nav_ctx.frame->sequence = s_frame_sequence++;

        LOGI("push: frame #%u complete, length=%u\n",
             (unsigned int)(s_frame_sequence - 1), (unsigned int)s_nav_ctx.frame->length);

        bk_err_t ret = cast_jpeg_pipeline_push_frame_buffer(s_nav_ctx.frame);
        if (ret != BK_OK)
        {
            LOGE("push: cast pipeline push failed: %d\n", ret);
            media_frame_queue_free(s_nav_ctx.frame);
            s_nav_ctx.frame = NULL;
            reset_context();
            if (ret == BK_ERR_BUSY)
                rtos_delay_milliseconds(2);
            else
                rtos_delay_milliseconds(1);
            return ret;
        }

        s_nav_ctx.frame = NULL;
        reset_context();

        if (frame_done)
        {
            *frame_done = true;
        }

        return BK_OK;
    }

    return BK_OK;
}


