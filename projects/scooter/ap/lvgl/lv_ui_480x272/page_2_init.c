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
#include "lvgl.h"
#include "beken_ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static lv_timer_t *mileage_timer = NULL;
static lv_timer_t *turn_signal_timer = NULL;
static lv_timer_t *digital_clock_timer = NULL;
static lv_timer_t *speed_timer = NULL;

static bool lv_turn_signal_is_green = true;
static bool lv_speed_is_increase = true;

static void lv_page2_mileage_timer_cb(lv_timer_t *timer)
{
    static uint32_t trip_value = 30;
    static uint32_t mileage_value = 125;
    char trip[6];
    char mileage[6];

    trip_value ++;
    sprintf(trip, "%d", (int)trip_value);
    lv_spangroup_set_span_text(bk_lv_tool_ui.page_2_spangroup_1, bk_lv_tool_ui.page_2_spangroup_1_span_0, trip);

    mileage_value++;
    sprintf(mileage, "%d", (int)mileage_value);
    lv_spangroup_set_span_text(bk_lv_tool_ui.page_2_spangroup_2, bk_lv_tool_ui.page_2_spangroup_2_span_0, mileage);

    if (trip_value > 299) {
        trip_value = 0;
    }

    if (mileage_value > 99999) {
        mileage_value = 0;
    }
}

static void lv_page2_digital_clock_timer_cb(lv_timer_t *timer)
{
    static int hour = 11;
    static int minute = 25;
    static int second = 55;
    static char meridiem[] = "AM";

    (second)++;
    if(second >= 60) {
        second = 0;
        minute++;
    }
    if(minute >= 60) {
        minute = 0;
        if(hour < 12) {
            hour++;
        }
        else {
            hour++;
            hour = hour % 12;
        }
    }
    if(hour == 12 && second == 0 && minute == 0) {
        if((lv_strcmp(meridiem, "PM") == 0)) {
            lv_strcpy(meridiem, "AM");
        }
        else {
            lv_strcpy(meridiem, "PM");
        }
    }
    lv_obj_t *time_obj = (lv_obj_t *)lv_timer_get_user_data(timer);

    if (lv_obj_is_valid(time_obj))
    {
        lv_label_set_text_fmt(time_obj, "%d:%02d %s", hour, minute, meridiem);
    }
}

static void lv_page2_speed_timer_cb(lv_timer_t *timer)
{
    static int32_t value = 0;
    char speed[3];

    if (value == 99) {
        lv_speed_is_increase = false;
    }

    if (value == 0) {
        lv_speed_is_increase = true;
    }

    if (lv_speed_is_increase == true) {
        if (value >= 0 && value <= 60) {
            value += 5;
            lv_timer_set_period(timer, 100);
        }

        if (value > 60 && value <= 80) {
            value += 2;
            lv_timer_set_period(timer, 200);
        }

        if (value > 80 && value < 99) {
            value += 1;
            lv_timer_set_period(timer, 300);
        }
    } else {
        value -= 4;
        if(value < 0) {
            value = 0;
        }
        lv_timer_set_period(timer, 180);
    }

    sprintf(speed, "%d", value);
    lv_label_set_text(bk_lv_tool_ui.page_2_label_3, speed);
}

static void lv_page2_turn_signal_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *signal_obj = (lv_obj_t *)lv_timer_get_user_data(timer);

    if (lv_turn_signal_is_green) {
        lv_image_set_src(signal_obj, &light_grey_30x30_RGB565A8_NONE);
        lv_turn_signal_is_green = false;
    } else {
        lv_image_set_src(signal_obj, &light_green_30x30_RGB565A8_NONE);
        lv_turn_signal_is_green = true;
    }
}

/*
 * @brief: init page page_2
 */
