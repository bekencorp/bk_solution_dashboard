#include "dashcam_player.h"

#include <string.h>
#include "common/avdk_pixel_types.h"
#include "components/bk_frame_buffer.h"
#include "components/bk_video_player/bk_video_player_engine.h"
#include "components/bk_video_player/bk_video_player_types.h"
#include "components/bk_video_player/container_parser/bk_video_player_avi_parser.h"
#include "components/bk_video_player/container_parser/bk_video_player_mp4_parser.h"
#include "components/log.h"
#include "dashcam_config.h"
#include "dashcam_h264_flexa_decoder.h"
#include "dashcam_video.h"
#include "os/mem.h"

#define TAG "d_player"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

static bk_video_player_engine_handle_t s_player = NULL;
static bool s_player_opened = false;
static bool s_player_playing = false;

/*
 * Decoded frames and compressed packets are far too large (a 640x360 frame is
 * ~340 KB) to come from the regular SRAM heap; they must be allocated from the
 * DMA-capable mem_slab frame-buffer heaps, like the SDK video_player example.
 * The HW H264 decoder also writes its output via DMA, so cache-aligned slab
 * memory is required.
 */
#define DASHCAM_PLAYER_PACKET_PAD_BYTES   2048U
#define DASHCAM_PLAYER_SLAB_ALIGN_BYTES   64U

static inline uint32_t dashcam_player_slab_size(uint32_t payload_plus_pad)
{
    return (payload_plus_pad + DASHCAM_PLAYER_SLAB_ALIGN_BYTES - 1U) &
           ~(DASHCAM_PLAYER_SLAB_ALIGN_BYTES - 1U);
}

/* Compressed bitstream packets -> coded slab heap. */
static avdk_err_t dashcam_player_packet_alloc_cb(void *user_data,
                                                 video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0)
    {
        return AVDK_ERR_INVAL;
    }

    const uint32_t requested = buffer->length;
    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                         dashcam_player_slab_size(requested + DASHCAM_PLAYER_PACKET_PAD_BYTES));
    if (frame == NULL)
    {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }

    buffer->data = frame;
    buffer->frame_buffer = frame;
    buffer->length = requested;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

static void dashcam_player_packet_free_cb(void *user_data,
                                          video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->frame_buffer != NULL)
    {
        bk_frame_buffer_free(buffer->frame_buffer);
        buffer->frame_buffer = NULL;
    }

    buffer->data = NULL;
    buffer->length = 0;
    buffer->user_data = NULL;
}

/* The player core requires a non-NULL allocation callback, but the private
 * Flexa decoder replaces the output with its GPU-owned compressed frame. */
static avdk_err_t dashcam_player_frame_prepare_cb(void *user_data,
                                                  video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

static void dashcam_player_frame_free_cb(void *user_data,
                                         video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->data != NULL)
    {
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
        (void)dashcam_h264_flexa_decoder_free_output_frame(buffer->data);
#else
        bk_frame_buffer_free(buffer->data);
#endif
    }

    buffer->data = NULL;
    buffer->frame_buffer = NULL;
    buffer->length = 0;
    buffer->user_data = NULL;
}

static void dashcam_player_video_done_cb(void *user_data,
                                         const video_player_video_frame_meta_t *meta,
                                         video_player_buffer_t *buffer)
{
    (void)user_data;

    if (meta != NULL && buffer != NULL &&
        buffer->data != NULL &&
        meta->output_format == PIXEL_FMT_ARGB8888 &&
        dashcam_video_submit_owned_frame(buffer->data,
                                         DASHCAM_PLAYBACK_WIDTH,
                                         DASHCAM_PLAYBACK_HEIGHT,
                                         DASHCAM_VIDEO_FRAME_FORMAT_ARGB8888))
    {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0;
    }

    dashcam_player_frame_free_cb(NULL, buffer);
}

static void dashcam_player_finished_cb(void *user_data, const char *file_path)
{
    (void)user_data;

    LOGI("playback finished: %s\n", file_path != NULL ? file_path : "");
    s_player_playing = false;
    dashcam_video_stop();
}

static void dashcam_player_register_modules(bk_video_player_engine_handle_t handle)
{
    video_player_container_parser_ops_t *avi_parser = bk_video_player_get_avi_parser_ops();
    video_player_container_parser_ops_t *mp4_parser = bk_video_player_get_mp4_parser_ops();

    if (avi_parser != NULL)
    {
        (void)bk_video_player_engine_register_container_parser(handle, avi_parser);
    }
    if (mp4_parser != NULL)
    {
        (void)bk_video_player_engine_register_container_parser(handle, mp4_parser);
    }
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
    video_player_video_decoder_ops_t *h264_decoder =
        dashcam_get_h264_flexa_decoder_ops();
    if (h264_decoder != NULL)
    {
        (void)bk_video_player_engine_register_video_decoder(handle, h264_decoder);
    }
#endif
}

