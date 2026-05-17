#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <common/bk_err.h>
#include <getopt.h>

#if CONFIG_LCD
#include <driver/lcd.h>
#endif

#include "media_devices.h"

#include "media_network_transfer.h"
#include "network_transfer.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include "components/bk_display.h"
#if CONFIG_LCD
#include "lcd_panel_devices.h"
#endif
#include "driver/gpio.h"
#include "gpio_driver.h"

#include <components/bk_frame_buffer.h>
#include "media_frame_psram_alloc.h"

#include "cast_jpeg_pipeline.h"
#include "media_msg.h"

#define TAG "db-device"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef struct
{
    uint32_t decode_frame_count;
    uint32_t last_decode_frame_count;
    uint32_t last_wifi_transfer_frame_count;
    uint32_t last_wifi_transfer_frame_size;
    uint32_t last_wifi_transfer_rate;
} db_device_debug_info_t;

typedef struct
{
    bk_display_ctlr_handle_t lcd_display_handle;
    uint32_t lcd_use_module;
    db_device_debug_info_t debug_info;
    beken_timer_t debug_timer;
} db_device_info_t;

#if CONFIG_LCD
static const lcd_device_t *lcd_device = &lcd_device_st7282;
#endif
db_device_info_t *db_device_info = NULL;

// static aud_intf_drv_setup_t aud_intf_drv_setup = DEFAULT_AUD_INTF_DRV_SETUP_CONFIG();
// static aud_intf_work_mode_t aud_work_mode = AUD_INTF_WORK_MODE_NULL;
// static aud_intf_voc_setup_t aud_voc_setup = DEFAULT_AUD_INTF_VOC_SETUP_CONFIG();

void set_lcd_use_module(lcd_source_module_t module)
{
    if (db_device_info)
    {
        db_device_info->lcd_use_module = module;
    }
}

bk_err_t av_server_jpeg_decode_manager_suspend(void)
{
    return cast_jpeg_pipeline_suspend();
}

bk_err_t av_server_jpeg_decode_manager_resume(void)
{
    return cast_jpeg_pipeline_resume();
}

bk_err_t av_server_jpeg_decode_manager_turn_on(void)
{
    if (db_device_info == NULL) {
        LOGE("db_device_info is NULL\n");
        return BK_FAIL;
    }

    db_device_info->debug_info.decode_frame_count = 0;
    db_device_info->debug_info.last_decode_frame_count = 0;

    set_lcd_use_module(LCD_SOURCE_MODULE_DECODER);

    LOGI("cast jpeg_stream_pipeline on (src=%ux%u dst=%ux%u)\n",
         (unsigned)CAST_JPEG_SRC_WIDTH, (unsigned)CAST_JPEG_SRC_HEIGHT,
         (unsigned)CAST_JPEG_DST_WIDTH, (unsigned)CAST_JPEG_DST_HEIGHT);

    return cast_jpeg_pipeline_turn_on(db_device_info->lcd_display_handle);
}

bk_err_t av_server_jpeg_decode_manager_turn_off(void)
{
    bk_err_t ret;

    set_lcd_use_module(LCD_SOURCE_MODULE_LVGL);
    ret = cast_jpeg_pipeline_turn_off();
    LOGI("cast jpeg_stream_pipeline off ret=%d\n", (int)ret);
    return ret;
}

bk_err_t av_server_jpeg_decode_manager_get_decoder_type(void)
{
    return BK_OK;
}

#if CONFIG_LCD
static avdk_err_t lcd_backlight_open(uint8_t bl_io)
{
    gpio_dev_unmap(bl_io);
    BK_LOG_ON_ERR(bk_gpio_enable_output(bl_io));
    BK_LOG_ON_ERR(bk_gpio_pull_up(bl_io));
    bk_gpio_set_output_high(bl_io);
    return AVDK_ERR_OK;
}

static avdk_err_t lcd_backlight_close(uint8_t bl_io)
{
    BK_LOG_ON_ERR(bk_gpio_pull_down(bl_io));
    bk_gpio_set_output_low(bl_io);
    return AVDK_ERR_OK;
}
    
