#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <common/bk_err.h>
#include <getopt.h>

#include <driver/lcd.h>

#include "media_devices.h"
#include "frame_buffer_que.h"

#include "media_network_transfer.h"
#include "network_transfer.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include "components/bk_display.h"
#include "lcd_panel_devices.h"
#include "driver/gpio.h"
#include "gpio_driver.h"

#include "frame_buffer.h"

#include "jpeg_decode_manager.h"

#include "network_provisioning.h"

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
    jpeg_decode_manager_t *manager;
    jpeg_decode_manager_config_t config;
    bk_display_ctlr_handle_t lcd_display_handle;
    uint32_t lcd_use_module;
    db_device_debug_info_t debug_info;
    beken_timer_t debug_timer;
} db_device_info_t;

static const lcd_device_t *lcd_device =  &lcd_device_st7282;
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

static avdk_err_t display_frame_free_cb(void *frame)
{
    frame_buffer_display_free((frame_buffer_t *)frame);
    return AVDK_ERR_OK;
}

static bk_err_t decode_complete_callback(uint32_t format_type, uint32_t result, frame_buffer_t *out_frame)
{
    avdk_err_t ret = AVDK_ERR_OK;
    if (result == BK_OK) {
        LOGV("Decode complete, format: %d, width: %d, height: %d\n", 
             format_type, out_frame->width, out_frame->height);
        db_device_info->debug_info.decode_frame_count++;
        // 这里可以处理解码后的图像数据
        // 例如：显示图像、保存图像等
        if (db_device_info->lcd_use_module == LCD_SOURCE_MODULE_DECODER)
        {
            if (get_navigation_type() == NAVIGATION_TYPE_BT)
            {
                lvgl_app_display_navigation((uint8_t *)out_frame->frame, out_frame->size);
            }
            else
            {
                ret = bk_display_flush(db_device_info->lcd_display_handle, (void *)out_frame, display_frame_free_cb);
                if (ret != BK_OK)
                {
                    LOGE("bk_display_flush failed\n");
                    frame_buffer_display_free(out_frame);
                }
            }
        }
        else
        {
            frame_buffer_display_free(out_frame);
        }
    } else {
        LOGE("Decode failed with result: %d\n", result);
    }
    
    return BK_OK;
}

int av_server_jpeg_decode_manager_suspend(void)
{
    if (db_device_info != NULL && db_device_info->manager != NULL) {
        jpeg_decode_manager_suspend(db_device_info->manager);
    }
    
    return BK_OK;
}

int av_server_jpeg_decode_manager_resume(void)
{
    if (db_device_info != NULL && db_device_info->manager != NULL) {
        jpeg_decode_manager_resume(db_device_info->manager);
    }
    
    return BK_OK;
}

int av_server_jpeg_decode_manager_turn_on(void)
{
    int ret = -1;
    os_memset(&db_device_info->config, 0, sizeof(jpeg_decode_manager_config_t));
    db_device_info->config.queue_size = 10;                // 队列大小为10
    db_device_info->config.thread_priority = BEKEN_DEFAULT_WORKER_PRIORITY;           // 线程优先级为6
    db_device_info->config.thread_stack_size = 2 * 1024;   // 线程栈大小为2KB
    db_device_info->config.decode_cbs.out_complete = decode_complete_callback; // 设置完成回调
    db_device_info->config.out_format = JPEG_DECODE_SW_OUT_FORMAT_RGB565;
    db_device_info->config.byte_order = JPEG_DECODE_LITTLE_ENDIAN;

    db_device_info->debug_info.decode_frame_count = 0;
    db_device_info->debug_info.last_decode_frame_count = 0;
    // 创建解码管理器
    if (db_device_info->manager == NULL) {
        db_device_info->manager = jpeg_decode_manager_create(&db_device_info->config);
        if (db_device_info->manager == NULL) {
            LOGE("Failed to create JPEG decode manager\n");
            return ret;
        }
    }
    else
    {
        LOGE("JPEG decode manager already created\n");
    }

    set_lcd_use_module(LCD_SOURCE_MODULE_DECODER);

    return BK_OK;
}

int av_server_jpeg_decode_manager_turn_off(void)
{
    int ret = -1;
    if (db_device_info->manager != NULL) {
        ret = jpeg_decode_manager_destroy(db_device_info->manager);
        db_device_info->manager = NULL;
    }

    return ret;
}

#if 1
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
    
int av_server_display_turn_on(void *param, uint16_t rotate)
{
    int ret = -1;
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
    return 0;
}

int av_server_display_turn_off(void)
{
    return 0;
}
#endif

int av_server_udp_voice_send_callback(unsigned char *data, unsigned int len)
{
    LOGD("%s, data: %p, len: %u\n", __func__, data, len);

    return ntwk_trans_audio_send(data, len, AUDIO_ENC_TYPE_G711A);
}

static void av_server_audio_connect_state_cb_handle(uint8_t state)
{
    os_printf("[--%s--] state: %d \n", __func__, state);
}

#if AUDIO_TRANSFER_ENABLE
int av_server_audio_turn_on(audio_parameters_t *parameters)
{
    int ret;

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

int av_server_audio_turn_off(void)
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

int av_server_audio_acoustics(uint32_t index, uint32_t param)
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

extern lv_vnd_config_t vendor_config;

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
}

int av_server_devices_init(void)
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

    db_device_info->lcd_display_handle = vendor_config.handle;
    db_device_info->lcd_use_module = LCD_SOURCE_MODULE_LVGL;

    rtos_init_timer(&db_device_info->debug_timer, 2000, av_server_debug_timer_handle, db_device_info);
    rtos_start_timer(&db_device_info->debug_timer);

    LOGI("%s end\r\n", __func__);

    return BK_OK;
}

void av_server_devices_deinit(void)
{
    if (db_device_info)
    {
        rtos_stop_timer(&db_device_info->debug_timer);
        rtos_deinit_timer(&db_device_info->debug_timer);
        os_free(db_device_info);
        db_device_info = NULL;
    }
}
