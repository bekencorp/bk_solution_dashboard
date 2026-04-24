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
/*
 * @file: beken_ui.c
 * @brief: beken ui implementation file
 * This file contains the implementation of the Beken UI system.
 * Customers can modify the UI implementation in beken_ui.c without
 * touching the main application code.
 */

#include "beken_ui.h"
#include <os/os.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

bk_lv_ui_t bk_lv_tool_ui = {0};

/* ---------- Speed gauge demo: 0->100->0 over ~10s ---------- */
#define SPEED_NEEDLE_LENGTH    250
#define SPEED_ANIM_PERIOD_MS   50   /* 50 ms * 200 steps ~= 10 s */
static int32_t s_speed_value = 0;
static int32_t s_speed_direction = 1;
static lv_timer_t *s_speed_anim_timer = NULL;

static void speed_anim_apply_cb(void *user_data)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    (void)user_data;
    if (!ui->Page_1_scale_1 || !ui->Page_1_scale_1_needle_0 || !ui->Page_1_label_3)
        return;
    lv_scale_set_line_needle_value(ui->Page_1_scale_1, ui->Page_1_scale_1_needle_0,
                                   SPEED_NEEDLE_LENGTH, s_speed_value);
    lv_label_set_text_fmt(ui->Page_1_label_3, "%" LV_PRId32, s_speed_value);
}

static void speed_anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_speed_value += s_speed_direction;
    if (s_speed_value >= 100) {
        s_speed_value = 100;
        s_speed_direction = -1;
    } else if (s_speed_value <= 0) {
        s_speed_value = 0;
        s_speed_direction = 1;
    }
    lv_async_call(speed_anim_apply_cb, NULL);
}

/* ---------- Left turn signal: green/grey blink ---------- */
#define LEFT_LIGHT_BLINK_PERIOD_MS  500
static bool s_left_light_on = true;  /* green image visible when true */
static lv_timer_t *s_left_light_timer = NULL;

static void left_light_blink_apply_cb(void *user_data)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    (void)user_data;
    if (!ui->Page_1_image_16 || !ui->Page_1_image_17)
        return;

    if (s_left_light_on) {
        /* green visible, grey hidden */
        lv_obj_set_style_image_opa(ui->Page_1_image_16, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_image_opa(ui->Page_1_image_17, 0,   LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        /* grey visible, green hidden */
        lv_obj_set_style_image_opa(ui->Page_1_image_16, 0,   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_image_opa(ui->Page_1_image_17, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void left_light_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_left_light_on = !s_left_light_on;
    lv_async_call(left_light_blink_apply_cb, NULL);
}

/**
 * @brief Get the configured screen width
 * @return Screen width in pixels
 */
int beken_get_screen_width(void)
{
    return SCREEN_WIDTH;
}

/**
 * @brief Get the configured screen height
 * @return Screen height in pixels
 */
int beken_get_screen_height(void)
{
    return SCREEN_HEIGHT;
}

/**
 * @brief Initialize the Beken UI system
 */
void beken_ui_init(void)
{
    
    init_page_Page_1(&bk_lv_tool_ui);
    lv_screen_load(bk_lv_tool_ui.Page_1);

    /* Speed gauge animation: 0->100->0 in ~10 s */
    lv_async_call(speed_anim_apply_cb, NULL);
    s_speed_anim_timer = lv_timer_create(speed_anim_timer_cb, SPEED_ANIM_PERIOD_MS, NULL);

    /* Left turn signal: alternate green/grey images */
    lv_async_call(left_light_blink_apply_cb, NULL);
    s_left_light_timer = lv_timer_create(left_light_timer_cb, LEFT_LIGHT_BLINK_PERIOD_MS, NULL);
}

/*
 * Pre-cast hook (strong symbol overrides weak default in display_ui_cast_hooks.c).
 * Called with lv_vendor_disp_lock held; LVGL thread is blocked on the lock so timers are
 * still on the active list and lv_timer_delete is safe.
 * Must run before lv_vendor_stop; after stop, timer pointers may be invalid.
 */
void beken_ui_before_cast_lvgl_teardown(void)
{
    if (s_speed_anim_timer) {
        lv_timer_delete(s_speed_anim_timer);
        s_speed_anim_timer = NULL;
    }
    if (s_left_light_timer) {
        lv_timer_delete(s_left_light_timer);
        s_left_light_timer = NULL;
    }
}

uint32_t beken_ui_after_cast_ui_painted_delay_ms(void)
{
    return 0;
}

/*
 * After cast ends: restore standby UI (caller holds lv_vendor_disp_lock).
 *
 * Timers were removed in beken_ui_before_cast_lvgl_teardown (pointers cleared).
 * Recreate with lv_timer_create here so only one pair of timers exists.
 */
void beken_ui_kick_after_display_resume(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (ui->Page_1 != NULL && lv_obj_is_valid(ui->Page_1)) {
        lv_screen_load(ui->Page_1);
        lv_obj_invalidate(ui->Page_1);
    } else {
        init_page_Page_1(ui);
        lv_screen_load(ui->Page_1);
    }

    s_speed_anim_timer = lv_timer_create(speed_anim_timer_cb, SPEED_ANIM_PERIOD_MS, NULL);
    s_left_light_timer = lv_timer_create(left_light_timer_cb, LEFT_LIGHT_BLINK_PERIOD_MS, NULL);

    speed_anim_apply_cb(NULL);
    left_light_blink_apply_cb(NULL);
}