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
#define SCREEN_WIDTH    480
#define SCREEN_HEIGHT   272

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *page_1;
    lv_obj_t *page_1_obj_1;
    lv_obj_t *page_1_label_1;
    lv_obj_t *page_1_spangroup_1;
    lv_span_t *page_1_spangroup_1_span_0;
    lv_span_t *page_1_spangroup_1_span_1;
    lv_obj_t *page_1_obj_2;
    lv_obj_t *page_1_label_2;
    lv_obj_t *page_1_spangroup_2;
    lv_span_t *page_1_spangroup_2_span_0;
    lv_span_t *page_1_spangroup_2_span_1;
    lv_obj_t *page_1_image_1;
    lv_obj_t *page_1_image_2;
    lv_obj_t *page_1_dclock_1;
    lv_obj_t *page_1_image_3;
    lv_obj_t *page_1_obj_3;
    lv_obj_t *page_1_image_4;
    lv_obj_t *page_1_image_5;
    lv_obj_t *page_1_image_6;
    lv_obj_t *page_1_image_7;
    lv_obj_t *page_1_scale_1;
    lv_obj_t *page_1_scale_1_needle_0;
    lv_obj_t *page_1_image_8;
    lv_obj_t *page_1_label_3;
    lv_obj_t *page_1_image_9;
    /* Page: 1 objects */
    lv_obj_t *page_2;
    lv_obj_t *page_2_obj_1;
    lv_obj_t *page_2_image_1;
    lv_obj_t *page_2_image_2;
    lv_obj_t *page_2_dclock_1;
    lv_obj_t *page_2_line_1;
    lv_obj_t *page_2_line_2;
    lv_obj_t *page_2_image_3;
    lv_obj_t *page_2_image_4;
    lv_obj_t *page_2_image_5;
    lv_obj_t *page_2_image_6;
    lv_obj_t *page_2_image_7;
    lv_obj_t *page_2_obj_2;
    lv_obj_t *page_2_label_1;
    lv_obj_t *page_2_spangroup_1;
    lv_span_t *page_2_spangroup_1_span_0;
    lv_span_t *page_2_spangroup_1_span_1;
    lv_obj_t *page_2_label_2;
    lv_obj_t *page_2_spangroup_2;
    lv_span_t *page_2_spangroup_2_span_0;
    lv_span_t *page_2_spangroup_2_span_1;
    lv_obj_t *page_2_image_8;
    lv_obj_t *page_2_obj_3;
    lv_obj_t *page_2_scale_1;
    lv_obj_t *page_2_scale_1_needle_0;
    lv_obj_t *page_2_label_3;
    lv_obj_t *page_2_label_4;
    lv_obj_t *page_2_label_5;
} bk_lv_ui_t;

void init_page_page_1(bk_lv_ui_t *bk_ui);
void destroy_page_page_1(bk_lv_ui_t *bk_ui);
void init_page_page_2(bk_lv_ui_t *bk_ui);
void destroy_page_page_2(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(light_green_45x45_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_grey_46x46_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_blue_76x42_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_bak_38x23_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_ind_34x22_RGB565A8_NONE);
LV_IMAGE_DECLARE(wifi_1_23x23_RGB565A8_NONE);
LV_IMAGE_DECLARE(bluetooth_23x23_RGB565A8_NONE);
LV_IMAGE_DECLARE(kmbg_113x110_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_11_34x26_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_green_30x30_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_grey_30x30_RGB565A8_NONE);
LV_IMAGE_DECLARE(wifi_1_26x26_RGB565A8_NONE);
LV_IMAGE_DECLARE(bluetooth_28x28_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_11_29x22_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_bak_28x20_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_ind_28x21_RGB565A8_NONE);
LV_IMAGE_DECLARE(moto_ARGB8888);

/* declare animimg sources */

/* declare fonts */

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

void lv_mileage_timer_cb(lv_timer_t *timer);

void lv_digital_clock_timer_cb(lv_timer_t *timer);

void lv_speed_timer_cb(lv_timer_t *timer);

void lv_turn_signal_timer_cb(lv_timer_t *timer);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __BEKEN_UI_H__ */
