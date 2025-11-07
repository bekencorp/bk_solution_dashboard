#ifndef SETUP_SCR_SCREEN_H
#define SETUP_SCR_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *screen_img_1;
    lv_obj_t *screen_meter_1;
    lv_obj_t *screen_meter_1_ndline_0;
    lv_obj_t *screen_digital_clock_1;
    lv_obj_t *screen_img_4;
    lv_obj_t *screen_img_5;
    lv_obj_t *screen_img_6;
    lv_obj_t *screen_img_7;
    lv_obj_t *screen_bar_1;
    lv_obj_t *screen_img_8;
    lv_obj_t *screen_label_1;
    lv_obj_t *screen_label_2;
    lv_obj_t *screen_label_3;
    lv_obj_t *screen_label_4;
    lv_obj_t *screen_label_5;
    lv_obj_t *screen_label_6;
    lv_obj_t *screen_label_7;
} lv_ui;

LV_IMAGE_DECLARE(_moto_RGB565A8_480x272);
LV_IMAGE_DECLARE(_beken_logo_blue_RGB565A8_73x36);
LV_IMAGE_DECLARE(_bluetooth_1_RGB565A8_21x20);
LV_IMAGE_DECLARE(_light_green_RGB565A8_49x40);
LV_IMAGE_DECLARE(_wifi_1_RGB565A8_20x20);
LV_IMAGE_DECLARE(_light_grey_RGB565A8_49x40);
LV_IMAGE_DECLARE(_battery_bak_RGB565A8_26x14);
LV_IMAGE_DECLARE(_battery_ind_RGB565A8_26x14);
LV_IMAGE_DECLARE(_light_11_RGB565A8_28x23);
LV_IMAGE_DECLARE(_kmbg_RGB565A8_100x100);

LV_FONT_DECLARE(lv_font_montserratMedium_13)
LV_FONT_DECLARE(lv_font_montserratMedium_16)
LV_FONT_DECLARE(lv_font_Abel_regular_16)
LV_FONT_DECLARE(lv_font_montserratMedium_21)
LV_FONT_DECLARE(lv_font_montserratMedium_32)
LV_FONT_DECLARE(lv_font_montserratMedium_14)
LV_FONT_DECLARE(lv_font_montserratMedium_24)

void lv_setup_ui(void);

#ifdef __cplusplus
}
#endif

#endif
