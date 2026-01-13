/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 * 
 * This software is proprietary and confidential. No part of this software may be
 * reproduced, distributed, or transmitted in any form or by any means, including
 * photocopying, recording, or other electronic or mechanical methods, without the
 * prior written permission of BekenCorp, except in the case of brief quotations
 * embodied in critical reviews and certain other noncommercial uses permitted
 * by copyright law.
 * 
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.

 * Author: Beken LVGL Designer Tool
*/
/**
 * @file beken_ui.c
 * @brief Beken UI implementation file
 * 
 * This file contains the implementation of the Beken UI system.
 * Customers can modify this file to customize their UI without
 * touching the main application code or build system.
 */

#ifndef __BEKEN_UI_H__
#define __BEKEN_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Display configuration */
#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   480

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *page_1;
    lv_obj_t *scale_1;
    lv_obj_t *scale_1_needle_0;
    lv_obj_t *image_2;
    lv_obj_t *image_3;
    lv_obj_t *image_4;
    lv_obj_t *dclock_1;
    lv_obj_t *image_5;
    lv_obj_t *image_6;
    lv_obj_t *image_7;
    lv_obj_t *image_8;
    lv_obj_t *image_9;
    lv_obj_t *label_7;
    lv_obj_t *image_10;
    lv_obj_t *obj_1;
    lv_obj_t *spangroup_1;
    lv_span_t *spangroup_1_span_0;
    lv_span_t *spangroup_1_span_1;
    lv_obj_t *label_1;
    lv_obj_t *obj_2;
    lv_obj_t *spangroup_2;
    lv_span_t *spangroup_2_span_0;
    lv_span_t *spangroup_2_span_1;
    lv_obj_t *label_8;

    lv_obj_t *page_2;
    lv_obj_t *page_2_obj_1;
    lv_obj_t *page_2_label_1;
    lv_obj_t *page_2_label_4;
    lv_obj_t *page_2_label_5;
    lv_obj_t *page_2_image_1;
    lv_obj_t *page_2_image_2;
    lv_obj_t *page_2_dclock_1;
    lv_obj_t *page_2_image_3;
    lv_obj_t *page_2_obj_3;
    lv_obj_t *page_2_image_4;
    lv_obj_t *page_2_image_5;
    lv_obj_t *page_2_image_6;
    lv_obj_t *page_2_image_7;
    lv_obj_t *page_2_image_9;
    lv_obj_t *page_2_obj_4;
    lv_obj_t *page_2_label_6;
    lv_obj_t *page_2_label_7;
    lv_obj_t *page_2_label_8;
    lv_obj_t *page_2_image_10;
} bk_lv_ui_t;

void init_page_page_1(bk_lv_ui_t *bk_ui);
void init_page_page_2(bk_lv_ui_t *bk_ui);

void destroy_page_page_2(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(light_green_70x74_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_grey_70x74_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_blue_118x65_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_bak_57x35_RGB565A8_NONE);
LV_IMAGE_DECLARE(wifi_1_40x42_RGB565A8_NONE);
LV_IMAGE_DECLARE(bluetooth_40x42_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_11_52x37_RGB565A8_NONE);
LV_IMAGE_DECLARE(kmbg_207x212_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_ind_47x35_RGB565A8_NONE);
LV_IMAGE_DECLARE(moto1_800x480_ARGB8888);

LV_IMAGE_DECLARE(battery_bak_63x41_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_ind_57x50_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_blue_81x47_RGB565A8_NONE);
LV_IMAGE_DECLARE(bluetooth_38x41_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_11_47x38_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_green_46x48_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_grey_46x48_RGB565A8_NONE);
LV_IMAGE_DECLARE(wifi_1_38x41_RGB565A8_NONE);

/* declare animimg sources */

/* declare fonts */
LV_FONT_DECLARE(lv_font_montserrat_regular_36);
LV_FONT_DECLARE(lv_font_montserrat_regular_28);
LV_FONT_DECLARE(lv_font_montserrat_regular_20);
LV_FONT_DECLARE(lv_font_montserrat_regular_22);

/**
 * @brief Initialize the Beken UI system
 * 
 * This function initializes the UI components and creates the main interface.
 * Customers can modify this function to customize their UI layout.
 */
void beken_ui_init(void);

/**
 * @brief Get the configured screen width
 * @return Screen width in pixels
 */
int beken_get_screen_width(void);

/**
 * @brief Get the configured screen height
 * @return Screen height in pixels
 */
int beken_get_screen_height(void);

extern bk_lv_ui_t bk_lv_tool_ui;

void lv_digital_clock_timer(lv_timer_t *timer);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);
void lv_mileage_timer_cb(lv_timer_t *timer);
void lv_speed_timer_cb(lv_timer_t *timer);
void lv_turn_signal_timer_cb(lv_timer_t *timer);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __BEKEN_UI_H__ */
