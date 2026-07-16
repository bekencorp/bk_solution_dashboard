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
    lv_obj_t *home;
    lv_obj_t *home_bg_img;
    lv_obj_t *home_top_bar;
    lv_obj_t *home_dash_title;
    lv_obj_t *home_time_clk;
    lv_obj_t *home_icons_panel;
    lv_obj_t *home_ic_abs;
    lv_obj_t *home_ic_tcs;
    lv_obj_t *home_ic_gps;
    lv_obj_t *home_ic_bt;
    lv_obj_t *home_ic_light;
    lv_obj_t *home_ic_eng;
    lv_obj_t *home_ic_warn;
    lv_obj_t *home_ic_batt;
    lv_obj_t *home_ic_tire;
    lv_obj_t *home_ic_temp;
    lv_obj_t *home_speed_panel;
    lv_obj_t *home_spd_halo_3;
    lv_obj_t *home_spd_halo_2;
    lv_obj_t *home_speed_scale;
    lv_obj_t *home_speed_scale_needle_0;
    lv_obj_t *home_speed_val;
    lv_obj_t *home_speed_unit;
    lv_obj_t *home_mode_txt;
    lv_obj_t *home_speed_title;
    lv_obj_t *home_left_panel;
    lv_obj_t *home_left_title;
    lv_obj_t *home_battery_lbl;
    lv_obj_t *home_range_big;
    lv_obj_t *home_trip_mode;
    lv_obj_t *home_assist_bar;
    lv_obj_t *home_assist_lbl;
    lv_obj_t *home_batt_seg_panel;
    lv_obj_t *home_batt_seg01;
    lv_obj_t *home_batt_seg02;
    lv_obj_t *home_batt_seg03;
    lv_obj_t *home_batt_seg04;
    lv_obj_t *home_batt_seg05;
    lv_obj_t *home_batt_seg06;
    lv_obj_t *home_batt_seg07;
    lv_obj_t *home_batt_seg08;
    lv_obj_t *home_batt_seg09;
    lv_obj_t *home_batt_seg10;
    lv_obj_t *home_right_panel;
    lv_obj_t *home_sys_halo_3;
    lv_obj_t *home_sys_halo_2;
    lv_obj_t *home_sys_scale;
    lv_obj_t *home_sys_scale_needle_0;
    lv_obj_t *home_right_title;
    lv_obj_t *home_front_psi;
    lv_obj_t *home_rear_psi;
    lv_obj_t *home_volt_txt;
    lv_obj_t *home_temp_txt;
    lv_obj_t *home_brake_txt;
    lv_obj_t *home_sys_unit;
    lv_obj_t *home_ic_left;
    lv_obj_t *home_ic_right;
    lv_obj_t *home_bottom_bar;
    lv_obj_t *home_left_info;
    lv_obj_t *home_odo_txt;
    lv_obj_t *home_trip_txt;
    lv_obj_t *home_left_title_lbl;
    lv_obj_t *home_odo_lbl;
    lv_obj_t *home_trip_lbl;
    lv_obj_t *home_music_panel;
    lv_obj_t *home_beat_panel;
    lv_obj_t *home_beat01;
    lv_obj_t *home_beat02;
    lv_obj_t *home_beat03;
    lv_obj_t *home_beat04;
    lv_obj_t *home_beat05;
    lv_obj_t *home_beat06;
    lv_obj_t *home_beat07;
    lv_obj_t *home_beat08;
    lv_obj_t *home_beat09;
    lv_obj_t *home_beat10;
    lv_obj_t *home_beat11;
    lv_obj_t *home_beat12;
    lv_obj_t *home_beat13;
    lv_obj_t *home_beat14;
    lv_obj_t *home_beat15;
    lv_obj_t *home_beat16;
    lv_obj_t *home_beat17;
    lv_obj_t *home_beat18;
    lv_obj_t *home_beat19;
    lv_obj_t *home_beat20;
    lv_obj_t *home_music_tag;
    lv_obj_t *home_song_title;
    lv_obj_t *home_song_artist;
    lv_obj_t *home_music_prog;
    lv_obj_t *home_right_info;
    lv_obj_t *home_avg_txt;
    lv_obj_t *home_right_title_l;
    lv_obj_t *home_ride_time;
    lv_obj_t *home_temp_info;
    lv_obj_t *home_avg_lbl;
    lv_obj_t *home_ride_lbl;
    lv_obj_t *home_gps_lbl;
    lv_obj_t *home_nav_panel;
    lv_obj_t *home_dash_entry;
    lv_obj_t *home_dash_ic;
    lv_obj_t *home_dash_lbl;
    lv_obj_t *home_ota_entry;
    lv_obj_t *home_ota_ic;
    lv_obj_t *home_ota_lbl;
    /* Page: 1 objects */
    lv_obj_t *nav_cast;
    lv_obj_t *nav_cast_bg_img;
    lv_obj_t *nav_cast_top_bar;
    lv_obj_t *nav_cast_dash_title;
    lv_obj_t *nav_cast_time_clk;
    lv_obj_t *nav_cast_icons_panel;
    lv_obj_t *nav_cast_ic_left;
    lv_obj_t *nav_cast_ic_right;
    lv_obj_t *nav_cast_ic_abs;
    lv_obj_t *nav_cast_ic_tcs;
    lv_obj_t *nav_cast_ic_gps;
    lv_obj_t *nav_cast_ic_bt;
    lv_obj_t *nav_cast_ic_light;
    lv_obj_t *nav_cast_ic_eng;
    lv_obj_t *nav_cast_ic_warn;
    lv_obj_t *nav_cast_ic_batt;
    lv_obj_t *nav_cast_ic_tire;
    lv_obj_t *nav_cast_ic_temp;
    lv_obj_t *nav_cast_map_panel;
    lv_obj_t *nav_cast_map_hint;
    lv_obj_t *nav_cast_side_panel;
    lv_obj_t *nav_cast_eta_card;
    lv_obj_t *nav_cast_eta_title;
    lv_obj_t *nav_cast_eta_value;
    lv_obj_t *nav_cast_nav_state;
    lv_obj_t *nav_cast_sys_card;
    lv_obj_t *nav_cast_sys_title;
    lv_obj_t *nav_cast_volt_lbl;
    lv_obj_t *nav_cast_tire_lbl;
    lv_obj_t *nav_cast_temp_lbl;
    lv_obj_t *nav_cast_brake_lbl;
    lv_obj_t *nav_cast_stat_card;
    lv_obj_t *nav_cast_stat_title;
    lv_obj_t *nav_cast_avg_lbl;
    lv_obj_t *nav_cast_ride_lbl;
    lv_obj_t *nav_cast_gps_lbl;
    lv_obj_t *nav_cast_left_panel;
    lv_obj_t *nav_cast_speed_card;
    lv_obj_t *nav_cast_spd_title;
    lv_obj_t *nav_cast_speed_span;
    lv_span_t *nav_cast_speed_span_span_0;
    lv_span_t *nav_cast_speed_span_span_1;
    lv_obj_t *nav_cast_mode_lbl;
    lv_obj_t *nav_cast_mile_card;
    lv_obj_t *nav_cast_mile_title;
    lv_obj_t *nav_cast_mile_value;
    lv_obj_t *nav_cast_mile_hint;
    lv_obj_t *nav_cast_batt_card;
    lv_obj_t *nav_cast_batt_title;
    lv_obj_t *nav_cast_batt_seg01;
    lv_obj_t *nav_cast_batt_seg02;
    lv_obj_t *nav_cast_batt_seg03;
    lv_obj_t *nav_cast_batt_seg04;
    lv_obj_t *nav_cast_batt_seg05;
    lv_obj_t *nav_cast_batt_seg06;
    lv_obj_t *nav_cast_batt_seg07;
    lv_obj_t *nav_cast_batt_seg08;
    lv_obj_t *nav_cast_batt_seg09;
    lv_obj_t *nav_cast_batt_seg10;
    lv_obj_t *nav_cast_assist_lbl;
    lv_obj_t *nav_cast_odo_card;
    lv_obj_t *nav_cast_odo_title;
    lv_obj_t *nav_cast_odo_value;
    lv_obj_t *nav_cast_trip_value;
    /* Page: 2 objects */
    lv_obj_t *dashcam;
    lv_obj_t *dashcam_bg_img;
    lv_obj_t *dashcam_bottom_bar;
    lv_obj_t *dashcam_ctrl_info_row;
    lv_obj_t *dashcam_dur_start;
    lv_obj_t *dashcam_dur_end;
    lv_obj_t *dashcam_time_bar;
    lv_obj_t *dashcam_front_vid_panel;
    lv_obj_t *dashcam_info_bar;
    lv_obj_t *dashcam_sky_area;
    lv_obj_t *dashcam_front_tag;
    lv_obj_t *dashcam_ts_overlay;
    lv_obj_t *dashcam_spd_overlay;
    lv_obj_t *dashcam_file_name;
    lv_obj_t *dashcam_Live;
    lv_obj_t *dashcam_rec_list_panel;
    lv_obj_t *dashcam_list_title;
    lv_obj_t *dashcam_rec_list;
    lv_obj_t *dashcam_rec_list_item_0;
    lv_obj_t *dashcam_rec_list_item_1;
    lv_obj_t *dashcam_rec_list_item_2;
    lv_obj_t *dashcam_rec_list_item_3;
    lv_obj_t *dashcam_rec_list_item_4;
    lv_obj_t *dashcam_rec_list_item_5;
    lv_obj_t *dashcam_rec_list_item_6;
    lv_obj_t *dashcam_rec_list_item_7;
    lv_obj_t *dashcam_rec_list_item_8;
    lv_obj_t *dashcam_rec_list_item_9;
    /* Page: 3 objects */
    lv_obj_t *ota_update;
    lv_obj_t *ota_update_bg_img;
    lv_obj_t *ota_update_top_bar;
    lv_obj_t *ota_update_ota_title;
    lv_obj_t *ota_update_back_btn;
    lv_obj_t *ota_update_back_btn_label;
    lv_obj_t *ota_update_icons_panel;
    lv_obj_t *ota_update_ic_abs;
    lv_obj_t *ota_update_ic_tcs;
    lv_obj_t *ota_update_ic_gps;
    lv_obj_t *ota_update_ic_bt;
    lv_obj_t *ota_update_ic_light;
    lv_obj_t *ota_update_ic_eng;
    lv_obj_t *ota_update_ic_warn;
    lv_obj_t *ota_update_ic_batt;
    lv_obj_t *ota_update_ic_tire;
    lv_obj_t *ota_update_ic_temp;
    lv_obj_t *ota_update_time_clk;
    lv_obj_t *ota_update_gauge_panel;
    lv_obj_t *ota_update_prog_ring;
    lv_obj_t *ota_update_pct_num;
    lv_obj_t *ota_update_pct_sym;
    lv_obj_t *ota_update_stat_lbl;
    lv_obj_t *ota_update_eta_lbl;
} bk_lv_ui_t;