void init_page_page_2(bk_lv_ui_t *bk_ui)
{
    bk_ui->page_2 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_2, 480, 272);
    lv_obj_set_style_bg_color(bk_ui->page_2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_obj_1 = lv_obj_create(bk_ui->page_2);
    lv_obj_set_x(bk_ui->page_2_obj_1, 10);
    lv_obj_set_y(bk_ui->page_2_obj_1, 10);
    lv_obj_set_width(bk_ui->page_2_obj_1, 459);
    lv_obj_set_height(bk_ui->page_2_obj_1, 47);
    lv_obj_remove_flag(bk_ui->page_2_obj_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(bk_ui->page_2_obj_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_obj_1, 38, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_obj_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_obj_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_obj_1, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_obj_1, 28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_obj_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_obj_1, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_obj_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_obj_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_image_1 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_1, &light_green_30x30_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_1, 0);
    lv_obj_set_x(bk_ui->page_2_image_1, 8);
    lv_obj_set_y(bk_ui->page_2_image_1, 8);
    lv_obj_set_width(bk_ui->page_2_image_1, 30);
    lv_obj_set_height(bk_ui->page_2_image_1, 30);
    lv_obj_remove_flag(bk_ui->page_2_image_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    turn_signal_timer = lv_timer_create(lv_page2_turn_signal_timer_cb, 600, bk_ui->page_2_image_1);

    bk_ui->page_2_image_2 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_2, &light_grey_30x30_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_2, 1800);
    lv_obj_set_x(bk_ui->page_2_image_2, 422);
    lv_obj_set_y(bk_ui->page_2_image_2, 8);
    lv_obj_set_width(bk_ui->page_2_image_2, 30);
    lv_obj_set_height(bk_ui->page_2_image_2, 30);
    lv_obj_remove_flag(bk_ui->page_2_image_2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    static bool screen_digital_clock_1_timer_enabled = false;
    bk_ui->page_2_dclock_1 = lv_label_create(bk_ui->page_2_obj_1);
    lv_label_set_text(bk_ui->page_2_dclock_1, "11:25 AM");
    lv_obj_set_x(bk_ui->page_2_dclock_1, 59);
    lv_obj_set_y(bk_ui->page_2_dclock_1, 12);
    lv_obj_set_width(bk_ui->page_2_dclock_1, 120);
    lv_obj_set_height(bk_ui->page_2_dclock_1, 20);
    lv_obj_add_flag(bk_ui->page_2_dclock_1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(bk_ui->page_2_dclock_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_text_color(bk_ui->page_2_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_dclock_1, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_dclock_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!screen_digital_clock_1_timer_enabled) {
        digital_clock_timer = lv_timer_create(lv_page2_digital_clock_timer_cb, 1000, bk_ui->page_2_dclock_1);
        screen_digital_clock_1_timer_enabled = true;
    }

    bk_ui->page_2_line_1 = lv_line_create(bk_ui->page_2_obj_1);
    static lv_point_precise_t page_2_line_1_points[] = {
        { 0, 0 },
        { 0, 30 }
    };
    lv_line_set_points(bk_ui->page_2_line_1, page_2_line_1_points, 2);
    lv_line_set_y_invert(bk_ui->page_2_line_1, false);
    lv_obj_set_x(bk_ui->page_2_line_1, 49);
    lv_obj_set_y(bk_ui->page_2_line_1, 12);
    lv_obj_set_width(bk_ui->page_2_line_1, 20);
    lv_obj_set_height(bk_ui->page_2_line_1, 22);
    lv_obj_add_flag(bk_ui->page_2_line_1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(bk_ui->page_2_line_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_line_2 = lv_line_create(bk_ui->page_2_obj_1);
    static lv_point_precise_t page_2_line_2_points[] = {
        { 0, 0 },
        { 0, 30 }
    };
    lv_line_set_points(bk_ui->page_2_line_2, page_2_line_2_points, 2);
    lv_line_set_y_invert(bk_ui->page_2_line_2, false);
    lv_obj_set_x(bk_ui->page_2_line_2, 408);
    lv_obj_set_y(bk_ui->page_2_line_2, 11);
    lv_obj_set_width(bk_ui->page_2_line_2, 20);
    lv_obj_set_height(bk_ui->page_2_line_2, 22);
    lv_obj_add_flag(bk_ui->page_2_line_2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(bk_ui->page_2_line_2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_image_3 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_3, &wifi_1_26x26_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_3, 0);
    lv_obj_set_x(bk_ui->page_2_image_3, 326);
    lv_obj_set_y(bk_ui->page_2_image_3, 10);
    lv_obj_set_width(bk_ui->page_2_image_3, 26);
    lv_obj_set_height(bk_ui->page_2_image_3, 26);
    lv_obj_remove_flag(bk_ui->page_2_image_3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_image_4 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_4, &bluetooth_28x28_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_4, 0);
    lv_obj_set_x(bk_ui->page_2_image_4, 284);
    lv_obj_set_y(bk_ui->page_2_image_4, 9);
    lv_obj_set_width(bk_ui->page_2_image_4, 28);
    lv_obj_set_height(bk_ui->page_2_image_4, 28);
    lv_obj_remove_flag(bk_ui->page_2_image_4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_image_5 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_5, &light_11_29x22_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_5, 0);
    lv_obj_set_x(bk_ui->page_2_image_5, 241);
    lv_obj_set_y(bk_ui->page_2_image_5, 12);
    lv_obj_set_width(bk_ui->page_2_image_5, 29);
    lv_obj_set_height(bk_ui->page_2_image_5, 22);
    lv_obj_remove_flag(bk_ui->page_2_image_5, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_image_6 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_6, &battery_bak_28x20_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_6, 0);
    lv_obj_set_x(bk_ui->page_2_image_6, 367);
    lv_obj_set_y(bk_ui->page_2_image_6, 13);
    lv_obj_set_width(bk_ui->page_2_image_6, 28);
    lv_obj_set_height(bk_ui->page_2_image_6, 20);
    lv_obj_remove_flag(bk_ui->page_2_image_6, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_image_7 = lv_image_create(bk_ui->page_2_obj_1);
    lv_image_set_src(bk_ui->page_2_image_7, &battery_ind_28x21_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_2_image_7, 0);
    lv_obj_set_x(bk_ui->page_2_image_7, 366);
    lv_obj_set_y(bk_ui->page_2_image_7, 13);
    lv_obj_set_width(bk_ui->page_2_image_7, 28);
    lv_obj_set_height(bk_ui->page_2_image_7, 21);
    lv_obj_remove_flag(bk_ui->page_2_image_7, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bk_ui->page_2_obj_2 = lv_obj_create(bk_ui->page_2);
    lv_obj_set_x(bk_ui->page_2_obj_2, 288);
    lv_obj_set_y(bk_ui->page_2_obj_2, 214);
    lv_obj_set_width(bk_ui->page_2_obj_2, 181);
    lv_obj_set_height(bk_ui->page_2_obj_2, 47);
    lv_obj_remove_flag(bk_ui->page_2_obj_2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(bk_ui->page_2_obj_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_obj_2, 28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_obj_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_obj_2, lv_color_hex(0xD9D9D9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_obj_2, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_obj_2, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_obj_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_obj_2, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_obj_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_obj_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_label_1 = lv_label_create(bk_ui->page_2_obj_2);
    lv_label_set_text(bk_ui->page_2_label_1, "TRIP");
    lv_label_set_long_mode(bk_ui->page_2_label_1, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_2_label_1, 10);
    lv_obj_set_y(bk_ui->page_2_label_1, 4);
    lv_obj_set_width(bk_ui->page_2_label_1, 60);
    lv_obj_set_height(bk_ui->page_2_label_1, 20);
    lv_obj_remove_flag(bk_ui->page_2_label_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_text_color(bk_ui->page_2_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_label_1, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_label_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_spangroup_1 = lv_spangroup_create(bk_ui->page_2_obj_2);
    lv_obj_set_x(bk_ui->page_2_spangroup_1, 11);
    lv_obj_set_y(bk_ui->page_2_spangroup_1, 19);
    lv_obj_set_width(bk_ui->page_2_spangroup_1, 60);
    lv_obj_set_height(bk_ui->page_2_spangroup_1, 20);
    lv_obj_remove_flag(bk_ui->page_2_spangroup_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    bk_ui->page_2_spangroup_1_span_0 = lv_spangroup_new_span(bk_ui->page_2_spangroup_1);
    lv_span_set_text(bk_ui->page_2_spangroup_1_span_0, "30");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_2_spangroup_1_span_0), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_2_spangroup_1_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_2_spangroup_1_span_0), &lv_font_montserrat_20);
    bk_ui->page_2_spangroup_1_span_1 = lv_spangroup_new_span(bk_ui->page_2_spangroup_1);
    lv_span_set_text(bk_ui->page_2_spangroup_1_span_1, " km");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_2_spangroup_1_span_1), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_2_spangroup_1_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_2_spangroup_1_span_1), &lv_font_montserrat_12);

    bk_ui->page_2_label_2 = lv_label_create(bk_ui->page_2_obj_2);
    lv_label_set_text(bk_ui->page_2_label_2, "ODO");
    lv_label_set_long_mode(bk_ui->page_2_label_2, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_2_label_2, 97);
    lv_obj_set_y(bk_ui->page_2_label_2, 4);
    lv_obj_set_width(bk_ui->page_2_label_2, 60);
    lv_obj_set_height(bk_ui->page_2_label_2, 20);
    lv_obj_remove_flag(bk_ui->page_2_label_2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_text_color(bk_ui->page_2_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_label_2, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_label_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_spangroup_2 = lv_spangroup_create(bk_ui->page_2_obj_2);
    lv_obj_set_x(bk_ui->page_2_spangroup_2, 97);
    lv_obj_set_y(bk_ui->page_2_spangroup_2, 19);
    lv_obj_set_width(bk_ui->page_2_spangroup_2, 69);
    lv_obj_set_height(bk_ui->page_2_spangroup_2, 20);
    lv_obj_remove_flag(bk_ui->page_2_spangroup_2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    bk_ui->page_2_spangroup_2_span_0 = lv_spangroup_new_span(bk_ui->page_2_spangroup_2);
    lv_span_set_text(bk_ui->page_2_spangroup_2_span_0, "125");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_2_spangroup_2_span_0), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_2_spangroup_2_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_2_spangroup_2_span_0), &lv_font_montserrat_20);
    bk_ui->page_2_spangroup_2_span_1 = lv_spangroup_new_span(bk_ui->page_2_spangroup_2);
    lv_span_set_text(bk_ui->page_2_spangroup_2_span_1, " km");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_2_spangroup_2_span_1), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_2_spangroup_2_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_2_spangroup_2_span_1), &lv_font_montserrat_12);
    mileage_timer = lv_timer_create(lv_page2_mileage_timer_cb, 8000, NULL);

    bk_ui->page_2_image_8 = lv_image_create(bk_ui->page_2);
    lv_image_set_rotation(bk_ui->page_2_image_8, 0);
    lv_obj_set_x(bk_ui->page_2_image_8, 10);
    lv_obj_set_y(bk_ui->page_2_image_8, 66);
    lv_obj_set_width(bk_ui->page_2_image_8, 265);
    lv_obj_set_height(bk_ui->page_2_image_8, 195);
    lv_obj_set_style_bg_color(bk_ui->page_2_image_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_image_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_image_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_image_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_image_8, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_image_8, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_2_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_2_image_8, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_2_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_obj_3 = lv_obj_create(bk_ui->page_2);
    lv_obj_set_x(bk_ui->page_2_obj_3, 288);
    lv_obj_set_y(bk_ui->page_2_obj_3, 68);
    lv_obj_set_width(bk_ui->page_2_obj_3, 181);
    lv_obj_set_height(bk_ui->page_2_obj_3, 139);
    lv_obj_remove_flag(bk_ui->page_2_obj_3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(bk_ui->page_2_obj_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_obj_3, 23, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_obj_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_obj_3, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_obj_3, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_obj_3, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_obj_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_obj_3, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_obj_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_obj_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_scale_1 = lv_scale_create(bk_ui->page_2_obj_3);
    lv_obj_update_layout(bk_ui->page_2_scale_1);
    lv_scale_set_mode(bk_ui->page_2_scale_1, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(bk_ui->page_2_scale_1, 41);
    lv_scale_set_major_tick_every(bk_ui->page_2_scale_1, 8);
    lv_scale_set_label_show(bk_ui->page_2_scale_1, false);
    lv_scale_set_range(bk_ui->page_2_scale_1, 0, 100);
    lv_scale_set_angle_range(bk_ui->page_2_scale_1, 260);
    lv_scale_set_rotation(bk_ui->page_2_scale_1, 140);
    lv_scale_set_post_draw(bk_ui->page_2_scale_1, true);
    lv_obj_set_x(bk_ui->page_2_scale_1, 23);
    lv_obj_set_y(bk_ui->page_2_scale_1, 4);
    lv_obj_set_width(bk_ui->page_2_scale_1, 156);
    lv_obj_set_height(bk_ui->page_2_scale_1, 133);
    lv_obj_remove_flag(bk_ui->page_2_scale_1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    bk_ui->page_2_scale_1_needle_0 = lv_line_create(bk_ui->page_2_scale_1);
    lv_obj_set_style_line_width(bk_ui->page_2_scale_1_needle_0, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(bk_ui->page_2_scale_1_needle_0, lv_color_hex(0x0933fe), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(bk_ui->page_2_scale_1_needle_0, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(bk_ui->page_2_scale_1, bk_ui->page_2_scale_1_needle_0, 65, 20);
    lv_scale_section_t * page_2_scale_1_section_0 = lv_scale_add_section(bk_ui->page_2_scale_1);
    static lv_style_t page_2_scale_1_section_0_minor_tick_style;
    static lv_style_t page_2_scale_1_section_0_label_style;
    static lv_style_t page_2_scale_1_section_0_main_line_style;
    lv_style_init(&page_2_scale_1_section_0_label_style);
    lv_style_init(&page_2_scale_1_section_0_minor_tick_style);
    lv_style_init(&page_2_scale_1_section_0_main_line_style);
    lv_style_set_text_color(&page_2_scale_1_section_0_label_style, lv_color_hex(0xffff00));
    lv_style_set_line_color(&page_2_scale_1_section_0_label_style, lv_color_hex(0x08cffb));
    lv_style_set_line_color(&page_2_scale_1_section_0_minor_tick_style, lv_color_hex(0x08cffb));
    lv_style_set_line_width(&page_2_scale_1_section_0_minor_tick_style, 9);
    lv_style_set_arc_color(&page_2_scale_1_section_0_main_line_style, lv_color_hex(0x00ff00));
    lv_style_set_arc_width(&page_2_scale_1_section_0_main_line_style, 8);
    lv_scale_section_set_range(page_2_scale_1_section_0, 0, 40);
    lv_scale_section_set_style(page_2_scale_1_section_0, LV_PART_INDICATOR, &page_2_scale_1_section_0_label_style);
    lv_scale_section_set_style(page_2_scale_1_section_0, LV_PART_MAIN, &page_2_scale_1_section_0_main_line_style);
    lv_scale_section_set_style(page_2_scale_1_section_0, LV_PART_ITEMS, &page_2_scale_1_section_0_minor_tick_style);
    lv_obj_set_style_radius(bk_ui->page_2_scale_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->page_2_scale_1, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->page_2_scale_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->page_2_scale_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->page_2_scale_1, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_label_3 = lv_label_create(bk_ui->page_2_obj_3);
    lv_label_set_text(bk_ui->page_2_label_3, "40");
    lv_label_set_long_mode(bk_ui->page_2_label_3, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_2_label_3, 52);
    lv_obj_set_y(bk_ui->page_2_label_3, 29);
    lv_obj_set_width(bk_ui->page_2_label_3, 68);
    lv_obj_set_height(bk_ui->page_2_label_3, 51);
    lv_obj_remove_flag(bk_ui->page_2_label_3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_text_color(bk_ui->page_2_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_label_3, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_label_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    speed_timer = lv_timer_create(lv_page2_speed_timer_cb, 150, NULL);

    bk_ui->page_2_label_4 = lv_label_create(bk_ui->page_2_obj_3);
    lv_label_set_text(bk_ui->page_2_label_4, "km/h");
    lv_label_set_long_mode(bk_ui->page_2_label_4, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_2_label_4, 58);
    lv_obj_set_y(bk_ui->page_2_label_4, 76);
    lv_obj_set_width(bk_ui->page_2_label_4, 60);
    lv_obj_set_height(bk_ui->page_2_label_4, 20);
    lv_obj_remove_flag(bk_ui->page_2_label_4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_text_color(bk_ui->page_2_label_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_label_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_label_4, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_label_4, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_label_5 = lv_label_create(bk_ui->page_2_obj_3);
    lv_label_set_text(bk_ui->page_2_label_5, "SPORT");
    lv_label_set_long_mode(bk_ui->page_2_label_5, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_2_label_5, 59);
    lv_obj_set_y(bk_ui->page_2_label_5, 112);
    lv_obj_set_width(bk_ui->page_2_label_5, 60);
    lv_obj_set_height(bk_ui->page_2_label_5, 17);
    lv_obj_remove_flag(bk_ui->page_2_label_5, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_text_color(bk_ui->page_2_label_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_label_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_label_5, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_label_5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);


    lv_obj_update_layout(bk_ui->page_2);
}

/*
 * @brief: destroy page page_2
 */
void destroy_page_page_2(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (mileage_timer != NULL) {
        lv_timer_del(mileage_timer);
        mileage_timer = NULL;
    }

    if (turn_signal_timer != NULL) {
        lv_timer_del(turn_signal_timer);
        turn_signal_timer = NULL;
    }

    if (digital_clock_timer != NULL) {
        lv_timer_del(digital_clock_timer);
        digital_clock_timer = NULL;
    }

    if (speed_timer != NULL) {
        lv_timer_del(speed_timer);
        speed_timer = NULL;
    }

    if (bk_ui->page_2 != NULL) {
        lv_obj_del(bk_ui->page_2);
        bk_ui->page_2 = NULL;
    }
}
