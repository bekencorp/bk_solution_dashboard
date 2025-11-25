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
#include <math.h>

static bool lv_turn_signal_is_green = true;
static bool lv_speed_is_increase = true;

void lv_mileage_timer_cb(lv_timer_t *timer)
{
    static float trip_value = 18.8;
    static uint32_t mileage_value = 300;
    char trip[6];
    char mileage[6];

    trip_value += 0.1;
    sprintf(trip, "%.1f", trip_value);
    lv_spangroup_set_span_text(bk_lv_tool_ui.spangroup_1, bk_lv_tool_ui.spangroup_1_span_0, trip);

    if (fmod(trip_value, 1.0) < 0.1) {
        mileage_value++;
        sprintf(mileage, "%d", (int)mileage_value);
        lv_spangroup_set_span_text(bk_lv_tool_ui.spangroup_2, bk_lv_tool_ui.spangroup_2_span_0, mileage);
    }

    if (trip_value > 299.9) {
        trip_value = 0.0;
    }

    if (mileage_value > 99999) {
        mileage_value = 0;
    }
}

void lv_digital_clock_timer_cb(lv_timer_t *timer)
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

void lv_speed_timer_cb(lv_timer_t *timer)
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
            lv_timer_set_period(timer, 99);
        }

        if (value > 60 && value <= 80) {
            value += 2;
            lv_timer_set_period(timer, 150);
        }

        if (value > 80 && value < 99) {
            value += 1;
            lv_timer_set_period(timer, 200);
        }
    } else {
        value -= 4;
        if(value < 0) {
            value = 0;
        }
        lv_timer_set_period(timer, 180);
    }

    sprintf(speed, "%d", value);
    lv_label_set_text(bk_lv_tool_ui.label_7, speed);
    lv_scale_set_line_needle_value(bk_lv_tool_ui.scale_1, bk_lv_tool_ui.scale_1_needle_0, 91, value);
}

void lv_turn_signal_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *signal_obj = (lv_obj_t *)lv_timer_get_user_data(timer);

    if (lv_turn_signal_is_green) {
        lv_image_set_src(signal_obj, &light_grey_70x74_RGB565A8_NONE);
        lv_turn_signal_is_green = false;
    } else {
        lv_image_set_src(signal_obj, &light_green_70x74_RGB565A8_NONE);
        lv_turn_signal_is_green = true;
    }
}