bk_err_t av_server_display_turn_on(void *param, uint16_t rotate)
{
    bk_err_t ret = BK_FAIL;
    bk_display_rgb_ctlr_config_t lcd_display_config = {0};

    lcd_display_config.lcd_device = lcd_device;
    lcd_display_config.clk_pin = GPIO_0;
    lcd_display_config.cs_pin = GPIO_12;
    lcd_display_config.sda_pin = GPIO_1;
    lcd_display_config.rst_pin = GPIO_6;
    ret = bk_display_rgb_new(&db_device_info->lcd_display_handle, &lcd_display_config);

    if (ret != BK_OK)
    {
        LOGE("%s, bk_display_rgb_new failed\n", __func__);
        return ret;
    }

    lcd_backlight_open(GPIO_7);
    ret = bk_display_open(db_device_info->lcd_display_handle);
    if (ret != BK_OK)
    {
        LOGE("%s, bk_display_open failed\n", __func__);
        return ret;
    }
    return BK_OK;
}

bk_err_t av_server_display_turn_off(void)
{
    return BK_OK;
}
#endif

bk_err_t av_server_udp_voice_send_callback(unsigned char *data, unsigned int len)
{
    LOGD("%s, data: %p, len: %u\n", __func__, data, len);

    return ntwk_trans_audio_send(data, len, AUDIO_ENC_TYPE_G711A);
}

static void av_server_audio_connect_state_cb_handle(uint8_t state)
{
    os_printf("[--%s--] state: %d \n", __func__, state);
}

#if AUDIO_TRANSFER_ENABLE
bk_err_t av_server_audio_turn_on(audio_parameters_t *parameters)
{
    bk_err_t ret;

    if (db_device_info->audio_enable == BK_TRUE)
    {
        LOGI("%s already turn on\n", __func__);

        return BK_FAIL;
    }

    LOGI("%s, AEC: %d, UAC: %d, sample rate: %d, %d, fmt: %d, %d\n", __func__,
        parameters->aec, parameters->uac, parameters->rmt_recorder_sample_rate,
        parameters->rmt_player_sample_rate, parameters->rmt_recoder_fmt, parameters->rmt_player_fmt);

    if (parameters->aec == 1)
    {
        aud_voc_setup.aec_enable = true;
    }
    else
    {
        aud_voc_setup.aec_enable = false;
    }

    //aud_voc_setup.data_type = AUD_INTF_VOC_DATA_TYPE_G711A;
    //aud_voc_setup.data_type = AUD_INTF_VOC_DATA_TYPE_PCM;
    aud_voc_setup.spk_mode = AUD_DAC_WORK_MODE_SIGNAL_END;
    //aud_voc_setup.mic_en = AUD_INTF_VOC_MIC_OPEN;
    //aud_voc_setup.spk_en = AUD_INTF_VOC_SPK_OPEN;

    if (parameters->uac == 1)
    {
        aud_voc_setup.mic_type = AUD_INTF_MIC_TYPE_UAC;
        aud_voc_setup.spk_type = AUD_INTF_SPK_TYPE_UAC;
        //aud_voc_setup.samp_rate = AUD_INTF_VOC_SAMP_RATE_16K;
    }
    else
    {
        aud_voc_setup.mic_type = AUD_INTF_MIC_TYPE_BOARD;
        aud_voc_setup.spk_type = AUD_INTF_SPK_TYPE_BOARD;
    }

    if (aud_voc_setup.mic_type == AUD_INTF_MIC_TYPE_BOARD && aud_voc_setup.spk_type == AUD_INTF_SPK_TYPE_BOARD) {
            aud_voc_setup.data_type = parameters->rmt_recoder_fmt - 1;
    }

    switch (parameters->rmt_recorder_sample_rate)
    {
        case DB_SAMPLE_RARE_8K:
            aud_voc_setup.samp_rate = 8000;
            break;

        case DB_SAMPLE_RARE_16K:
            aud_voc_setup.samp_rate = 16000;
            break;

        default:
            aud_voc_setup.samp_rate = 8000;
            break;
    }

    aud_intf_drv_setup.aud_intf_tx_mic_data = av_server_udp_voice_send_callback;
    ret = bk_aud_intf_drv_init(&aud_intf_drv_setup);
    if (ret != BK_ERR_AUD_INTF_OK)
    {
        LOGE("bk_aud_intf_drv_init fail, ret:%d\n", ret);
        goto error;
    }

    aud_work_mode = AUD_INTF_WORK_MODE_VOICE;
    ret = bk_aud_intf_set_mode(aud_work_mode);
    if (ret != BK_ERR_AUD_INTF_OK)
    {
        LOGE("bk_aud_intf_set_mode fail, ret:%d\n", ret);
        goto error;
    }

    /* uac recover connection */
    if (aud_voc_setup.mic_type == AUD_INTF_MIC_TYPE_UAC)
    {
        ret = bk_aud_intf_register_uac_connect_state_cb(av_server_audio_connect_state_cb_handle);
        if (ret != BK_ERR_AUD_INTF_OK)
        {
            LOGE("bk_aud_intf_register_uac_connect_state_cb fail, ret:%d\n", ret);
            goto error;
        }

        ret = bk_aud_intf_uac_auto_connect_ctrl(true);
        if (ret != BK_ERR_AUD_INTF_OK)
        {
            LOGE("aud_tras_uac_auto_connect_ctrl fail, ret:%d\n", ret);
            goto error;
        }
    }

    ret = bk_aud_intf_voc_init(aud_voc_setup);
    if (ret != BK_ERR_AUD_INTF_OK)
    {
        LOGE("bk_aud_intf_voc_init fail, ret:%d\n", ret);
        goto error;
    }

    ret = bk_aud_intf_voc_start();
    if (ret != BK_ERR_AUD_INTF_OK)
    {
        LOGE("bk_aud_intf_voc_start fail, ret:%d\n", ret);
        goto error;
    }

    db_device_info->audio_enable = BK_TRUE;

    return BK_OK;
error:
    aud_work_mode = AUD_INTF_WORK_MODE_NULL;
    bk_aud_intf_set_mode(aud_work_mode);
    bk_aud_intf_drv_deinit();

    return BK_FAIL;
}

