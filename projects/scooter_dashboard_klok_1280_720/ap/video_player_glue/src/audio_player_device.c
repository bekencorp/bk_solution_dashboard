// Copyright 2020-2021 Beken
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

#include "audio_player_device.h"

#include <common/bk_include.h>
#include <components/avdk_utils/avdk_error.h>
#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_streams/raw_stream.h>
#include <components/bk_audio/audio_streams/onboard_speaker_stream_v2.h>
#include <driver/aud_dac_types.h>
#include <os/mem.h>
#include <os/str.h>

#define TAG "audio_player_device"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

// Default UI volume; keep in sync with VIDEO_PLAY_DEFAULT_VOLUME in video_play_engine_cli.c
#define AUDIO_PLAYER_DEFAULT_VOLUME     (8)

// Board PA control. Product projects can override these values from CMake.
#ifndef AUDIO_PLAYER_PA_CTRL_ENABLE
#define AUDIO_PLAYER_PA_CTRL_ENABLE     (1)
#endif
#ifndef AUDIO_PLAYER_PA_CTRL_GPIO
#define AUDIO_PLAYER_PA_CTRL_GPIO       (29)
#endif
#ifndef AUDIO_PLAYER_PA_ON_LEVEL
#define AUDIO_PLAYER_PA_ON_LEVEL        (1)
#endif
#ifndef AUDIO_PLAYER_PA_ON_DELAY_MS
#define AUDIO_PLAYER_PA_ON_DELAY_MS     (2)
#endif
#ifndef AUDIO_PLAYER_PA_OFF_DELAY_MS
#define AUDIO_PLAYER_PA_OFF_DELAY_MS    (0)
#endif

// Audio player device context
typedef struct
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t raw_stream;
    audio_element_handle_t onboard_speaker_stream;
    audio_player_device_cfg_t cfg;
    float current_dig_gain_db;
    bool is_started;
    bool is_muted;
} audio_player_device_ctx_t;

/**
 * @brief Convert volume (0-100) to DAC digital gain in dB.
 *
 * onboard_speaker_stream v2 expects dB, not the legacy 0x00~0x3F register value.
 * volume 0 maps to BK_AUD_DAC_DIG_GAIN_DB_SILENCE so the driver hard-mutes the DAC.
 */
static float volume_to_dig_gain_db(uint8_t volume)
{
    if (volume == 0)
    {
        return BK_AUD_DAC_DIG_GAIN_DB_SILENCE;
    }

    if (volume > 100)
    {
        volume = 100;
    }

    const float min_db = -45.0f;
    const float max_db = BK_AUD_DAC_DIG_GAIN_DB_MAX;
    float db = min_db + ((float)(volume - 1) * (max_db - min_db) + 49.5f) / 99.0f;

    if (db > max_db)
    {
        db = max_db;
    }

    return db;
}

