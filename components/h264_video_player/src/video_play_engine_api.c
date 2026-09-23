#include "video_play_engine_api.h"

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdio.h>

#include "audio_player_device.h"
#include "bk_partition.h"
#include "bk_posix.h"
#include "components/bk_video_player/bk_video_player_engine.h"
#include "components/bk_video_player/container_parser/bk_video_player_avi_parser.h"
#include "components/bk_video_player/container_parser/bk_video_player_mp4_parser.h"
#include "components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h"
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_AAC_AUDIO_DECODER
#include "components/bk_video_player/audio_decoder/bk_video_player_aac_decoder.h"
#endif
#include "video_play_callbacks.h"

#define TAG "klok_video_engine"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#define KLOK_VIDEO_WIDTH             (1280U)
#define KLOK_VIDEO_HEIGHT            (720U)
#define KLOK_VIDEO_DEFAULT_VOLUME    (8U)
#define KLOK_VIDEO_PATH_MAX          (256U)

static bk_video_player_engine_handle_t s_player = NULL;
static bool s_player_opened = false;
static bool s_sd_mounted = false;
static audio_player_device_handle_t s_audio_player = NULL;
static video_play_user_ctx_t s_play_ctx;
static video_play_output_mode_t s_output_mode = VIDEO_PLAY_OUTPUT_FLEXA_DIRECT;
static video_play_output_mode_t s_switch_target_mode = VIDEO_PLAY_OUTPUT_FLEXA_DIRECT;
static video_play_output_mode_t s_switch_source_mode = VIDEO_PLAY_OUTPUT_FLEXA_DIRECT;
static bool s_output_switching = false;
static bool s_video_switch_prepared = false;
static video_player_video_decoder_ops_t *s_flexa_decoder_ops = NULL;
static video_player_video_decoder_ops_t *s_frame_decoder_ops = NULL;
static char s_current_file_path[KLOK_VIDEO_PATH_MAX];
static video_play_finished_cb_t s_finished_callback = NULL;
static void *s_finished_user_data = NULL;

static void video_play_finished_cb(void *user_data, const char *file_path)
{
    (void)user_data;
    LOGI("playback finished: %s\n", file_path != NULL ? file_path : "(null)");
    if (s_finished_callback != NULL) {
        s_finished_callback(file_path, s_finished_user_data);
    }
}

void video_play_engine_api_set_finished_callback(video_play_finished_cb_t callback,
                                                 void *user_data)
{
    s_finished_callback = callback;
    s_finished_user_data = user_data;
}

static avdk_err_t video_play_audio_set_volume_cb(void *user_data, uint8_t volume)
{
    video_play_user_ctx_t *ctx = user_data;
    if (ctx == NULL) {
        return AVDK_ERR_INVAL;
    }

    ctx->audio_volume = volume;
    if (ctx->audio_player_handle == NULL) {
        return AVDK_ERR_OK;
    }
    return audio_player_device_set_volume(ctx->audio_player_handle, volume);
}

static avdk_err_t video_play_audio_set_mute_cb(void *user_data, bool mute)
{
    video_play_user_ctx_t *ctx = user_data;
    if (ctx == NULL) {
        return AVDK_ERR_INVAL;
    }

    ctx->audio_muted = mute;
    if (ctx->audio_player_handle == NULL) {
        return AVDK_ERR_OK;
    }
    return audio_player_device_set_mute(ctx->audio_player_handle, mute);
}