bk_err_t av_server_audio_turn_off(void)
{
    if (db_device_info->audio_enable == BK_FALSE)
    {
        LOGI("%s already turn off\n", __func__);

        return BK_FAIL;
    }

    LOGI("%s entry\n", __func__);

    db_device_info->audio_enable = BK_FALSE;

    bk_aud_intf_voc_stop();
    bk_aud_intf_voc_deinit();
    aud_work_mode = AUD_INTF_WORK_MODE_NULL;
    bk_aud_intf_set_mode(aud_work_mode);
    bk_aud_intf_drv_deinit();

    LOGI("%s out\n", __func__);

    return BK_OK;
}

bk_err_t av_server_audio_acoustics(uint32_t index, uint32_t param)
{
    LOGI("%s, %u, %u\n", __func__, index, param);
    bk_err_t ret = BK_FAIL;

    switch (index)
    {
        case AA_ECHO_DEPTH:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_EC_DEPTH, param);
            break;
        case AA_MAX_AMPLITUDE:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_TXRX_THR, param);
            break;
        case AA_MIN_AMPLITUDE:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_TXRX_FLR, param);
            break;
        case AA_NOISE_LEVEL:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_NS_LEVEL, param);
            break;
        case AA_NOISE_PARAM:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_NS_PARA, param);
            break;
    }

    return ret;
}

void av_server_audio_data_callback(uint8_t *data, uint32_t length)
{
    bk_err_t ret = BK_OK;

    ret = bk_aud_intf_write_spk_data(data, length);

    if (ret != BK_OK)
    {
        //LOGE("write speaker data fail\n", length);
    }
}

#endif

#include "display_ui_cast_hooks.h"

/*
 * Stale-pipeline watchdog: when lcd_use_module == DECODER but no frames have
 * been decoded for STALE_PIPELINE_MAX_TICKS consecutive timer periods (each
 * 2 s → 4 s total), the pipeline is orphaned (e.g. CTRL disconnect without VIDEO
 * disconnect).  Post IMAGE_TCP_SERVICE_DISCONNECTED to safely tear it down
 * from the media message thread, avoiding the race with live video reception
 * that a direct turn_off from REMOTE_DEVICE_DISCONNECTED would cause.
 */
#define STALE_PIPELINE_MAX_TICKS 2U
static uint32_t s_stale_pipeline_ticks;

