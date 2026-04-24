#ifndef __AV_SERVER_DEVICES_H__
#define __AV_SERVER_DEVICES_H__

#include <stdbool.h>
#include <common/bk_err.h>

//#include "av_server_transmission.h"

#define DB_SAMPLE_RARE_8K (8000)
#define DB_SAMPLE_RARE_16K (16000)

#define UVC_DEVICE_ID (0xFDF6)


typedef struct
{
	uint16_t id;
	uint16_t rotate;
	uint16_t fmt;
} lcd_parameters_t;

typedef enum
{
	LCD_STATUS_CLOSE,
	LCD_STATUS_OPEN,
	LCD_STATUS_UNKNOWN,
} lcd_status_t;

/**
 * @brief LCD 模块数据源枚举类型
 *
 * 定义 LCD 模块使用的数据源，可以是解码模块或 LVGL
 */
typedef enum
{
	LCD_SOURCE_MODULE_DECODER = 0,  /**< 解码模块作为数据源 */
	LCD_SOURCE_MODULE_LVGL = 1,     /**< LVGL 作为数据源 */
} lcd_source_module_t;

typedef enum
{
	CODEC_FORMAT_UNKNOW = 0,
	CODEC_FORMAT_G711A = 1,
	CODEC_FORMAT_PCM = 2,
	CODEC_FORMAT_G711U = 3,
} codec_format_t;

typedef struct
{
	uint8_t aec;
	uint8_t uac;
	uint8_t rmt_recoder_fmt; /* codec_format_t */
	uint8_t rmt_player_fmt; /* codec_format_t */
	uint32_t rmt_recorder_sample_rate;
	uint32_t rmt_player_sample_rate;
} audio_parameters_t;


typedef enum
{
	AA_UNKNOWN = 0,
	AA_ECHO_DEPTH = 1,
	AA_MAX_AMPLITUDE = 2,
	AA_MIN_AMPLITUDE = 3,
	AA_NOISE_LEVEL = 4,
	AA_NOISE_PARAM = 5,
} audio_acoustics_t;


void av_server_devices_deinit(void);
bk_err_t av_server_devices_init(void);

bk_err_t av_server_audio_turn_on(audio_parameters_t *parameters);
bk_err_t av_server_audio_turn_off(void);
bk_err_t av_server_audio_acoustics(uint32_t index, uint32_t param);
void av_server_audio_data_callback(uint8_t *data, uint32_t length);

bk_err_t av_server_display_turn_on(void *lcd, uint16_t rotate);
bk_err_t av_server_display_turn_off(void);

bk_err_t av_server_jpeg_decode_manager_turn_on(void);
bk_err_t av_server_jpeg_decode_manager_turn_off(void);
bk_err_t av_server_jpeg_decode_manager_suspend(void);
bk_err_t av_server_jpeg_decode_manager_resume(void);
bk_err_t av_server_jpeg_decode_manager_get_decoder_type(void);

void lvgl_app_init(void);
bk_err_t lvgl_app_deinit(void);
bk_err_t lvgl_app_suspend_display(void);
bk_err_t lvgl_app_resume_display(void);

bk_err_t navigation_map_dma2d_yuyv2rgb565_init(void);
bk_err_t navigation_map_dma2d_yuyv2rgb565_deinit(void);

void lvgl_app_enter_navigation(void);
void lvgl_app_exit_navigation(void);

void lvgl_app_display(uint8_t *data, uint32_t data_len, bool data_is_rgb565);

void set_lcd_use_module(lcd_source_module_t module);
#endif