avdk_err_t audio_player_device_init(const audio_player_device_cfg_t *cfg, audio_player_device_handle_t *handle)
{
    if (cfg == NULL || handle == NULL)
    {
        LOGE("%s: invalid parameter\n", __func__);
        return AVDK_ERR_INVAL;
    }

    // Validate configuration parameters
    if (cfg->channels == 0 || cfg->channels > 2)
    {
        LOGE("%s: invalid channels: %u (must be 1 or 2)\n", __func__, cfg->channels);
        return AVDK_ERR_INVAL;
    }

    if (cfg->sample_rate == 0)
    {
        LOGE("%s: invalid sample_rate: %u\n", __func__, cfg->sample_rate);
        return AVDK_ERR_INVAL;
    }

    if (cfg->bits_per_sample == 0)
    {
        LOGE("%s: invalid bits_per_sample: %u\n", __func__, cfg->bits_per_sample);
        return AVDK_ERR_INVAL;
    }

    if (cfg->frame_size == 0)
    {
        LOGE("%s: invalid frame_size: %u\n", __func__, cfg->frame_size);
        return AVDK_ERR_INVAL;
    }

    // Allocate device context
    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)os_malloc(sizeof(audio_player_device_ctx_t));
    if (ctx == NULL)
    {
        LOGE("%s: failed to allocate context\n", __func__);
        return AVDK_ERR_NOMEM;
    }
    os_memset(ctx, 0, sizeof(audio_player_device_ctx_t));

    // Copy configuration
    os_memcpy(&ctx->cfg, cfg, sizeof(audio_player_device_cfg_t));
    ctx->current_dig_gain_db = volume_to_dig_gain_db(AUDIO_PLAYER_DEFAULT_VOLUME);
    ctx->is_started = false;
    ctx->is_muted = false;

    // Initialize audio pipeline
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    ctx->pipeline = audio_pipeline_init(&pipeline_cfg);
    if (ctx->pipeline == NULL)
    {
        LOGE("%s: failed to init audio pipeline\n", __func__);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    // Initialize raw stream (writer)
    raw_stream_cfg_t raw_stream_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_stream_cfg.type = AUDIO_STREAM_WRITER;
    raw_stream_cfg.output_port_type = PORT_TYPE_RB;
    // Calculate block size and number based on frame_size
    raw_stream_cfg.out_block_size = cfg->frame_size;
    raw_stream_cfg.out_block_num = 2; // Use 2 blocks for buffering
    ctx->raw_stream = raw_stream_init(&raw_stream_cfg);
    if (ctx->raw_stream == NULL)
    {
        LOGE("%s: failed to init raw stream\n", __func__);
        audio_pipeline_deinit(ctx->pipeline);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    // Initialize onboard speaker stream
    onboard_speaker_stream_cfg_t speaker_cfg = ONBOARD_SPEAKER_STREAM_CFG_DEFAULT();
    speaker_cfg.chl_num = cfg->channels;

    aud_dac_source_t main_src = (cfg->sample_rate <= 16000)
        ? AUD_DAC_SOURCE_CALL
        : AUD_DAC_SOURCE_A2DP;

    for (int i = 0; i < AUD_DAC_SOURCE_MAX; i++) {
        speaker_cfg.sample_rate[i] = cfg->sample_rate;
        speaker_cfg.frame_size[i]  = cfg->frame_size;
    }
    speaker_cfg.dac_source_bitmap = (1u << main_src);
    speaker_cfg.main_dac_source   = main_src;

    speaker_cfg.bits = cfg->bits_per_sample;
    speaker_cfg.dig_gain = ctx->current_dig_gain_db;
#if AUDIO_PLAYER_PA_CTRL_ENABLE
    speaker_cfg.pa_ctrl_en   = true;
    speaker_cfg.pa_ctrl_gpio = AUDIO_PLAYER_PA_CTRL_GPIO;
    speaker_cfg.pa_on_level  = AUDIO_PLAYER_PA_ON_LEVEL;
    speaker_cfg.pa_on_delay  = AUDIO_PLAYER_PA_ON_DELAY_MS;
    speaker_cfg.pa_off_delay = AUDIO_PLAYER_PA_OFF_DELAY_MS;
#endif
    speaker_cfg.multi_in_port_num = 0;
    speaker_cfg.multi_out_port_num = 0;
    ctx->onboard_speaker_stream = onboard_speaker_stream_init(&speaker_cfg);
    if (ctx->onboard_speaker_stream == NULL)
    {
        LOGE("%s: failed to init onboard speaker stream\n", __func__);
        // Clean up raw_stream element
        audio_element_deinit(ctx->raw_stream);
        audio_pipeline_deinit(ctx->pipeline);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    // Register elements to pipeline
    bk_err_t ret = audio_pipeline_register(ctx->pipeline, ctx->raw_stream, "raw_stream");
    if (ret != BK_OK)
    {
        LOGE("%s: failed to register raw_stream, ret=%d\n", __func__, ret);
        // Clean up elements
        audio_element_deinit(ctx->onboard_speaker_stream);
        audio_element_deinit(ctx->raw_stream);
        audio_pipeline_deinit(ctx->pipeline);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    ret = audio_pipeline_register(ctx->pipeline, ctx->onboard_speaker_stream, "onboard_speaker");
    if (ret != BK_OK)
    {
        LOGE("%s: failed to register onboard_speaker, ret=%d\n", __func__, ret);
        // Unregister raw_stream and clean up
        audio_pipeline_unregister(ctx->pipeline, ctx->raw_stream);
        audio_element_deinit(ctx->onboard_speaker_stream);
        audio_element_deinit(ctx->raw_stream);
        audio_pipeline_deinit(ctx->pipeline);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    // Link elements: raw_stream -> onboard_speaker
    const char *link_tag[] = {"raw_stream", "onboard_speaker"};
    ret = audio_pipeline_link(ctx->pipeline, link_tag, 2);
    if (ret != BK_OK)
    {
        LOGE("%s: failed to link pipeline, ret=%d\n", __func__, ret);
        // Unregister elements and clean up
        audio_pipeline_unregister(ctx->pipeline, ctx->onboard_speaker_stream);
        audio_pipeline_unregister(ctx->pipeline, ctx->raw_stream);
        audio_element_deinit(ctx->onboard_speaker_stream);
        audio_element_deinit(ctx->raw_stream);
        audio_pipeline_deinit(ctx->pipeline);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    *handle = (audio_player_device_handle_t)ctx;
    LOGI("%s: initialized successfully, ch=%u, rate=%u, bits=%u, frame_size=%u\n",
         __func__, cfg->channels, cfg->sample_rate, cfg->bits_per_sample, cfg->frame_size);

    return AVDK_ERR_OK;
}

avdk_err_t audio_player_device_deinit(audio_player_device_handle_t handle)
{
    if (handle == NULL)
    {
        LOGE("%s: invalid handle\n", __func__);
        return AVDK_ERR_INVAL;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;

    // Stop if started
    if (ctx->is_started)
    {
        audio_player_device_stop(handle);
    }

    // Clean up pipeline and elements
    if (ctx->pipeline != NULL)
    {
        // Unlink pipeline first
        audio_pipeline_unlink(ctx->pipeline);

        // Unregister elements
        if (ctx->onboard_speaker_stream != NULL)
        {
            audio_pipeline_unregister(ctx->pipeline, ctx->onboard_speaker_stream);
        }
        if (ctx->raw_stream != NULL)
        {
            audio_pipeline_unregister(ctx->pipeline, ctx->raw_stream);
        }

        // Deinitialize pipeline
        audio_pipeline_deinit(ctx->pipeline);
        ctx->pipeline = NULL;
    }

    // Deinitialize elements
    if (ctx->onboard_speaker_stream != NULL)
    {
        audio_element_deinit(ctx->onboard_speaker_stream);
        ctx->onboard_speaker_stream = NULL;
    }
    if (ctx->raw_stream != NULL)
    {
        audio_element_deinit(ctx->raw_stream);
        ctx->raw_stream = NULL;
    }

    os_free(ctx);
    LOGI("%s: deinitialized\n", __func__);

    return AVDK_ERR_OK;
}

avdk_err_t audio_player_device_start(audio_player_device_handle_t handle)
{
    if (handle == NULL)
    {
        LOGE("%s: invalid handle\n", __func__);
        return AVDK_ERR_INVAL;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;

    if (ctx->is_started)
    {
        LOGW("%s: already started\n", __func__);
        return AVDK_ERR_OK;
    }

    // Start pipeline
    bk_err_t ret = audio_pipeline_run(ctx->pipeline);
    if (ret != BK_OK)
    {
        LOGE("%s: failed to run pipeline, ret=%d\n", __func__, ret);
        return AVDK_ERR_HWERROR;
    }

    ctx->is_started = true;
    LOGI("%s: started\n", __func__);

    return AVDK_ERR_OK;
}

avdk_err_t audio_player_device_stop(audio_player_device_handle_t handle)
{
    if (handle == NULL)
    {
        LOGE("%s: invalid handle\n", __func__);
        return AVDK_ERR_INVAL;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;

    if (!ctx->is_started)
    {
        LOGW("%s: not started\n", __func__);
        return AVDK_ERR_OK;
    }

    // Stop pipeline
    bk_err_t ret = audio_pipeline_stop(ctx->pipeline);
    if (ret != BK_OK)
    {
        LOGE("%s: failed to stop pipeline, ret=%d\n", __func__, ret);
        return AVDK_ERR_HWERROR;
    }

    ret = audio_pipeline_wait_for_stop(ctx->pipeline);
    if (ret != BK_OK)
    {
        LOGE("%s: failed to wait for pipeline stop, ret=%d\n", __func__, ret);
        return AVDK_ERR_HWERROR;
    }

    ctx->is_started = false;
    LOGI("%s: stopped\n", __func__);

    return AVDK_ERR_OK;
}

avdk_err_t audio_player_device_reassert_format(audio_player_device_handle_t handle)
{
    if (handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;
    aud_dac_source_t source = (ctx->cfg.sample_rate <= 16000U)
        ? AUD_DAC_SOURCE_CALL
        : AUD_DAC_SOURCE_A2DP;

    bk_err_t ret = onboard_speaker_stream_set_param(
        ctx->onboard_speaker_stream,
        (int)ctx->cfg.sample_rate,
        (int)ctx->cfg.bits_per_sample,
        (int)ctx->cfg.channels,
        source);
    return ret == BK_OK ? AVDK_ERR_OK : AVDK_ERR_HWERROR;
}

int32_t audio_player_device_write_frame_data(audio_player_device_handle_t handle, const char *data, uint32_t len)
{
    if (handle == NULL || data == NULL || len == 0)
    {
        LOGE("%s: invalid parameter\n", __func__);
        return -1;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;

    if (!ctx->is_started)
    {
        LOGW("%s: device not started\n", __func__);
        return -1;
    }

    // Write data to raw stream
    int written = raw_stream_write(ctx->raw_stream, (char *)data, (int)len);
    if (written < 0)
    {
        LOGW("%s: raw_stream_write failed, ret=%d\n", __func__, written);
        return written;
    }

    return written;
}

avdk_err_t audio_player_device_set_volume(audio_player_device_handle_t handle, uint8_t volume)
{
    if (handle == NULL)
    {
        LOGE("%s: invalid handle\n", __func__);
        return AVDK_ERR_INVAL;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;

    float gain_db = volume_to_dig_gain_db(volume);
    bk_err_t ret = onboard_speaker_stream_set_digital_gain(ctx->onboard_speaker_stream, gain_db);
    if (ret != BK_OK)
    {
        LOGE("%s: onboard_speaker_stream_set_digital_gain failed, ret=%d\n", __func__, ret);
        return AVDK_ERR_HWERROR;
    }

    ctx->current_dig_gain_db = gain_db;
    ctx->is_muted = false;
    LOGD("%s: volume=%u -> dig_gain_db=%.2f\n", __func__, volume, gain_db);

    return AVDK_ERR_OK;
}

avdk_err_t audio_player_device_set_mute(audio_player_device_handle_t handle, bool mute)
{
    if (handle == NULL)
    {
        LOGE("%s: invalid handle\n", __func__);
        return AVDK_ERR_INVAL;
    }

    audio_player_device_ctx_t *ctx = (audio_player_device_ctx_t *)handle;

    float gain_db = mute ? BK_AUD_DAC_DIG_GAIN_DB_SILENCE : ctx->current_dig_gain_db;
    bk_err_t ret = onboard_speaker_stream_set_digital_gain(ctx->onboard_speaker_stream, gain_db);
    if (ret != BK_OK)
    {
        LOGE("%s: onboard_speaker_stream_set_digital_gain failed, ret=%d\n", __func__, ret);
        return AVDK_ERR_HWERROR;
    }

    ctx->is_muted = mute;
    LOGD("%s: mute=%u, dig_gain_db=%.2f\n", __func__, mute, gain_db);

    return AVDK_ERR_OK;
}
