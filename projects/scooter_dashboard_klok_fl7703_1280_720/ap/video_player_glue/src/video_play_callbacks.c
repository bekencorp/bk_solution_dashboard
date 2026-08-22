#include "video_play_callbacks.h"

#include <common/bk_include.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h>
#include <os/mem.h>
#include <os/os.h>

#include "klok_h264_pp_osd.h"
#include "klok_lvgl_preview.h"
#include "lv_vendor.h"

#define TAG "klok_video_cb"

#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES   (2048U)
#define VIDEO_FRAME_BUFFER_SAFETY_PAD_BYTES    (128U)
#define VIDEO_PLAY_MEM_SLAB_ALIGN_BYTES        (64U)
#define VIDEO_FLEXA_PLACEHOLDER_BYTES           (128U)

#define VIDEO_PLAY_DISPLAY_QUEUE_DEPTH         (1U)
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
#define VIDEO_PLAY_DISPLAY_WORKER_STACK_SIZE   (8U * 1024U)
#define VIDEO_PLAY_DISPLAY_WORKER_PRIORITY     (BEKEN_DEFAULT_WORKER_PRIORITY)
#define VIDEO_PLAY_DISPLAY_POP_TIMEOUT_MS      (50U)
#endif

typedef struct {
    enum {
        VIDEO_PLAY_FRAME_OWNER_SLAB = 0,
        VIDEO_PLAY_FRAME_OWNER_FLEXA,
    } owner;
    void *pixel;
    uint32_t pixel_format;
    video_player_video_format_t video_format;
    uint16_t width;
    uint16_t height;
    uint64_t pts_ms;
    uint32_t generation;
    bool frame_preview_mode;
} video_play_display_node_t;

static const uint8_t s_frame_slab_owner = 0U;
static const uint8_t s_flexa_output_owner = 0U;
static beken_queue_t s_display_queue = NULL;
static bool s_display_worker_ready = false;
static volatile uint32_t s_display_generation = 1U;
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
static beken_thread_t s_display_thread = NULL;
static beken_semaphore_t s_display_exit_sem = NULL;
static volatile bool s_display_worker_exit = false;
static volatile bool s_preview_call_pending = false;
#endif

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
    buffer->user_data = (void *)&s_frame_slab_owner;
    return AVDK_ERR_OK;
}

avdk_err_t video_play_video_flexa_placeholder_alloc_cb(void *user_data,
                                                        video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL) {
        return AVDK_ERR_INVAL;
    }

    /*
     * Flexa allocates its compressed ARGB8888 result internally and replaces
     * this engine-owned buffer after GPU completion. Avoid reserving an unused
     * full RGB565 frame for every decode.
     */
    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                         VIDEO_FLEXA_PLACEHOLDER_BYTES);
    if (frame == NULL) {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0U;
        return AVDK_ERR_NOMEM;
    }

    buffer->data = frame;
    buffer->frame_buffer = NULL;
    buffer->length = VIDEO_FLEXA_PLACEHOLDER_BYTES;
    buffer->user_data = (void *)&s_flexa_output_owner;
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
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
        if (buffer->user_data == (void *)&s_flexa_output_owner) {
            (void)bk_video_player_hw_h264_decoder_free_output_frame(buffer->data);
        } else {
            if (klok_h264_pp_osd_is_pp_active()) {
                klok_h264_pp_osd_cancel_pending_frame();
            }
            bk_frame_buffer_free(buffer->data);
        }
#else
        /*
         * A decoded H.264 PP-OSD frame can be dropped by the player after
         * decode (for example by A/V catch-up) before decode_complete_cb runs.
         * Release the provider's one-frame backpressure in that path.
         */
        klok_h264_pp_osd_cancel_pending_frame();
        bk_frame_buffer_free(buffer->data);
#endif
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
        if (node->owner == VIDEO_PLAY_FRAME_OWNER_FLEXA) {
            (void)bk_video_player_hw_h264_decoder_free_output_frame(node->pixel);
            return;
        }
