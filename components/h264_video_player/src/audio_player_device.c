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
#include <driver/aud_dac_types.h>
#include <os/mem.h>
#include <os/str.h>
#include "spk_service.h"

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
    audio_player_device_cfg_t cfg;
    float current_dig_gain_db;
    bool is_started;
    bool is_muted;
    bool is_attached;
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

    if (spk_service_init() != BK_OK)
    {
        LOGE("%s: failed to initialize shared speaker service\n", __func__);
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }

    spk_source_cfg_t source_cfg = {
        .src = SPK_SERVICE_SRC_A2DP,
        .nChans = cfg->channels,
        .sampRate = cfg->sample_rate,
        .bitsPerSample = cfg->bits_per_sample,
        .frame_size = cfg->frame_size,
        .volume = ctx->current_dig_gain_db,
    };
    if (spk_service_attach(&source_cfg) != BK_OK)
    {
        LOGE("%s: failed to attach KTV source\n", __func__);
        (void)spk_service_deinit();
        os_free(ctx);
        return AVDK_ERR_HWERROR;
    }
    ctx->is_attached = true;

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

    if (ctx->is_attached)
    {
        (void)spk_service_detach(SPK_SERVICE_SRC_A2DP);
        ctx->is_attached = false;
    }
    (void)spk_service_deinit();

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
    return (ctx->is_attached && spk_service_is_running())
        ? AVDK_ERR_OK : AVDK_ERR_HWERROR;
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

    int written = spk_service_write_ex(SPK_SERVICE_SRC_A2DP,
                                       data,
                                       len,
                                       SPK_SERVICE_WAIT_FOREVER);
    if (written < 0)
    {
        LOGW("%s: shared speaker write failed, ret=%d\n", __func__, written);
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
    bk_err_t ret = spk_service_set_volume(gain_db);
    if (ret != BK_OK)
    {
        LOGE("%s: shared speaker volume failed, ret=%d\n", __func__, ret);
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
    bk_err_t ret = spk_service_set_mute(mute ? 1U : 0U);
    if (ret == BK_OK && !mute)
    {
        ret = spk_service_set_volume(gain_db);
    }
    if (ret != BK_OK)
    {
        LOGE("%s: shared speaker mute failed, ret=%d\n", __func__, ret);
        return AVDK_ERR_HWERROR;
    }

    ctx->is_muted = mute;
    LOGD("%s: mute=%u, dig_gain_db=%.2f\n", __func__, mute, gain_db);

    return AVDK_ERR_OK;
}