static avdk_err_t video_play_audio_output_config_cb(
    void *user_data,
    const video_player_audio_params_t *params)
{
    video_play_user_ctx_t *ctx = user_data;
    if (ctx == NULL || params == NULL ||
        params->channels == 0U || params->channels > 2U ||
        params->sample_rate == 0U) {
        return AVDK_ERR_INVAL;
    }

    uint32_t bits = params->bits_per_sample > 0U ? params->bits_per_sample : 16U;
    if (bits != 8U && bits != 16U && bits != 24U && bits != 32U) {
        return AVDK_ERR_INVAL;
    }

    if (ctx->audio_player_handle != NULL) {
        audio_player_device_handle_t old_handle = ctx->audio_player_handle;
        ctx->audio_player_handle = NULL;
        s_audio_player = NULL;
        avdk_err_t old_ret = audio_player_device_deinit(old_handle);
        if (old_ret != AVDK_ERR_OK) {
            ctx->audio_player_handle = old_handle;
            s_audio_player = old_handle;
            return old_ret;
        }
    }

    uint32_t bytes_per_sample = bits / 8U;
    uint32_t samples_20ms = (params->sample_rate * 20U + 999U) / 1000U;
    audio_player_device_cfg_t audio_cfg = {
        .channels = params->channels,
        .sample_rate = params->sample_rate,
        .bits_per_sample = bits,
        .format = params->format,
        .frame_size = samples_20ms * bytes_per_sample * params->channels,
    };

    audio_player_device_handle_t new_handle = NULL;
    avdk_err_t ret = audio_player_device_init(&audio_cfg, &new_handle);
    if (ret != AVDK_ERR_OK || new_handle == NULL) {
        return ret == AVDK_ERR_OK ? AVDK_ERR_HWERROR : ret;
    }

    ret = audio_player_device_start(new_handle);
    if (ret != AVDK_ERR_OK) {
        (void)audio_player_device_deinit(new_handle);
        return ret;
    }

    ret = audio_player_device_set_volume(new_handle, ctx->audio_volume);
    if (ret == AVDK_ERR_OK) {
        ret = audio_player_device_set_mute(new_handle, ctx->audio_muted);
    }
    if (ret != AVDK_ERR_OK) {
        (void)audio_player_device_deinit(new_handle);
        return ret;
    }

    ctx->audio_player_handle = new_handle;
    s_audio_player = new_handle;
    return AVDK_ERR_OK;
}

static avdk_err_t video_play_mount_sd(void)
{
    if (s_sd_mounted) {
        return AVDK_ERR_OK;
    }

    DIR *dir = opendir(VFS_SD_0_PATITION_0);
    if (dir != NULL) {
        closedir(dir);
        s_sd_mounted = true;
        return AVDK_ERR_OK;
    }

    struct bk_fatfs_partition partition;
    os_memset(&partition, 0, sizeof(partition));
    partition.part_type = FATFS_DEVICE;
    partition.part_dev.device_name = FATFS_DEV_SDCARD;
    partition.mount_path = VFS_SD_0_PATITION_0;

    int ret = mount("SOURCE_NONE", partition.mount_path, "fatfs", 0, &partition);
    if (ret != BK_OK) {
        LOGE("mount %s failed, ret=%d\n", partition.mount_path, ret);
        return AVDK_ERR_IO;
    }

    s_sd_mounted = true;
    return AVDK_ERR_OK;
}

avdk_err_t video_play_engine_api_prepare_storage(void)
{
    return video_play_mount_sd();
}