static void av_server_debug_timer_handle(void *arg)
{
    db_device_info_t *db_device_info = (db_device_info_t *)arg;
    uint32_t decode_frame_count = (db_device_info->debug_info.decode_frame_count - db_device_info->debug_info.last_decode_frame_count) / 2;
    media_data_process_debug_info_t debug_info = {0};
    get_last_debug_info(&debug_info);
    uint32_t wifi_transfer_frame_count = (debug_info.wifi_transfer_frame_count - db_device_info->debug_info.last_wifi_transfer_frame_count) / 2;
    uint32_t wifi_transfer_frame_size = (debug_info.wifi_transfer_frame_size - db_device_info->debug_info.last_wifi_transfer_frame_size) / 2;
    //uint32_t wifi_transfer_rate = (debug_info.wifi_transfer_rate - db_device_info->debug_info.last_wifi_transfer_rate) / 2;

    db_device_info->debug_info.last_decode_frame_count = db_device_info->debug_info.decode_frame_count;
    db_device_info->debug_info.last_wifi_transfer_frame_count = debug_info.wifi_transfer_frame_count;
    db_device_info->debug_info.last_wifi_transfer_frame_size = debug_info.wifi_transfer_frame_size;
    //db_device_info->debug_info.last_wifi_transfer_rate = debug_info.wifi_transfer_rate;
    #if 0
    LOGD("%s dec:%d[%d], wifi:%d[%d, %dKB %d]\n", __func__,
            decode_frame_count, db_device_info->debug_info.decode_frame_count,
            wifi_transfer_frame_count, wifi_transfer_frame_size, wifi_transfer_frame_size / 1024, wifi_transfer_rate);
    #endif
    LOGD("%s dec:%d[%d], wifi:%d[%d, %dKB]\n", __func__,
                decode_frame_count, db_device_info->debug_info.decode_frame_count,
                wifi_transfer_frame_count, wifi_transfer_frame_size, wifi_transfer_frame_size / 1024);

    if (db_device_info->lcd_use_module == LCD_SOURCE_MODULE_DECODER) {
        if (decode_frame_count == 0 && wifi_transfer_frame_count == 0) {
            s_stale_pipeline_ticks++;
            if (s_stale_pipeline_ticks >= STALE_PIPELINE_MAX_TICKS) {
                LOGW("stale cast pipeline (%u ticks, 0 frames), posting turn_off\n",
                     (unsigned)s_stale_pipeline_ticks);
                s_stale_pipeline_ticks = 0;
                media_msg_t msg = {0};
                msg.event = MEDIA_EVT_CAST_SESSION_TEARDOWN;
                msg.param = MEDIA_CAST_TEARDOWN_REASON_STALE_WATCHDOG;
                media_send_msg_wait(&msg);
            }
        } else {
            s_stale_pipeline_ticks = 0;
        }
    } else {
        s_stale_pipeline_ticks = 0;
    }
}

bk_err_t av_server_devices_init(void)
{
    LOGI("%s start\r\n", __func__);

    if (db_device_info == NULL)
    {
        db_device_info = os_malloc(sizeof(db_device_info_t));
    }

    if (db_device_info == NULL)
    {
        LOGE("malloc db_device_info failed");
        return  BK_FAIL;
    }

    os_memset(db_device_info, 0, sizeof(db_device_info_t));

    db_device_info->lcd_display_handle = display_ui_get_dpu_handle();
    db_device_info->lcd_use_module = LCD_SOURCE_MODULE_LVGL;

    LOGI("%s end\r\n", __func__);

    return BK_OK;
}

void av_server_devices_deinit(void)
{
    if (db_device_info)
    {
        os_free(db_device_info);
        db_device_info = NULL;
    }
}

/*
 * Weak defaults: boards with CONFIG_LCD_PANEL_USE_480X272 / 800X480 may override
 * in their LVGL app; scooter 1920x1080 uses full-screen DPU path only.
 */
__attribute__((weak)) void lvgl_app_enter_navigation(void) {}

__attribute__((weak)) void lvgl_app_exit_navigation(void) {}

__attribute__((weak)) void lvgl_app_display(uint8_t *data, uint32_t data_len, bool data_is_rgb565)
{
    (void)data;
    (void)data_len;
    (void)data_is_rgb565;
}

__attribute__((weak)) bk_err_t navigation_map_dma2d_yuyv2rgb565_init(void)
{
    return BK_OK;
}

__attribute__((weak)) bk_err_t navigation_map_dma2d_yuyv2rgb565_deinit(void)
{
    return BK_OK;
}