static bk_err_t dashcam_player_open(void)
{
    bk_video_player_config_t config = {0};
    avdk_err_t ret;

    if (s_player_opened)
    {
        return BK_OK;
    }

    config.video.packet_buffer_alloc_cb = dashcam_player_packet_alloc_cb;
    config.video.packet_buffer_free_cb = dashcam_player_packet_free_cb;
    config.video.buffer_alloc_cb = dashcam_player_frame_prepare_cb;
    config.video.buffer_free_cb = dashcam_player_frame_free_cb;
    config.video.decode_complete_cb = dashcam_player_video_done_cb;
    config.video.output_format = PIXEL_FMT_ARGB8888;
    config.video.display_width = DASHCAM_PLAYBACK_WIDTH;
    config.video.display_height = DASHCAM_PLAYBACK_HEIGHT;
    config.video.rotate_degree = DASHCAM_PLAYBACK_ROTATION;
    config.playback_finished_cb = dashcam_player_finished_cb;

    ret = bk_video_player_engine_new(&s_player, &config);
    if (ret != AVDK_ERR_OK || s_player == NULL)
    {
        LOGE("bk_video_player_engine_new failed: %d\n", ret);
        s_player = NULL;
        return BK_FAIL;
    }

    dashcam_player_register_modules(s_player);

    ret = bk_video_player_engine_open(s_player);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_video_player_engine_open failed: %d\n", ret);
        bk_video_player_engine_delete(s_player);
        s_player = NULL;
        return BK_FAIL;
    }

    s_player_opened = true;
    return BK_OK;
}

bk_err_t dashcam_player_play(const char *path)
{
    avdk_err_t ret;

    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }

    if (dashcam_player_open() != BK_OK)
    {
        return BK_FAIL;
    }

    ret = bk_video_player_engine_play_file(s_player, path);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("play_file failed: %d, path=%s\n", ret, path);
        return BK_FAIL;
    }

    s_player_playing = true;
    LOGI("playback started: %s\n", path);
    return BK_OK;
}

bk_err_t dashcam_player_stop(void)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_player == NULL)
    {
        return BK_OK;
    }

    /* Playback-only pause of the engine. Callers that hand display ownership
     * back to LVGL must use dashcam_player_close() instead, so decoder/GPU
     * teardown completes before LVGL reacquires the shared VG-Lite context. */
    if (s_player_playing)
    {
        ret = bk_video_player_engine_stop(s_player);
        s_player_playing = false;
    }

    return (ret == AVDK_ERR_OK) ? BK_OK : BK_FAIL;
}

bk_err_t dashcam_player_close(void)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_player == NULL)
    {
        return BK_OK;
    }

    if (s_player_playing)
    {
        ret = bk_video_player_engine_stop(s_player);
        s_player_playing = false;
    }

    if (s_player_opened)
    {
        (void)bk_video_player_engine_close(s_player);
        s_player_opened = false;
    }

    (void)bk_video_player_engine_delete(s_player);
    s_player = NULL;

    return (ret == AVDK_ERR_OK) ? BK_OK : BK_FAIL;
}

bool dashcam_player_is_playing(void)
{
    return s_player_playing;
}

bool dashcam_player_is_open(void)
{
    return s_player_opened;
}

bk_err_t dashcam_player_get_media_info(dashcam_player_info_t *info)
{
    video_player_media_info_t media = {0};
    avdk_err_t ret;

    if (info == NULL)
    {
        return BK_ERR_PARAM;
    }

    memset(info, 0, sizeof(*info));

    if (s_player == NULL)
    {
        return BK_FAIL;
    }

    ret = bk_video_player_engine_get_media_info(s_player, NULL, &media);
    if (ret != AVDK_ERR_OK)
    {
        LOGD("get_media_info failed: %d\n", ret);
        return BK_FAIL;
    }

    info->width = media.video.width;
    info->height = media.video.height;
    info->fps = media.video.fps;
    info->duration_ms = media.duration_ms;
    info->file_size_bytes = media.file_size_bytes;
    info->position_ms = dashcam_player_get_position_ms();

    LOGD("media info %ux%u fps=%u dur=%llums\n",
         (unsigned)info->width, (unsigned)info->height, (unsigned)info->fps,
         (unsigned long long)info->duration_ms);
    return BK_OK;
}

uint64_t dashcam_player_get_position_ms(void)
{
    uint64_t time_ms = 0;

    if (s_player == NULL || !s_player_playing)
    {
        return 0;
    }

    if (bk_video_player_engine_get_current_time(s_player, &time_ms) != AVDK_ERR_OK)
    {
        return 0;
    }

    return time_ms;
}