static avdk_err_t video_play_register_modules(void)
{
    video_player_container_parser_ops_t *avi = bk_video_player_get_avi_parser_ops();
    video_player_container_parser_ops_t *mp4 = bk_video_player_get_mp4_parser_ops();
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    s_flexa_decoder_ops = bk_video_player_get_hw_h264_decoder_ops();
    s_frame_decoder_ops = bk_video_player_get_hw_h264_decoder_frame_ops();
    video_player_video_decoder_ops_t *initial_h264 =
        s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW
            ? s_frame_decoder_ops
            : s_flexa_decoder_ops;
    video_player_video_decoder_ops_t *alternate_h264 =
        s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW
            ? s_flexa_decoder_ops
            : s_frame_decoder_ops;
#else
    s_frame_decoder_ops = bk_video_player_get_hw_h264_decoder_frame_ops();
    video_player_video_decoder_ops_t *initial_h264 = s_frame_decoder_ops;
#endif
    if (avi == NULL || mp4 == NULL || initial_h264 == NULL) {
        return AVDK_ERR_UNSUPPORTED;
    }

    avdk_err_t ret = bk_video_player_engine_register_container_parser(s_player, avi);
    if (ret == AVDK_ERR_OK) {
        ret = bk_video_player_engine_register_container_parser(s_player, mp4);
    }
    if (ret == AVDK_ERR_OK) {
        /*
         * Register the explicitly selected initial decoder first. The
         * controller records the successful template as its preference, so
         * later file opens do not depend on list order after a hot switch.
         */
        ret = bk_video_player_engine_register_video_decoder(s_player,
                                                             initial_h264);
    }
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (ret == AVDK_ERR_OK && alternate_h264 != NULL &&
        alternate_h264 != initial_h264) {
        ret = bk_video_player_engine_register_video_decoder(s_player,
                                                             alternate_h264);
    }
#endif

#if CONFIG_BK_VIDEO_PLAYER_ENABLE_AAC_AUDIO_DECODER
    if (ret == AVDK_ERR_OK) {
        const video_player_audio_decoder_ops_t *aac =
            bk_video_player_get_aac_decoder_ops();
        if (aac != NULL) {
            ret = bk_video_player_engine_register_audio_decoder(s_player, aac);
        }
    }
#endif

    return ret;
}

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
static avdk_err_t video_play_make_switch_profile(
    video_play_output_mode_t mode,
    bk_video_player_video_switch_profile_t *profile)
{
    if (profile == NULL) {
        return AVDK_ERR_INVAL;
    }

    os_memset(profile, 0, sizeof(*profile));
    if (mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW) {
        profile->decoder_ops = s_frame_decoder_ops;
        profile->output_format = PIXEL_FMT_RGB565;
        profile->rotate_degree = 0U;
        profile->display_width = KLOK_VIDEO_WIDTH;
        profile->display_height = KLOK_VIDEO_HEIGHT;
        profile->buffer_alloc_cb = video_play_video_frame_buffer_alloc_cb;
    } else if (mode == VIDEO_PLAY_OUTPUT_FLEXA_DIRECT) {
        profile->decoder_ops = s_flexa_decoder_ops;
        profile->output_format = PIXEL_FMT_NV12;
        profile->rotate_degree = 90U;
        profile->display_width = KLOK_VIDEO_HEIGHT;
        profile->display_height = KLOK_VIDEO_WIDTH;
        profile->buffer_alloc_cb =
            video_play_video_flexa_placeholder_alloc_cb;
    } else {
        return AVDK_ERR_INVAL;
    }
    profile->buffer_free_cb = video_play_video_frame_buffer_free_cb;
    return profile->decoder_ops != NULL ? AVDK_ERR_OK : AVDK_ERR_UNSUPPORTED;
}
#endif

static void video_play_destroy_runtime(void)
{
    /*
     * Stop accepting/displaying decoded frames before joining the decoder.
     * This drains either a Flexa direct frame or a PP-OSD frame with the
     * allocator and completion path that owns it before decoder teardown.
     */
    video_play_display_prepare_restart();

    if (s_player != NULL) {
        if (s_player_opened) {
            avdk_err_t ret = bk_video_player_engine_stop(s_player);
            if (ret != AVDK_ERR_OK) {
                LOGW("engine stop failed, ret=%d\n", ret);
            }
            ret = bk_video_player_engine_close(s_player);
            if (ret != AVDK_ERR_OK) {
                LOGW("engine close failed, ret=%d\n", ret);
            }
        }
        (void)bk_video_player_engine_delete(s_player);
        s_player = NULL;
        s_player_opened = false;
    }

    video_play_display_worker_deinit();

    if (s_audio_player != NULL) {
        (void)audio_player_device_deinit(s_audio_player);
        s_audio_player = NULL;
        s_play_ctx.audio_player_handle = NULL;
    }
}

