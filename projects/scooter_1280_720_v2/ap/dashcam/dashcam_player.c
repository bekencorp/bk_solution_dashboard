#include "dashcam_player.h"

#include <string.h>
#include <cache.h>
#include "common/avdk_pixel_types.h"
#include "components/bk_decode/bk_pp_ctlr.h"
#include "components/bk_frame_buffer.h"
#include "components/bk_video_player/bk_video_player_engine.h"
#include "components/bk_video_player/bk_video_player_types.h"
#include "components/bk_video_player/container_parser/bk_video_player_avi_parser.h"
#include "components/bk_video_player/container_parser/bk_video_player_mp4_parser.h"
#include "components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h"
#include "components/bk_video_player/video_decoder/bk_video_player_hw_jpeg_decoder.h"
#include "components/log.h"
#include "dashcam_config.h"
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

/* One-shot NV12 diagnostic probe, re-armed at the start of every playback so a
 * with-recording vs. without-recording comparison needs no reboot/reflash. */
static bool s_nv12_probed = false;

/*
 * Standalone PP (VCDEC post-processor) path. The HW H.264 decoder outputs NV12
 * at source size (1280x720); the standalone PP then does NV12 -> RGB565 with
 * hardware down-scale to the playback size in one pass, straight into an RGB565
 * slab that drops onto the LVGL RGB565 canvas. This replaces the decoder's own
 * RGB565 output path (which produced green-striped frames on this platform).
 * The PP is opened lazily on the first decoded frame and torn down on stop.
 */
#define DASHCAM_PP_ALIGN16(v)  (((uint32_t)(v) + 15U) & ~15U)

static bk_pp_ctlr_handle_t s_pp = NULL;
static bool s_pp_opened = false;
static void *s_pp_rgb565 = NULL;
static uint32_t s_pp_rgb565_size = 0;

static void dashcam_player_pp_close(void)
{
    if (s_pp != NULL)
    {
        if (s_pp_opened)
        {
            (void)bk_pp_close(s_pp);
        }
        (void)bk_pp_deinit(s_pp);
        (void)bk_pp_delete(s_pp);
        s_pp = NULL;
    }
    s_pp_opened = false;

    if (s_pp_rgb565 != NULL)
    {
        bk_frame_buffer_free(s_pp_rgb565);
        s_pp_rgb565 = NULL;
        s_pp_rgb565_size = 0;
    }
}

static bk_err_t dashcam_player_pp_ensure(void)
{
    if (s_pp_opened)
    {
        return BK_OK;
    }

    bk_pp_config_t cfg = DEFAULT_PP_CONFIG;
    cfg.timeout_ms = 1000U;

    avdk_err_t ret = bk_pp_ctlr_new(&s_pp, &cfg);
    if (ret != AVDK_ERR_OK || s_pp == NULL)
    {
        LOGE("bk_pp_ctlr_new failed: %d\n", ret);
        s_pp = NULL;
        return BK_FAIL;
    }

    ret = bk_pp_init(s_pp);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_pp_init failed: %d\n", ret);
        (void)bk_pp_delete(s_pp);
        s_pp = NULL;
        return BK_FAIL;
    }

    ret = bk_pp_open(s_pp);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_pp_open failed: %d\n", ret);
        (void)bk_pp_deinit(s_pp);
        (void)bk_pp_delete(s_pp);
        s_pp = NULL;
        return BK_FAIL;
    }

    s_pp_opened = true;

    /* RGB565 output storage height is 16-aligned for the PP hardware; the valid
     * frame is still packed at DASHCAM_PLAYBACK_WIDTH x HEIGHT (stride = W*2). */
    s_pp_rgb565_size = DASHCAM_PLAYBACK_WIDTH *
                       DASHCAM_PP_ALIGN16(DASHCAM_PLAYBACK_HEIGHT) * 2U;
    s_pp_rgb565 = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, s_pp_rgb565_size);
    if (s_pp_rgb565 == NULL)
    {
        LOGE("alloc PP RGB565 buffer (%u) failed\n", (unsigned)s_pp_rgb565_size);
        dashcam_player_pp_close();
        return BK_ERR_NO_MEM;
    }

    LOGI("PP opened: NV12 -> RGB565 %ux%u (buf %u bytes)\n",
         (unsigned)DASHCAM_PLAYBACK_WIDTH, (unsigned)DASHCAM_PLAYBACK_HEIGHT,
         (unsigned)s_pp_rgb565_size);
    return BK_OK;
}

