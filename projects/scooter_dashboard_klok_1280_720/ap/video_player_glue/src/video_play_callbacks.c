#include "video_play_callbacks.h"

#include <common/bk_include.h>
#include <components/bk_frame_buffer.h>
#include <os/mem.h>
#include <os/os.h>

#include "klok_h264_pp_osd.h"
#include "klok_lvgl_preview.h"

#define TAG "klok_video_cb"

#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES   (2048U)
#define VIDEO_FRAME_BUFFER_SAFETY_PAD_BYTES    (128U)
#define VIDEO_PLAY_MEM_SLAB_ALIGN_BYTES        (64U)

#define VIDEO_PLAY_DISPLAY_QUEUE_DEPTH         (1U)

typedef struct {
    void *pixel;
    uint32_t pixel_format;
    video_player_video_format_t video_format;
    uint16_t width;
    uint16_t height;
    uint64_t pts_ms;
} video_play_display_node_t;

static beken_queue_t s_display_queue = NULL;
static bool s_display_worker_ready = false;

static uint32_t video_play_slab_alloc_size(uint32_t payload_plus_pad)
{
    return (payload_plus_pad + VIDEO_PLAY_MEM_SLAB_ALIGN_BYTES - 1U) &
           ~(VIDEO_PLAY_MEM_SLAB_ALIGN_BYTES - 1U);
}

avdk_err_t video_play_audio_buffer_alloc_cb(void *user_data,
                                            video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0U) {
        return AVDK_ERR_INVAL;
    }

    uint32_t requested = buffer->length;
    buffer->data = os_malloc(requested + VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES);
    if (buffer->data == NULL) {
        buffer->length = 0U;
        return AVDK_ERR_NOMEM;
    }

    buffer->frame_buffer = NULL;
    buffer->length = requested;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

void video_play_audio_buffer_free_cb(void *user_data,
                                     video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL) {
        return;
    }

    if (buffer->data != NULL) {
        os_free(buffer->data);
    }
    buffer->data = NULL;
    buffer->length = 0U;
    buffer->pts = 0U;
    buffer->frame_buffer = NULL;
    buffer->user_data = NULL;
}

avdk_err_t video_play_video_packet_buffer_alloc_cb(void *user_data,
                                                    video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0U) {
        return AVDK_ERR_INVAL;
    }

    uint32_t requested = buffer->length;
    uint32_t alloc_size = video_play_slab_alloc_size(
        requested + VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES);
    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, alloc_size);
    if (frame == NULL) {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0U;
        return AVDK_ERR_NOMEM;
    }

    buffer->data = frame;
    buffer->frame_buffer = frame;
    buffer->length = requested;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

void video_play_video_packet_buffer_free_cb(void *user_data,
                                             video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL) {
        return;
    }

    if (buffer->frame_buffer != NULL) {
        bk_frame_buffer_free(buffer->frame_buffer);
    }
    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->length = 0U;
    buffer->pts = 0U;
    buffer->user_data = NULL;
}

avdk_err_t video_play_video_frame_buffer_alloc_cb(void *user_data,
                                                   video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0U) {
        return AVDK_ERR_INVAL;
    }

    uint32_t requested = buffer->length;
    uint32_t alloc_size = video_play_slab_alloc_size(
        requested + VIDEO_FRAME_BUFFER_SAFETY_PAD_BYTES);
    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, alloc_size);
    if (frame == NULL) {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0U;
        return AVDK_ERR_NOMEM;
    }

    buffer->data = frame;
    buffer->frame_buffer = NULL;
    buffer->length = requested;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

void video_play_video_frame_buffer_free_cb(void *user_data,
                                            video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL) {
        return;
    }

    if (buffer->data != NULL) {
        /*
         * A decoded H.264 PP-OSD frame can be dropped by the player after
         * decode (for example by A/V catch-up) before decode_complete_cb runs.
         * Release the provider's one-frame backpressure in that path.
         */
        klok_h264_pp_osd_cancel_pending_frame();
        bk_frame_buffer_free(buffer->data);
    }
    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->length = 0U;
    buffer->pts = 0U;
    buffer->user_data = NULL;
}