static avdk_err_t video_play_open(void)
{
    if (s_player != NULL && s_player_opened) {
        return AVDK_ERR_OK;
    }

    avdk_err_t ret = video_play_mount_sd();
    if (ret != AVDK_ERR_OK) {
        return ret;
    }

    os_memset(&s_play_ctx, 0, sizeof(s_play_ctx));
    s_play_ctx.audio_volume = KLOK_VIDEO_DEFAULT_VOLUME;
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    s_play_ctx.frame_preview_mode =
        s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW;
#endif

    ret = video_play_display_worker_init();
    if (ret != AVDK_ERR_OK) {
        return ret;
    }

    bk_video_player_config_t cfg;
    os_memset(&cfg, 0, sizeof(cfg));
    cfg.video.parser_to_decode_buffer_count = 2U;
    cfg.video.decode_to_output_buffer_count = 2U;
    cfg.audio.parser_to_decode_buffer_count = 2U;
    cfg.audio.decode_to_output_buffer_count = 2U;

    cfg.video.packet_buffer_alloc_cb = video_play_video_packet_buffer_alloc_cb;
    cfg.video.packet_buffer_free_cb = video_play_video_packet_buffer_free_cb;
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    cfg.video.buffer_alloc_cb =
        s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW
            ? video_play_video_frame_buffer_alloc_cb
            : video_play_video_flexa_placeholder_alloc_cb;
#else
    cfg.video.buffer_alloc_cb = video_play_video_frame_buffer_alloc_cb;
#endif
    cfg.video.buffer_free_cb = video_play_video_frame_buffer_free_cb;
    cfg.video.decode_complete_cb = video_play_video_decode_complete_cb;
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    cfg.video.output_format =
        s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW
            ? PIXEL_FMT_RGB565
            : PIXEL_FMT_NV12;
#else
    cfg.video.output_format = PIXEL_FMT_RGB565;
#endif
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW) {
        cfg.video.rotate_degree = 0U;
        cfg.video.display_width = KLOK_VIDEO_WIDTH;
        cfg.video.display_height = KLOK_VIDEO_HEIGHT;
    } else {
        cfg.video.rotate_degree = 90U;
        cfg.video.display_width = KLOK_VIDEO_HEIGHT;
        cfg.video.display_height = KLOK_VIDEO_WIDTH;
    }
#else
    cfg.video.rotate_degree = 0U;
    cfg.video.display_width = KLOK_VIDEO_WIDTH;
    cfg.video.display_height = KLOK_VIDEO_HEIGHT;
#endif

    cfg.audio.buffer_alloc_cb = video_play_audio_buffer_alloc_cb;
    cfg.audio.buffer_free_cb = video_play_audio_buffer_free_cb;
    cfg.audio.decode_complete_cb = video_play_audio_decode_complete_cb;
    cfg.audio.audio_set_volume_cb = video_play_audio_set_volume_cb;
    cfg.audio.audio_set_mute_cb = video_play_audio_set_mute_cb;
    cfg.audio.audio_output_config_cb = video_play_audio_output_config_cb;
    cfg.user_data = &s_play_ctx;
    cfg.playback_finished_cb = video_play_finished_cb;
    cfg.playback_finished_user_data = NULL;

    ret = bk_video_player_engine_new(&s_player, &cfg);
    if (ret != AVDK_ERR_OK || s_player == NULL) {
        s_player = NULL;
        video_play_display_worker_deinit();
        return ret == AVDK_ERR_OK ? AVDK_ERR_GENERIC : ret;
    }

    /*
     * The player engine defaults to 100%. Set the product default before open()
     * so its initial audio-control callback cannot overwrite our low-volume
     * setting with full-scale DAC gain.
     */
    ret = bk_video_player_engine_set_volume(s_player, KLOK_VIDEO_DEFAULT_VOLUME);
    if (ret == AVDK_ERR_OK) {
        ret = video_play_register_modules();
    }
    if (ret == AVDK_ERR_OK) {
        ret = bk_video_player_engine_open(s_player);
    }
    if (ret != AVDK_ERR_OK) {
        video_play_destroy_runtime();
        return ret;
    }

    s_player_opened = true;
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (s_output_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW) {
        LOGI("Frame preview player opened: RGB565 %ux%u, decoder rotate=0\n",
             (unsigned)KLOK_VIDEO_WIDTH,
             (unsigned)KLOK_VIDEO_HEIGHT);
    } else {
        LOGI("Flexa direct player opened: compressed ARGB8888 %ux%u, decoder rotate=90\n",
             (unsigned)KLOK_VIDEO_HEIGHT,
             (unsigned)KLOK_VIDEO_WIDTH);
    }
#else
    LOGI("OSD player opened: H264 frame RGB565 %ux%u, decoder rotate=0\n",
         (unsigned)KLOK_VIDEO_WIDTH,
         (unsigned)KLOK_VIDEO_HEIGHT);
#endif
    return AVDK_ERR_OK;
}

static bool video_play_is_ready(void)
{
    return s_player != NULL && s_player_opened;
}