/*
 * Decoded frames and compressed packets are far too large (a 640x360 frame is
 * ~340 KB) to come from the regular SRAM heap; they must be allocated from the
 * DMA-capable mem_slab frame-buffer heaps, like the SDK video_player example.
 * The HW H264 decoder also writes its output via DMA, so cache-aligned slab
 * memory is required.
 */
#define DASHCAM_PLAYER_PACKET_PAD_BYTES   2048U
#define DASHCAM_PLAYER_FRAME_PAD_BYTES    128U
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
    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED,
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

/* Decoded output frames -> uncoded slab heap (DMA-capable, cache-aligned). */
static avdk_err_t dashcam_player_frame_alloc_cb(void *user_data,
                                                video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0)
    {
        return AVDK_ERR_INVAL;
    }

    const uint32_t requested = buffer->length;
    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                         dashcam_player_slab_size(requested + DASHCAM_PLAYER_FRAME_PAD_BYTES));
    if (frame == NULL)
    {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }

    buffer->data = frame;
    buffer->frame_buffer = NULL;
    buffer->length = requested;
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
        (void)bk_video_player_hw_h264_decoder_free_output_frame(buffer->data);
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

    /*
     * The HW H264 decoder produced an NV12 frame at the source size
     * (meta->video.width/height, e.g. 1280x720). Run the standalone PP to
     * down-scale + convert NV12 -> RGB565 at the playback size (== the canvas),
     * then hand the RGB565 frame to the sink. dashcam_video_on_frame copies out
     * of s_pp_rgb565 before we free the decoder buffer below, so a single PP
     * output buffer is reused for every frame.
     */
    if (meta != NULL && buffer != NULL &&
        buffer->data != NULL &&
        meta->output_format == PIXEL_FMT_NV12 &&
        meta->video.width > 0U && meta->video.height > 0U &&
        dashcam_player_pp_ensure() == BK_OK)
    {
        const uint32_t in_w = meta->video.width;
        const uint32_t in_h = meta->video.height;

        /*
         * One-shot probe of the decoder's NV12 output BEFORE the PP runs, to
         * localise the green-stripe corruption. The decoder wrote this buffer by
         * DMA, so invalidate D-cache before the CPU samples it. Report mean Y and
         * mean Cb/Cr: healthy chroma sits near 128; ~0/128-flat green output means
         * the PP is mis-reading chroma; striped/garbage Y means the decode/NV12
         * itself is wrong. Also sample Y at a few rows to spot row banding.
         */
        if (!s_nv12_probed)
        {
            s_nv12_probed = true;
            const uint8_t *y = (const uint8_t *)buffer->data;
            const uint8_t *c = y + (size_t)in_w * in_h;
            flush_dcache((void *)buffer->data, (long)((size_t)in_w * in_h * 3U / 2U));

            uint64_t ysum = 0;
            uint32_t ycnt = 0;
            for (uint32_t row = 0; row < in_h; row += 16U)
            {
                const uint8_t *yr = y + (size_t)row * in_w;
                uint32_t rsum = 0;
                for (uint32_t col = 0; col < in_w; col += 8U)
                {
                    rsum += yr[col];
                }
                uint32_t rn = (in_w + 7U) / 8U;
                ysum += rsum;
                ycnt += rn;
                if (row < 96U)
                {
                    LOGI("nv12 Yrow[%u] mean=%u\n", (unsigned)row,
                         (unsigned)(rsum / rn));
                }
            }

            uint64_t cbsum = 0, crsum = 0;
            uint32_t ccnt = 0;
            const uint32_t ch = in_h / 2U;
            for (uint32_t row = 0; row < ch; row += 16U)
            {
                const uint8_t *cr_ = c + (size_t)row * in_w;
                for (uint32_t col = 0; col < in_w; col += 16U)
                {
                    cbsum += cr_[col];
                    crsum += cr_[col + 1U];
                    ccnt++;
                }
            }
            LOGI("nv12 probe %ux%u Ymean=%u Cbmean=%u Crmean=%u\n",
                 (unsigned)in_w, (unsigned)in_h,
                 (unsigned)(ycnt ? ysum / ycnt : 0),
                 (unsigned)(ccnt ? cbsum / ccnt : 0),
                 (unsigned)(ccnt ? crsum / ccnt : 0));
        }

        bk_pp_process_req_t req = {0};
        req.in_y = buffer->data;
        req.in_c = (uint8_t *)buffer->data + (in_w * in_h);
        req.in_width = (uint16_t)in_w;
        req.in_height = (uint16_t)in_h;
        req.in_format = BK_PIXEL_FORMAT_NV12;
        req.out_width = DASHCAM_PLAYBACK_WIDTH;
        req.out_height = DASHCAM_PLAYBACK_HEIGHT;
        req.out_buffer = s_pp_rgb565;
        req.out_size = s_pp_rgb565_size;
        req.out_format = BK_PIXEL_FORMAT_RGB565;

        avdk_err_t pret = bk_pp_process(s_pp, &req);
        if (pret == AVDK_ERR_OK)
        {
            dashcam_video_on_frame(s_pp_rgb565,
                                   DASHCAM_PLAYBACK_WIDTH,
                                   DASHCAM_PLAYBACK_HEIGHT,
                                   DASHCAM_VIDEO_FRAME_FORMAT_RGB565);
        }
        else
        {
            LOGW("pp process failed: %d (%ux%u -> %ux%u)\n", pret,
                 (unsigned)in_w, (unsigned)in_h,
                 (unsigned)DASHCAM_PLAYBACK_WIDTH, (unsigned)DASHCAM_PLAYBACK_HEIGHT);
        }
    }

    dashcam_player_frame_free_cb(NULL, buffer);
}