#if !KLOK_VIDEO_FLEXA_DIRECT_MODE
        if (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264) {
            klok_h264_pp_osd_cancel_pending_frame();
        }
#else
        if (node->frame_preview_mode &&
            node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264 &&
            klok_h264_pp_osd_is_pp_active()) {
            klok_h264_pp_osd_cancel_pending_frame();
        }
#endif
        bk_frame_buffer_free(node->pixel);
    }
}

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
static void video_play_preview_disp_cb(void *user_data)
{
    video_play_display_node_t *node =
        (video_play_display_node_t *)user_data;
    s_preview_call_pending = false;

    if (node == NULL) {
        return;
    }
    if (node->generation == s_display_generation &&
        s_display_worker_ready &&
        node->frame_preview_mode) {
        if (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264 &&
            klok_h264_pp_osd_is_pp_active() &&
            klok_h264_pp_osd_push_video_take(node->pixel,
                                             node->pixel_format,
                                             node->width,
                                             node->height,
                                             node->pts_ms)) {
            node->pixel = NULL;
        } else if (node->owner == VIDEO_PLAY_FRAME_OWNER_SLAB &&
            klok_lvgl_preview_present_fullscreen_take(node->pixel,
                                                      node->pixel_format,
                                                      node->width,
                                                      node->height)) {
            node->pixel = NULL;
        } else {
            (void)klok_lvgl_preview_present(node->pixel,
                                            node->pixel_format,
                                            node->width,
                                            node->height);
        }
    }
    video_play_display_node_discard(node);
    os_free(node);
}
#endif