void init_page_home(bk_lv_ui_t *bk_ui);
void destroy_page_home(bk_lv_ui_t *bk_ui);
void init_page_nav_cast(bk_lv_ui_t *bk_ui);
void destroy_page_nav_cast(bk_lv_ui_t *bk_ui);
void init_page_dashcam(bk_lv_ui_t *bk_ui);
void destroy_page_dashcam(bk_lv_ui_t *bk_ui);
void init_page_ota_update(bk_lv_ui_t *bk_ui);
void destroy_page_ota_update(bk_lv_ui_t *bk_ui);
void beken_ui_nav_to_dashcam(void);
void beken_ui_nav_to_ota_update(void);

/*
 * Wrap the background bitmap preloaded during the boot animation
 * (boot_bg_preload) onto a page's background image object. Shared by the page
 * modules / navigation so each page can reuse the single decoded bitmap.
 * Returns true if the preloaded bitmap was installed.
 */
bool beken_ui_install_preloaded_bg(lv_obj_t *bg_img);

/* declare image */
LV_IMAGE_DECLARE(dash_bg_clean_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(entry_dashcam_status_copy_38x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(entry_ota_status_copy_38x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(home_bg_tech_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_grey_80x77_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_left_80x77_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_bg_tech_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_abs_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_back_status_copy_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_batt_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_bt_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_eng_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_gps_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_light_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_tcs_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_temp_copy_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_tire_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_turn_l_copy2_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_turn_r_copy2_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_ic_warn_33x31_RGB565A8_NONE);
LV_IMAGE_DECLARE(ota_bg_pure_black_copy_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(play_btn_trans_ARGB8888);

/* declare fonts */
LV_FONT_DECLARE(lv_font_pingfang_SC_18);
LV_FONT_DECLARE(lv_font_montserrat_regular_22);
LV_FONT_DECLARE(lv_font_montserrat_regular_14);
LV_FONT_DECLARE(lv_font_pingfang_SC_90);
LV_FONT_DECLARE(lv_font_pingfang_SC_24);
LV_FONT_DECLARE(lv_font_pingfang_SC_14);
LV_FONT_DECLARE(lv_font_montserrat_regular_18);
LV_FONT_DECLARE(lv_font_pingfang_SC_13);
LV_FONT_DECLARE(lv_font_pingfang_SC_16);
LV_FONT_DECLARE(lv_font_pingfang_SC_15);
LV_FONT_DECLARE(lv_font_montserrat_regular_12);
LV_FONT_DECLARE(lv_font_pingfang_SC_72);
LV_FONT_DECLARE(lv_font_pingfang_SC_22);
LV_FONT_DECLARE(lv_font_pingfang_SC_20);
LV_FONT_DECLARE(lv_font_montserrat_regular_16);
LV_FONT_DECLARE(lv_font_montserrat_regular_20);
LV_FONT_DECLARE(lv_font_pingfang_SC_30);
LV_FONT_DECLARE(lv_font_pingfang_SC_12);
LV_FONT_DECLARE(lv_font_pingfang_SC_28);
LV_FONT_DECLARE(lv_font_pingfang_SC_11);
LV_FONT_DECLARE(lv_font_pingfang_SC_80);

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