static void dashcam_player_finished_cb(void *user_data, const char *file_path)
{
    (void)user_data;

    LOGI("playback finished: %s\n", file_path != NULL ? file_path : "");
    s_player_playing = false;
}

static void dashcam_player_register_modules(bk_video_player_engine_handle_t handle)
{
    video_player_container_parser_ops_t *avi_parser = bk_video_player_get_avi_parser_ops();
    video_player_container_parser_ops_t *mp4_parser = bk_video_player_get_mp4_parser_ops();
    video_player_video_decoder_ops_t *jpeg_decoder = bk_video_player_get_hw_jpeg_decoder_ops();

    if (avi_parser != NULL)
    {
        (void)bk_video_player_engine_register_container_parser(handle, avi_parser);
    }
    if (mp4_parser != NULL)
    {
        (void)bk_video_player_engine_register_container_parser(handle, mp4_parser);
    }
    if (jpeg_decoder != NULL)
    {
        (void)bk_video_player_engine_register_video_decoder(handle, jpeg_decoder);
    }

#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
    video_player_video_decoder_ops_t *h264_decoder =
        bk_video_player_get_hw_h264_decoder_frame_ops();
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
    config.video.buffer_alloc_cb = dashcam_player_frame_alloc_cb;
    config.video.buffer_free_cb = dashcam_player_frame_free_cb;
    config.video.decode_complete_cb = dashcam_player_video_done_cb;
    /*
     * Ask the decoder for plain NV12 at source size. The decode-complete callback
     * runs the standalone PP (bk_pp_ctlr) to down-scale + convert NV12 -> RGB565
     * at the playback size, so we deliberately do NOT request the decoder's own
     * RGB565/PP output path (which produced corrupt green-striped frames here).
     * display_width/height = 0 keeps the decoder at source geometry.
     */
    config.video.output_format = PIXEL_FMT_NV12;
    config.video.display_width = 0;
    config.video.display_height = 0;
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

    s_nv12_probed = false;

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

    dashcam_player_pp_close();
    return (ret == AVDK_ERR_OK) ? BK_OK : BK_FAIL;
}

bool dashcam_player_is_playing(void)
{
    return s_player_playing;
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