static void video_play_display_node_discard(const video_play_display_node_t *node)
{
    if (node != NULL && node->pixel != NULL) {
        if (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264) {
            klok_h264_pp_osd_cancel_pending_frame();
        }
        bk_frame_buffer_free(node->pixel);
    }
}

static void video_play_display_process_node(const video_play_display_node_t *node)
{
    if (node == NULL || node->pixel == NULL) {
        return;
    }

    if (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264 &&
        klok_h264_pp_osd_push_video_take(node->pixel,
                                         node->pixel_format,
                                         node->width,
                                         node->height,
                                         node->pts_ms)) {
        return;
    }

    if (klok_lvgl_preview_present(node->pixel,
                                  node->pixel_format,
                                  node->width,
                                  node->height)) {
        video_play_display_node_discard(node);
        return;
    }

    video_play_display_node_discard(node);
}

void video_play_display_process_one(void)
{
    if (!s_display_worker_ready || s_display_queue == NULL) {
        return;
    }

    video_play_display_node_t node;
    if (rtos_pop_from_queue(&s_display_queue, &node, BEKEN_NO_WAIT) == BK_OK) {
        video_play_display_process_node(&node);
    }
}

avdk_err_t video_play_display_worker_init(void)
{
    if (s_display_worker_ready) {
        return AVDK_ERR_OK;
    }

    if (rtos_init_queue(&s_display_queue,
                        "klok_osd_q",
                        sizeof(video_play_display_node_t),
                        VIDEO_PLAY_DISPLAY_QUEUE_DEPTH) != BK_OK) {
        s_display_queue = NULL;
        return AVDK_ERR_NOMEM;
    }

    s_display_worker_ready = true;
    return AVDK_ERR_OK;
}

void video_play_display_prepare_restart(void)
{
    s_display_worker_ready = false;
    if (s_display_queue != NULL) {
        video_play_display_node_t node;
        while (rtos_pop_from_queue(&s_display_queue,
                                   &node,
                                   BEKEN_NO_WAIT) == BK_OK) {
            video_play_display_node_discard(&node);
        }
    }
    klok_h264_pp_osd_cancel_pending_frame();
}

void video_play_display_resume_handoff(void)
{
    if (s_display_queue != NULL) {
        s_display_worker_ready = true;
    }
}

void video_play_display_worker_deinit(void)
{
    video_play_display_prepare_restart();
    if (s_display_queue != NULL) {
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
    }
}

void video_play_video_decode_complete_cb(void *user_data,
                                         const video_player_video_frame_meta_t *meta,
                                         video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->data == NULL) {
        return;
    }

    void *pixel = buffer->data;
    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->length = 0U;

    if (meta == NULL || !s_display_worker_ready || s_display_queue == NULL) {
        klok_h264_pp_osd_cancel_pending_frame();
        bk_frame_buffer_free(pixel);
        return;
    }

    video_play_display_node_t node = {
        .pixel = pixel,
        .pixel_format = (uint32_t)meta->output_format,
        .video_format = meta->video.format,
        .width = (uint16_t)meta->video.width,
        .height = (uint16_t)meta->video.height,
        .pts_ms = meta->pts_ms,
    };

    if (rtos_is_queue_full(&s_display_queue)) {
        video_play_display_node_t old_node;
        if (rtos_pop_from_queue(&s_display_queue, &old_node, BEKEN_NO_WAIT) == BK_OK) {
            video_play_display_node_discard(&old_node);
        }
    }

    if (rtos_push_to_queue(&s_display_queue, &node, BEKEN_NO_WAIT) != BK_OK) {
        video_play_display_node_discard(&node);
    }
}

void video_play_audio_decode_complete_cb(void *user_data,
                                         const video_player_audio_packet_meta_t *meta,
                                         video_player_buffer_t *buffer)
{
    (void)meta;

    if (buffer == NULL || buffer->data == NULL || buffer->length == 0U) {
        return;
    }

    video_play_user_ctx_t *ctx = user_data;
    if (ctx != NULL && ctx->audio_player_handle != NULL) {
        int32_t written = audio_player_device_write_frame_data(
            ctx->audio_player_handle,
            (char *)buffer->data,
            buffer->length);
        if (written < 0) {
            LOGW("audio write failed, ret=%d\n", written);
        }
    }

    os_free(buffer->data);
    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->length = 0U;
    buffer->pts = 0U;
}
