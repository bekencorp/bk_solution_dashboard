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

/* Digital clock global timer and instance management */
#define MAX_DIGITAL_CLOCKS 32

typedef struct {
    lv_obj_t *label;      /* label object pointer */
    int show_second;      /* show second */
    int use_ampm;         /* use ampm */
} lv_digital_clock_inst_t;

static lv_timer_t *g_digital_clock_timer = NULL;
static lv_digital_clock_inst_t g_digital_clock_instances[MAX_DIGITAL_CLOCKS];
static int g_digital_clock_count = 0;
static int g_global_hour = 0;
static int g_global_minute = 0;
static int g_global_second = 0;
static char g_global_meridiem[4] = "AM";
static int g_timer_initialized = 0;
static bool lv_turn_signal_is_green = true;
static bool lv_speed_is_increase = true;


/**
 * @brief format time string
 * @param buf output buffer
 * @param hour hour
 * @param minute minute
 * @param second second (if show_second is 0, it will be ignored)
 * @param use_ampm use ampm
 * @param show_second show second
 */
static void format_time_string(char *buf, int hour, int minute, int second, int use_ampm, int show_second)
{
    int display_hour = hour;
    const char *meridiem = "";
    
    if (use_ampm) {
        if (hour == 0) {
            display_hour = 12;
            meridiem = "AM";
        } else if (hour < 12) {
            display_hour = hour;
            meridiem = "AM";
        } else if (hour == 12) {
            display_hour = 12;
            meridiem = "PM";
        } else {
            display_hour = hour - 12;
            meridiem = "PM";
        }
        
        if (show_second) {
            lv_snprintf(buf, 32, "%02d:%02d:%02d %s", display_hour, minute, second, meridiem);
        } else {
            lv_snprintf(buf, 32, "%02d:%02d %s", display_hour, minute, meridiem);
        }
    } else {
        if (show_second) {
            lv_snprintf(buf, 32, "%02d:%02d:%02d", hour, minute, second);
        } else {
            lv_snprintf(buf, 32, "%02d:%02d", hour, minute);
        }
    }
}

/**
* @brief global digital clock timer callback
 * @param timer LVGL timer object
 */
void lv_digital_clock_timer(lv_timer_t *timer)
{
    int i;
    char time_str[32];
    
    g_global_second++;
    if (g_global_second >= 60) {
        g_global_second = 0;
        g_global_minute++;
    }
    if (g_global_minute >= 60) {
        g_global_minute = 0;
        g_global_hour++;
    }
    if (g_global_hour >= 24) {
        g_global_hour = 0;
    }
    
    if (g_global_hour == 0) {
        lv_strcpy(g_global_meridiem, "AM");
    } else if (g_global_hour == 12) {
        lv_strcpy(g_global_meridiem, "PM");
    }
    
    i = 0;
    while (i < g_digital_clock_count) {
        if (g_digital_clock_instances[i].label != NULL && 
            lv_obj_is_valid(g_digital_clock_instances[i].label) &&
            lv_obj_check_type(g_digital_clock_instances[i].label, &lv_label_class)) {
            format_time_string(time_str, g_global_hour, g_global_minute, g_global_second,
                             g_digital_clock_instances[i].use_ampm,
                             g_digital_clock_instances[i].show_second);
            lv_label_set_text(g_digital_clock_instances[i].label, time_str);
            i++;
        } else {
            int j;
            for (j = i; j < g_digital_clock_count - 1; j++) {
                g_digital_clock_instances[j] = g_digital_clock_instances[j + 1];
            }
            g_digital_clock_count--;
        }
    }
}

/**
 * @brief register digital_clock instance (called when create)
 * @param label label object pointer
 * @param show_second show second
 * @param use_ampm use ampm
 * @param hour initial hour (optional, -1 means not specified, only the first instance is valid)
 * @param minute initial minute (optional, -1 means not specified, only the first instance is valid)
 * @param second initial second (optional, -1 means not specified, only the first instance is valid)
 */
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second)
{
    if (g_digital_clock_count >= MAX_DIGITAL_CLOCKS) {
        return;
    }
    
    if (g_timer_initialized == 0) {
        if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60) {
            g_global_hour = hour;
            g_global_minute = minute;
            g_global_second = second;
        } else {
            g_global_hour = 0;
            g_global_minute = 0;
            g_global_second = 0;
        }
        
        if (use_ampm) {
            if (g_global_hour == 0) {
                lv_strcpy(g_global_meridiem, "AM");
            } else if (g_global_hour < 12) {
                lv_strcpy(g_global_meridiem, "AM");
            } else {
                lv_strcpy(g_global_meridiem, "PM");
            }
        }
        
        if (g_digital_clock_timer == NULL) {
            g_digital_clock_timer = lv_timer_create(lv_digital_clock_timer, 1000, NULL);
        }
        g_timer_initialized = 1;
    }

    g_digital_clock_instances[g_digital_clock_count].label = label;
    g_digital_clock_instances[g_digital_clock_count].show_second = show_second;
    g_digital_clock_instances[g_digital_clock_count].use_ampm = use_ampm;
    g_digital_clock_count++;
}

/**
 * @brief unregister digital_clock instance (called when destroy)
 * @param label label object pointer
 */
void lv_digital_clock_unregister(lv_obj_t *label)
{
    int i, j;
    for (i = 0; i < g_digital_clock_count; i++) {
        if (g_digital_clock_instances[i].label == label) {
            for (j = i; j < g_digital_clock_count - 1; j++) {
                g_digital_clock_instances[j] = g_digital_clock_instances[j + 1];
            }
            g_digital_clock_count--;
            break;
        }
    }
}

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