static avdk_err_t video_play_start_internal(const char *file_path)
{
    if (file_path == NULL || file_path[0] == '\0') {
        return AVDK_ERR_INVAL;
    }

    bool restarting = video_play_is_ready();
    avdk_err_t ret = video_play_open();
    if (ret != AVDK_ERR_OK) {
        return ret;
    }

    if (restarting) {
        video_play_display_prepare_restart();
    }
    ret = bk_video_player_engine_play_file(s_player, file_path);
    if (restarting) {
        video_play_display_resume_handoff();
    }
    if (ret != AVDK_ERR_OK) {
        LOGE("play_file failed, ret=%d, file=%s\n", ret, file_path);
        return ret;
    }

    (void)snprintf(s_current_file_path,
                   sizeof(s_current_file_path),
                   "%s",
                   file_path);
    LOGI("playing %s\n", file_path);
    return AVDK_ERR_OK;
}

avdk_err_t video_play_engine_api_start(const char *file_path)
{
    if (s_output_switching) {
        return AVDK_ERR_BUSY;
    }
    return video_play_start_internal(file_path);
}

avdk_err_t video_play_engine_api_stop(void)
{
    s_output_switching = false;
    s_video_switch_prepared = false;
    video_play_destroy_runtime();
    return AVDK_ERR_OK;
}

avdk_err_t video_play_engine_api_set_pause(bool pause)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    return bk_video_player_engine_set_pause(s_player, pause);
}

avdk_err_t video_play_engine_api_seek(uint64_t time_ms)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    video_play_display_prepare_restart();
    avdk_err_t ret = bk_video_player_engine_seek(s_player, time_ms);
    video_play_display_resume_handoff();
    return ret;
}

avdk_err_t video_play_engine_api_set_volume(uint8_t volume)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    return bk_video_player_engine_set_volume(s_player, volume);
}

avdk_err_t video_play_engine_api_volume_up(uint8_t step)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    return bk_video_player_engine_volume_up(s_player, step);
}

avdk_err_t video_play_engine_api_volume_down(uint8_t step)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    return bk_video_player_engine_volume_down(s_player, step);
}

avdk_err_t video_play_engine_api_set_mute(bool mute)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    return bk_video_player_engine_set_mute(s_player, mute);
}

avdk_err_t video_play_engine_api_reassert_audio_format(void)
{
    if (!video_play_is_ready() || s_play_ctx.audio_player_handle == NULL) {
        return AVDK_ERR_GENERIC;
    }
    return audio_player_device_reassert_format(s_play_ctx.audio_player_handle);
}

avdk_err_t video_play_engine_api_select_audio_track(uint8_t index)
{
    if (!video_play_is_ready()) {
        return AVDK_ERR_GENERIC;
    }
    video_play_display_prepare_restart();
    avdk_err_t ret = bk_video_player_engine_select_audio_track(s_player, index);
    video_play_display_resume_handoff();
    return ret;
}

video_play_output_mode_t video_play_engine_api_get_output_mode(void)
{
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    return s_output_mode;
#else
    return VIDEO_PLAY_OUTPUT_FRAME_PREVIEW;
#endif
}

bool video_play_engine_api_is_switching(void)
{
    return s_output_switching;
}

avdk_err_t video_play_engine_api_begin_output_switch(video_play_output_mode_t target_mode)
{
#if !KLOK_VIDEO_FLEXA_DIRECT_MODE
    (void)target_mode;
    return AVDK_ERR_UNSUPPORTED;
#else
    if (target_mode != VIDEO_PLAY_OUTPUT_FLEXA_DIRECT &&
        target_mode != VIDEO_PLAY_OUTPUT_FRAME_PREVIEW) {
        return AVDK_ERR_INVAL;
    }
    if (s_output_switching) {
        return AVDK_ERR_BUSY;
    }
    if (target_mode == s_output_mode) {
        return AVDK_ERR_RDYDONE;
    }

    s_output_switching = true;
    s_switch_source_mode = s_output_mode;
    s_switch_target_mode = target_mode;
    s_video_switch_prepared = false;

    if (video_play_is_ready()) {
        /*
         * Reject and drain display handoff first. The core call then waits for
         * the video parser and the in-flight decoder call to acknowledge
         * quiescence before it destroys only active_video_decoder.
         */
        video_play_display_prepare_restart();
        avdk_err_t ret =
            bk_video_player_engine_prepare_video_decoder_switch(s_player);
        if (ret != AVDK_ERR_OK) {
            video_play_display_resume_handoff();
            s_output_switching = false;
            return ret;
        }
        s_video_switch_prepared = true;
    }

    LOGI("video-only output switch prepared: %d -> %d\n",
         (int)s_output_mode,
         (int)target_mode);
    return AVDK_ERR_OK;
#endif
}

