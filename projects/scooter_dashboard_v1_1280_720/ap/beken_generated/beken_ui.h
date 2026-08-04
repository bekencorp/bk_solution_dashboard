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
#define SCREEN_WIDTH    1280
#define SCREEN_HEIGHT   720

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *Page_1;
    lv_obj_t *Page_1_obj_1;
    lv_obj_t *Page_1_label_1;
    lv_obj_t *Page_1_spangroup_1;
    lv_span_t *Page_1_spangroup_1_span_0;
    lv_span_t *Page_1_spangroup_1_span_1;
    lv_obj_t *Page_1_obj_2;
    lv_obj_t *Page_1_label_2;
    lv_obj_t *Page_1_spangroup_2;
    lv_span_t *Page_1_spangroup_2_span_0;
    lv_span_t *Page_1_spangroup_2_span_1;
    lv_obj_t *Page_1_dclock_1;
    lv_obj_t *Page_1_scale_1;
    lv_obj_t *Page_1_scale_1_needle_0;
    lv_obj_t *Page_1_image_8;
    lv_obj_t *Page_1_label_3;
    lv_obj_t *Page_1_image_10;
    lv_obj_t *Page_1_image_11;
    lv_obj_t *Page_1_image_13;
    lv_obj_t *Page_1_image_14;
    lv_obj_t *Page_1_image_15;
    lv_obj_t *Page_1_image_16;
    lv_obj_t *Page_1_image_17;
    /* Page: 1 objects */
    lv_obj_t *page_2;
    lv_obj_t *page_2_label_1;
    lv_obj_t *page_2_label_2;
    /* Page: pet objects */
    lv_obj_t *page_pet;
    lv_obj_t *page_pet_gif;
} bk_lv_ui_t;

void init_page_Page_1(bk_lv_ui_t *bk_ui);
void destroy_page_Page_1(bk_lv_ui_t *bk_ui);
void init_page_page_2(bk_lv_ui_t *bk_ui);
void destroy_page_page_2(bk_lv_ui_t *bk_ui);
void init_page_pet(bk_lv_ui_t *bk_ui);
void destroy_page_pet(bk_lv_ui_t *bk_ui);
void pet_page_toggle(void);

/* declare image */
LV_IMAGE_DECLARE(battery_bak_95x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(battery_ind_102x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_blue_190x102_RGB565A8_NONE);
LV_IMAGE_DECLARE(kmbg_301x291_RGB565A8_NONE);
LV_IMAGE_DECLARE(l_green_128x128_RGB565A8_NONE);
LV_IMAGE_DECLARE(l_grey_128x128_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_11_83x64_RGB565A8_NONE);
LV_IMAGE_DECLARE(right_light_grey_128x128_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_Tinos_Regular_80);
LV_FONT_DECLARE(lv_font_Tinos_Bold_80);
LV_FONT_DECLARE(lv_font_Tinos_Regular_60);
LV_FONT_DECLARE(lv_font_Tinos_Regular_30);
LV_FONT_DECLARE(lv_font_montserrat_regular_48);
LV_FONT_DECLARE(lv_font_montserrat_regular_30);

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

/* Digital clock functions */
void lv_digital_clock_timer(lv_timer_t *timer);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __BEKEN_UI_H__ */