static void video_play_display_process_node(const video_play_display_node_t *node)
{
    if (node == NULL || node->pixel == NULL) {
        return;
    }
    if (node->generation != s_display_generation) {
        video_play_display_node_discard(node);
        return;
    }

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (!node->frame_preview_mode &&
        node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264 &&
        node->pixel_format == PIXEL_FMT_ARGB8888) {
        if (!klok_h264_flexa_display_take(node->pixel)) {
            video_play_display_node_discard(node);
        }
        return;
    }

    if (node->frame_preview_mode) {
        if (s_preview_call_pending) {
            video_play_display_node_discard(node);
            return;
        }

        video_play_display_node_t *pending =
            (video_play_display_node_t *)os_malloc(sizeof(*pending));
        if (pending == NULL) {
            video_play_display_node_discard(node);
            return;
        }
        *pending = *node;
        s_preview_call_pending = true;
        /*
         * This dispatch is Klok-specific, so keep it out of the common
         * lv_vendor API. Protect lv_async_call's internal LVGL list while the
         * display worker enqueues the preview callback; the callback itself
         * will run later from lv_task_handler.
         */
        lv_vendor_disp_lock();
        lv_result_t async_ret =
            lv_async_call(video_play_preview_disp_cb, pending);
        lv_vendor_disp_unlock();
        if (async_ret != LV_RESULT_OK) {
            s_preview_call_pending = false;
            video_play_display_node_discard(pending);
            os_free(pending);
        }
        return;
    }
#else
    if (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264 &&
        klok_h264_pp_osd_push_video_take(node->pixel,
                                         node->pixel_format,
                                         node->width,
                                         node->height,
                                         node->pts_ms)) {
        return;
    }
#endif

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

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
static void video_play_display_worker_thread(void *arg)
{
    (void)arg;

    while (!s_display_worker_exit) {
        video_play_display_node_t node;
        if (rtos_pop_from_queue(&s_display_queue,
                                &node,
                                VIDEO_PLAY_DISPLAY_POP_TIMEOUT_MS) == BK_OK) {
            if (s_display_worker_ready) {
                video_play_display_process_node(&node);
            } else {
                video_play_display_node_discard(&node);
            }
        }
    }

    video_play_display_node_t node;
    while (rtos_pop_from_queue(&s_display_queue, &node, BEKEN_NO_WAIT) == BK_OK) {
        video_play_display_node_discard(&node);
    }
    (void)rtos_set_semaphore(&s_display_exit_sem);
    rtos_delete_thread(NULL);
}
#endif

avdk_err_t video_play_display_worker_init(void)
{
    if (s_display_worker_ready) {
        return AVDK_ERR_OK;
    }

    if (rtos_init_queue(&s_display_queue,
                        "klok_video_q",
                        sizeof(video_play_display_node_t),
                        VIDEO_PLAY_DISPLAY_QUEUE_DEPTH) != BK_OK) {
        s_display_queue = NULL;
        return AVDK_ERR_NOMEM;
    }

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    s_display_worker_exit = false;
    if (rtos_init_semaphore(&s_display_exit_sem, 1) != BK_OK) {
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
        return AVDK_ERR_NOMEM;
    }
    if (rtos_create_thread(&s_display_thread,
                           VIDEO_PLAY_DISPLAY_WORKER_PRIORITY,
                           "klok_flexa_disp",
                           (beken_thread_function_t)video_play_display_worker_thread,
                           VIDEO_PLAY_DISPLAY_WORKER_STACK_SIZE,
                           NULL) != BK_OK) {
        rtos_deinit_semaphore(&s_display_exit_sem);
        s_display_exit_sem = NULL;
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
        return AVDK_ERR_GENERIC;
    }
#endif

    s_display_worker_ready = true;
    return AVDK_ERR_OK;
}

void video_play_display_prepare_restart(void)
{
    s_display_worker_ready = false;
    s_display_generation++;
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
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    s_display_worker_exit = true;
    if (s_display_thread != NULL && s_display_exit_sem != NULL) {
        (void)rtos_get_semaphore(&s_display_exit_sem, BEKEN_WAIT_FOREVER);
        s_display_thread = NULL;
    }
    if (s_display_exit_sem != NULL) {
        rtos_deinit_semaphore(&s_display_exit_sem);
        s_display_exit_sem = NULL;
    }
#endif
    if (s_display_queue != NULL) {
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
    }
}

void video_play_video_decode_complete_cb(void *user_data,
                                         const video_player_video_frame_meta_t *meta,
                                         video_player_buffer_t *buffer)
{
    if (buffer == NULL || buffer->data == NULL) {
        return;
    }

    video_play_user_ctx_t *ctx = (video_play_user_ctx_t *)user_data;
    bool frame_preview_mode =
        ctx != NULL && ctx->frame_preview_mode;
    void *pixel = buffer->data;
    void *buffer_owner = buffer->user_data;
    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->length = 0U;

    if (meta == NULL || !s_display_worker_ready || s_display_queue == NULL) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
        if (buffer_owner == (void *)&s_flexa_output_owner) {
            (void)bk_video_player_hw_h264_decoder_free_output_frame(pixel);
        } else {
            if (frame_preview_mode && klok_h264_pp_osd_is_pp_active()) {
                klok_h264_pp_osd_cancel_pending_frame();
            }
            bk_frame_buffer_free(pixel);
        }
#else
        klok_h264_pp_osd_cancel_pending_frame();
        bk_frame_buffer_free(pixel);
#endif
        return;
    }

    video_play_display_node_t node = {
        .owner = meta->output_format == PIXEL_FMT_ARGB8888 ||
                         buffer_owner == (void *)&s_flexa_output_owner
                     ? VIDEO_PLAY_FRAME_OWNER_FLEXA
                     : VIDEO_PLAY_FRAME_OWNER_SLAB,
        .pixel = pixel,
        .pixel_format = (uint32_t)meta->output_format,
        .video_format = meta->video.format,
        .width = (uint16_t)meta->video.width,
        .height = (uint16_t)meta->video.height,
        .pts_ms = meta->pts_ms,
        .generation = s_display_generation,
        .frame_preview_mode = frame_preview_mode,
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