avdk_err_t video_play_engine_api_complete_output_switch(const char *file_path,
                                                        bool remain_paused)
{
#if !KLOK_VIDEO_FLEXA_DIRECT_MODE
    (void)file_path;
    (void)remain_paused;
    return AVDK_ERR_UNSUPPORTED;
#else
    if (!s_output_switching) {
        return AVDK_ERR_INVAL;
    }

    avdk_err_t ret = AVDK_ERR_OK;
    bk_video_player_video_switch_profile_t profile;
    /*
     * When no player runtime exists, begin_output_switch only records the
     * desired mode. Decoder ops are populated later by video_play_open(), so
     * building a hot-switch profile here would incorrectly report
     * AVDK_ERR_UNSUPPORTED because s_frame_decoder_ops is still NULL.
     */
    if (s_video_switch_prepared) {
        ret = video_play_make_switch_profile(s_switch_target_mode, &profile);
    }
    if (ret == AVDK_ERR_OK && s_video_switch_prepared) {
        s_play_ctx.frame_preview_mode =
            s_switch_target_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW;
        ret = bk_video_player_engine_complete_video_decoder_switch(s_player,
                                                                    &profile);
        if (ret == AVDK_ERR_OK) {
            s_video_switch_prepared = false;
        }
    }

    if (ret == AVDK_ERR_OK) {
        s_output_mode = s_switch_target_mode;
        const bool changing_file =
            file_path != NULL && file_path[0] != '\0';
        if (changing_file) {
            /*
             * A new file intentionally uses the normal full play_file path.
             * The just-selected decoder template remains the controller's
             * explicit preference for decoder selection.
             */
            ret = video_play_start_internal(file_path);
        }
        if (ret == AVDK_ERR_OK && remain_paused && changing_file) {
            ret = bk_video_player_engine_set_pause(s_player, true);
        }
        if (ret == AVDK_ERR_OK) {
            video_play_display_resume_handoff();
        }
    }

    if (ret != AVDK_ERR_OK) {
        LOGE("video-only output switch failed, target=%d ret=%d\n",
             (int)s_switch_target_mode,
             ret);
        /*
         * A failed target create/init leaves the core intentionally prepared
         * so the source decoder can be rebuilt without touching parser/audio.
         * Never clear the product flag while the core is still quiesced.
         */
        if (s_video_switch_prepared) {
            bk_video_player_video_switch_profile_t rollback;
            avdk_err_t rollback_ret =
                video_play_make_switch_profile(s_switch_source_mode, &rollback);
            if (rollback_ret == AVDK_ERR_OK) {
                s_play_ctx.frame_preview_mode =
                    s_switch_source_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW;
                rollback_ret =
                    bk_video_player_engine_complete_video_decoder_switch(
                        s_player, &rollback);
            }
            if (rollback_ret == AVDK_ERR_OK) {
                s_video_switch_prepared = false;
                s_output_mode = s_switch_source_mode;
                video_play_display_resume_handoff();
                LOGI("video-only output switch rolled back to %d\n",
                     (int)s_switch_source_mode);
            } else {
                LOGE("video-only output rollback failed, source=%d ret=%d\n",
                     (int)s_switch_source_mode, rollback_ret);
            }
        }
    }

    s_output_switching = s_video_switch_prepared;
    return ret;
#endif
}

void video_play_engine_api_cancel_output_switch(void)
{
    if (!s_output_switching) {
        return;
    }

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (s_video_switch_prepared) {
        bk_video_player_video_switch_profile_t profile;
        if (video_play_make_switch_profile(s_switch_source_mode, &profile) ==
            AVDK_ERR_OK) {
            s_play_ctx.frame_preview_mode =
                s_switch_source_mode == VIDEO_PLAY_OUTPUT_FRAME_PREVIEW;
            if (bk_video_player_engine_complete_video_decoder_switch(
                    s_player, &profile) == AVDK_ERR_OK) {
                s_video_switch_prepared = false;
                video_play_display_resume_handoff();
            }
        }
    }
#endif
    if (s_video_switch_prepared) {
        LOGE("video-only output cancel rollback remains prepared\n");
        return;
    }
    s_output_mode = s_switch_source_mode;
    s_output_switching = false;
}
