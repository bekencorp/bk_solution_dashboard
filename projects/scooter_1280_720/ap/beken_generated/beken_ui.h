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
    lv_obj_t *home_phone_entry;
    lv_obj_t *home_phone_ic;
    lv_obj_t *home_phone_lbl;
    lv_obj_t *home_music_entry;
    lv_obj_t *home_nav_music_ic;
    lv_obj_t *home_nav_music_lbl;
    lv_obj_t *home_np_panel;
    lv_obj_t *home_music_ic;
    lv_obj_t *home_song_title;
    lv_obj_t *home_song_artist;
    lv_obj_t *home_call_popup;
    lv_obj_t *home_cp_phone;
    lv_obj_t *home_cp_title;
    lv_obj_t *home_cp_num;
    lv_obj_t *home_cp_answer;
    lv_obj_t *home_cp_answer_label;
    lv_obj_t *home_cp_hangup;
    lv_obj_t *home_cp_hangup_label;
    lv_obj_t *home_oncall_card;
    lv_obj_t *home_oc_phone;
    lv_obj_t *home_oc_title;
    lv_obj_t *home_oc_timer;
    lv_obj_t *home_oc_hangup;
    lv_obj_t *home_oc_hangup_label;
    lv_obj_t *home_outcall_popup;
    lv_obj_t *home_op_phone;
    lv_obj_t *home_op_title;
    lv_obj_t *home_op_num;
    lv_obj_t *home_op_hangup;
    lv_obj_t *home_op_hangup_label;
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
    /* Page: 4 objects */
    lv_obj_t *phone_book;
    lv_obj_t *phone_book_status_lbl;
    lv_obj_t *phone_book_batt_lbl;
    lv_obj_t *phone_book_hdiv;
    lv_obj_t *phone_book_vdiv;
    lv_obj_t *phone_book_c_title;
    lv_obj_t *phone_book_r_title;
    lv_obj_t *phone_book_batt_body;
    lv_obj_t *phone_book_batt_fill;
    lv_obj_t *phone_book_batt_cap;
    lv_obj_t *phone_book_contacts_list;
    lv_obj_t *phone_book_c_row1;
    lv_obj_t *phone_book_c_iav1;
    lv_obj_t *phone_book_c_nm1;
    lv_obj_t *phone_book_c_no1;
    lv_obj_t *phone_book_c_call1;
    lv_obj_t *phone_book_c_row2;
    lv_obj_t *phone_book_c_iav2;
    lv_obj_t *phone_book_c_nm2;
    lv_obj_t *phone_book_c_no2;
    lv_obj_t *phone_book_c_call2;
    lv_obj_t *phone_book_c_row3;
    lv_obj_t *phone_book_c_iav3;
    lv_obj_t *phone_book_c_nm3;
    lv_obj_t *phone_book_c_no3;
    lv_obj_t *phone_book_c_call3;
    lv_obj_t *phone_book_c_row4;
    lv_obj_t *phone_book_c_iav4;
    lv_obj_t *phone_book_c_nm4;
    lv_obj_t *phone_book_c_no4;
    lv_obj_t *phone_book_c_call4;
    lv_obj_t *phone_book_c_row5;
    lv_obj_t *phone_book_c_iav5;
    lv_obj_t *phone_book_c_nm5;
    lv_obj_t *phone_book_c_no5;
    lv_obj_t *phone_book_c_call5;
    lv_obj_t *phone_book_c_row6;
    lv_obj_t *phone_book_c_iav6;
    lv_obj_t *phone_book_c_nm6;
    lv_obj_t *phone_book_c_no6;
    lv_obj_t *phone_book_c_call6;
    lv_obj_t *phone_book_recents_list;
    lv_obj_t *phone_book_r_row1;
    lv_obj_t *phone_book_r_ar1;
    lv_obj_t *phone_book_r_nm1;
    lv_obj_t *phone_book_r_tp1;
    lv_obj_t *phone_book_r_call1;
    lv_obj_t *phone_book_r_tm1;
    lv_obj_t *phone_book_r_row2;
    lv_obj_t *phone_book_r_ar2;
    lv_obj_t *phone_book_r_nm2;
    lv_obj_t *phone_book_r_tp2;
    lv_obj_t *phone_book_r_call2;
    lv_obj_t *phone_book_r_tm2;
    lv_obj_t *phone_book_r_row3;
    lv_obj_t *phone_book_r_ar3;
    lv_obj_t *phone_book_r_nm3;
    lv_obj_t *phone_book_r_tp3;
    lv_obj_t *phone_book_r_call3;
    lv_obj_t *phone_book_r_tm3;
    lv_obj_t *phone_book_r_row4;
    lv_obj_t *phone_book_r_ar4;
    lv_obj_t *phone_book_r_nm4;
    lv_obj_t *phone_book_r_tp4;
    lv_obj_t *phone_book_r_call4;
    lv_obj_t *phone_book_r_tm4;
    lv_obj_t *phone_book_r_row5;
    lv_obj_t *phone_book_r_ar5;
    lv_obj_t *phone_book_r_nm5;
    lv_obj_t *phone_book_r_tp5;
    lv_obj_t *phone_book_r_call5;
    lv_obj_t *phone_book_r_tm5;
    lv_obj_t *phone_book_r_row6;
    lv_obj_t *phone_book_r_ar6;
    lv_obj_t *phone_book_r_nm6;
    lv_obj_t *phone_book_r_tp6;
    lv_obj_t *phone_book_r_call6;
    lv_obj_t *phone_book_r_tm6;
    lv_obj_t *phone_book_bt_ic;
    /* Page: 5 objects */
    lv_obj_t *music_player;
    lv_obj_t *music_player_bg_img;
    lv_obj_t *music_player_page_title;
    lv_obj_t *music_player_np_panel;
    lv_obj_t *music_player_np_section;
    lv_obj_t *music_player_album_art;
    lv_obj_t *music_player_album_note;
    lv_obj_t *music_player_song_title;
    lv_obj_t *music_player_song_artist;
    lv_obj_t *music_player_progress;
    lv_obj_t *music_player_cur_time;
    lv_obj_t *music_player_total_time;
    lv_obj_t *music_player_btn_prev;
    lv_obj_t *music_player_btn_play;
    lv_obj_t *music_player_btn_next;
    lv_obj_t *music_player_vol_lbl;
    lv_obj_t *music_player_vol_slider;
    lv_obj_t *music_player_btn_mode;
    lv_obj_t *music_player_btn_vol_down;
    lv_obj_t *music_player_btn_vol_up;
    lv_obj_t *music_player_pl_panel;
    lv_obj_t *music_player_pl_title;
    lv_obj_t *music_player_pl_count;
    lv_obj_t *music_player_pl_list;
    lv_obj_t *music_player_row1;
    lv_obj_t *music_player_r1_acc;
    lv_obj_t *music_player_r1_idx;
    lv_obj_t *music_player_r1_title;
    lv_obj_t *music_player_r1_artist;
    lv_obj_t *music_player_r1_dur;
    lv_obj_t *music_player_row2;
    lv_obj_t *music_player_r2_idx;
    lv_obj_t *music_player_r2_title;
    lv_obj_t *music_player_r2_artist;
    lv_obj_t *music_player_r2_dur;
    lv_obj_t *music_player_row3;
    lv_obj_t *music_player_r3_idx;
    lv_obj_t *music_player_r3_title;
    lv_obj_t *music_player_r3_artist;
    lv_obj_t *music_player_r3_dur;
    lv_obj_t *music_player_row4;
    lv_obj_t *music_player_r4_idx;
    lv_obj_t *music_player_r4_title;
    lv_obj_t *music_player_r4_artist;
    lv_obj_t *music_player_r4_dur;
    lv_obj_t *music_player_row5;
    lv_obj_t *music_player_r5_idx;
    lv_obj_t *music_player_r5_title;
    lv_obj_t *music_player_r5_artist;
    lv_obj_t *music_player_r5_dur;
    lv_obj_t *music_player_row6;
    lv_obj_t *music_player_r6_idx;
    lv_obj_t *music_player_r6_title;
    lv_obj_t *music_player_r6_artist;
    lv_obj_t *music_player_r6_dur;
    lv_obj_t *music_player_row7;
    lv_obj_t *music_player_r7_idx;
    lv_obj_t *music_player_r7_title;
    lv_obj_t *music_player_r7_artist;
    lv_obj_t *music_player_r7_dur;
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
void home_menu_key_short_press(void);
void home_menu_key_double_press(void);
void home_menu_key_long_press(void);
void beken_ui_nav_to_phone_book(void);
void beken_ui_nav_to_music_player(void);
void beken_ui_nav_home(void);
void init_page_phone_book(bk_lv_ui_t *bk_ui);
void destroy_page_phone_book(bk_lv_ui_t *bk_ui);
void init_page_music_player(bk_lv_ui_t *bk_ui);
void destroy_page_music_player(bk_lv_ui_t *bk_ui);
/*
 * Wrap the background bitmap preloaded during the boot animation
 * (boot_bg_preload) onto a page's background image object. Shared by the page
 * modules / navigation so each page can reuse the single decoded bitmap.
 * Returns true if the preloaded bitmap was installed.
 */
