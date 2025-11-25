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
} bk_lv_ui_t;

void init_page_page_1(bk_lv_ui_t *bk_ui);

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