bool beken_ui_install_preloaded_bg(lv_obj_t *bg_img);

/* declare image */
LV_IMAGE_DECLARE(arrow_in_sm_30x30_RGB565A8_NONE);
LV_IMAGE_DECLARE(arrow_miss_sm_30x30_RGB565A8_NONE);
LV_IMAGE_DECLARE(arrow_out_sm_30x30_RGB565A8_NONE);
LV_IMAGE_DECLARE(avatar_sm_52x52_RGB565A8_NONE);
LV_IMAGE_DECLARE(bt_gray_sm_28x28_RGB565A8_NONE);
LV_IMAGE_DECLARE(bt_sm_28x28_RGB565A8_NONE);
LV_IMAGE_DECLARE(btn_vol_down_44x44_RGB565A8_NONE);
LV_IMAGE_DECLARE(btn_vol_up_44x44_RGB565A8_NONE);
LV_IMAGE_DECLARE(contacts_ic2_tp_38x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(dash_bg_clean_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(entry_dashcam_status_copy_38x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(entry_ota_status_copy_38x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(home_bg_tech_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(incall_sm_44x44_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_grey_80x77_RGB565A8_NONE);
LV_IMAGE_DECLARE(light_left_80x77_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_line_sm_38x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_next_sm_56x56_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_note_tp_110x74_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_note_tp_139x76_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_play_sm_72x72_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_prev_sm_56x56_RGB565A8_NONE);
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
LV_IMAGE_DECLARE(oncall_sm_44x44_RGB565A8_NONE);
LV_IMAGE_DECLARE(ota_bg_pure_black_copy_1280x720_RGB565A8_NONE);
LV_IMAGE_DECLARE(outcall_sm_44x44_RGB565A8_NONE);
LV_IMAGE_DECLARE(play_btn_trans_ARGB8888);
LV_IMAGE_DECLARE(repeat_sm_52x52_RGB565A8_NONE);
LV_IMAGE_DECLARE(repeat_one_sm_52x52_RGB565A8_NONE);
LV_IMAGE_DECLARE(shuffle_sm_52x52_RGB565A8_NONE);
LV_IMAGE_DECLARE(music_pause_sm_72x72_RGB565A8_NONE);

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
LV_FONT_DECLARE(lv_font_montserrat_regular_24);
LV_FONT_DECLARE(lv_font_montserrat_regular_30);
LV_FONT_DECLARE(lv_font_montserrat_regular_20);
LV_FONT_DECLARE(lv_font_pingfang_SC_30);
LV_FONT_DECLARE(lv_font_pingfang_SC_12);
LV_FONT_DECLARE(lv_font_pingfang_SC_28);
LV_FONT_DECLARE(lv_font_pingfang_SC_11);
LV_FONT_DECLARE(lv_font_pingfang_SC_80);
LV_FONT_DECLARE(lv_font_montserrat_regular_32);
LV_FONT_DECLARE(lv_font_pingfang_SC_26);

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
