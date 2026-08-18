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
    lv_obj_t *home_bg_panel;
    lv_obj_t *home_logo_eye;
    lv_obj_t *home_brand_txt;
    lv_obj_t *home_date_txt;
    lv_obj_t *home_left_card;
    lv_obj_t *home_main_title;
    lv_obj_t *home_main_hint;
    lv_obj_t *home_singer_img;
    lv_obj_t *home_bt_card;
    lv_obj_t *home_bt_title;
    lv_obj_t *home_bt_hint;
    lv_obj_t *home_bt_img;
    lv_obj_t *home_media_card;
    lv_obj_t *home_med_title;
    lv_obj_t *home_med_hint;
    lv_obj_t *home_media_img;
    lv_obj_t *home_dance_card;
    lv_obj_t *home_dance_title;
    lv_obj_t *home_dance_hint;
    lv_obj_t *home_dance_img;
    lv_obj_t *home_album_card;
    lv_obj_t *home_album_title;
    lv_obj_t *home_album_hint;
    lv_obj_t *home_album_img;
    lv_obj_t *home_sys_card;
    lv_obj_t *home_sys_title;
    lv_obj_t *home_sys_hint;
    lv_obj_t *home_sys_img;
    lv_obj_t *home_nav_grp;
    lv_obj_t *home_nav_eq_img;
    lv_obj_t *home_nav_set_img;
    lv_obj_t *home_nav_user_img;
    lv_obj_t *home_nav_wifi_img;
    lv_obj_t *home_nav_bat_img;
    /* Page: 1 objects */
    lv_obj_t *screen_saver;
    lv_obj_t *screen_saver_ss_bg;
    lv_obj_t *screen_saver_ss_back;
    lv_obj_t *screen_saver_ss_title;
    lv_obj_t *screen_saver_clock_card;
    lv_obj_t *screen_saver_clock_img;
    lv_obj_t *screen_saver_clock_txt;
    lv_obj_t *screen_saver_clock_en;
    lv_obj_t *screen_saver_scene_card;
    lv_obj_t *screen_saver_scene_img;
    lv_obj_t *screen_saver_scene_txt;
    lv_obj_t *screen_saver_scene_en;
    lv_obj_t *screen_saver_art_card;
    lv_obj_t *screen_saver_art_img;
    lv_obj_t *screen_saver_art_txt;
    lv_obj_t *screen_saver_art_en;
    lv_obj_t *screen_saver_photo_card;
    lv_obj_t *screen_saver_photo_img;
    lv_obj_t *screen_saver_photo_txt;
    lv_obj_t *screen_saver_photo_en;
    lv_obj_t *screen_saver_sel_btn1;
    lv_obj_t *screen_saver_sel_txt1;
    lv_obj_t *screen_saver_sel_btn2;
    lv_obj_t *screen_saver_sel_txt2;
    lv_obj_t *screen_saver_sel_btn3;
    lv_obj_t *screen_saver_sel_txt3;
    lv_obj_t *screen_saver_sel_btn4;
    lv_obj_t *screen_saver_sel_txt4;
    /* Page: 2 objects */
    lv_obj_t *klok_main;
    lv_obj_t *klok_main_km_bg;
    lv_obj_t *klok_main_km_back;
    lv_obj_t *klok_main_video_box;
    lv_obj_t *klok_main_fav_card;
    lv_obj_t *klok_main_record_img;
    lv_obj_t *klok_main_fav_text;
    lv_obj_t *klok_main_rec_panel;
    lv_obj_t *klok_main_rec_title;
    lv_obj_t *klok_main_rec_more;
    lv_obj_t *klok_main_rec_row1;
    lv_obj_t *klok_main_rec_row2;
    lv_obj_t *klok_main_rec_row3;
    lv_obj_t *klok_main_rec_row4;
    lv_obj_t *klok_main_rec_mic1;
    lv_obj_t *klok_main_rec_add1;
    lv_obj_t *klok_main_rec_mic2;
    lv_obj_t *klok_main_rec_add2;
    lv_obj_t *klok_main_rec_mic3;
    lv_obj_t *klok_main_rec_add3;
    lv_obj_t *klok_main_rec_mic4;
    lv_obj_t *klok_main_rec_add4;
    lv_obj_t *klok_main_name_card;
    lv_obj_t *klok_main_name_title;
    lv_obj_t *klok_main_name_hint;
    lv_obj_t *klok_main_search_img;
    lv_obj_t *klok_main_singer_card;
    lv_obj_t *klok_main_singer_title;
    lv_obj_t *klok_main_singer_hint;
    lv_obj_t *klok_main_crown_img;
    lv_obj_t *klok_main_rank_card;
    lv_obj_t *klok_main_rank_title;
    lv_obj_t *klok_main_rank_hint;
    lv_obj_t *klok_main_trophy_img;
    lv_obj_t *klok_main_bottom_bar;
    lv_obj_t *klok_main_bot_actions;
    lv_obj_t *klok_main_mute_btn;
    lv_obj_t *klok_main_mute_btn_label;
    lv_obj_t *klok_main_vol_down_btn;
    lv_obj_t *klok_main_vol_down_btn_label;
    lv_obj_t *klok_main_vol_up_btn;
    lv_obj_t *klok_main_vol_up_btn_label;
    lv_obj_t *klok_main_vocal_btn;
    lv_obj_t *klok_main_vocal_btn_label;
    lv_obj_t *klok_main_replay_btn;
    lv_obj_t *klok_main_replay_btn_label;
    lv_obj_t *klok_main_next_btn;
    lv_obj_t *klok_main_next_btn_label;
    lv_obj_t *klok_main_play_btn;
    lv_obj_t *klok_main_play_btn_label;
    lv_obj_t *klok_main_full_btn;
    lv_obj_t *klok_main_full_btn_label;
    lv_obj_t *klok_main_volume_bar;
    lv_obj_t *klok_main_top_status;
    lv_obj_t *klok_main_vip_box;
    lv_obj_t *klok_main_vip_txt;
    lv_obj_t *klok_main_day_txt;
    lv_obj_t *klok_main_mix_box;
    lv_obj_t *klok_main_mix_txt;
    lv_obj_t *klok_main_mix_icon;
    lv_obj_t *klok_main_queue_box;
    lv_obj_t *klok_main_queue_txt;
    lv_obj_t *klok_main_queue_num;
    /* Page: 3 objects */
    lv_obj_t *song_list;
    lv_obj_t *song_list_sl_bg;
    lv_obj_t *song_list_sl_back;
    lv_obj_t *song_list_sl_video;
    lv_obj_t *song_list_search_bar;
    lv_obj_t *song_list_sbar_icon;
    lv_obj_t *song_list_sbar_txt;
    lv_obj_t *song_list_key_panel;
    lv_obj_t *song_list_key_a;
    lv_obj_t *song_list_key_a_label;
    lv_obj_t *song_list_key_b;
    lv_obj_t *song_list_key_b_label;
    lv_obj_t *song_list_key_c;
    lv_obj_t *song_list_key_c_label;
    lv_obj_t *song_list_key_d;
    lv_obj_t *song_list_key_d_label;
    lv_obj_t *song_list_key_e;
    lv_obj_t *song_list_key_e_label;
    lv_obj_t *song_list_key_f;
    lv_obj_t *song_list_key_f_label;
    lv_obj_t *song_list_key_g;
    lv_obj_t *song_list_key_g_label;
    lv_obj_t *song_list_key_h;
    lv_obj_t *song_list_key_h_label;
    lv_obj_t *song_list_key_i;
    lv_obj_t *song_list_key_i_label;
    lv_obj_t *song_list_key_j;
    lv_obj_t *song_list_key_j_label;
    lv_obj_t *song_list_key_k;
    lv_obj_t *song_list_key_k_label;
    lv_obj_t *song_list_key_l;
    lv_obj_t *song_list_key_l_label;
    lv_obj_t *song_list_key_m;
    lv_obj_t *song_list_key_m_label;
    lv_obj_t *song_list_key_n;
    lv_obj_t *song_list_key_n_label;
    lv_obj_t *song_list_key_o;
    lv_obj_t *song_list_key_o_label;
    lv_obj_t *song_list_key_p;
    lv_obj_t *song_list_key_p_label;
    lv_obj_t *song_list_key_q;
    lv_obj_t *song_list_key_q_label;
    lv_obj_t *song_list_key_r;
    lv_obj_t *song_list_key_r_label;
    lv_obj_t *song_list_key_s;
    lv_obj_t *song_list_key_s_label;
    lv_obj_t *song_list_key_t;
    lv_obj_t *song_list_key_t_label;
    lv_obj_t *song_list_key_u;
    lv_obj_t *song_list_key_u_label;
    lv_obj_t *song_list_key_v;
    lv_obj_t *song_list_key_v_label;
    lv_obj_t *song_list_key_w;
    lv_obj_t *song_list_key_w_label;
    lv_obj_t *song_list_key_x;
    lv_obj_t *song_list_key_x_label;
    lv_obj_t *song_list_key_y;
    lv_obj_t *song_list_key_y_label;
    lv_obj_t *song_list_key_z;
    lv_obj_t *song_list_key_z_label;
    lv_obj_t *song_list_key_1;
    lv_obj_t *song_list_key_1_label;
    lv_obj_t *song_list_key_2;
    lv_obj_t *song_list_key_2_label;
    lv_obj_t *song_list_key_3;
    lv_obj_t *song_list_key_3_label;
    lv_obj_t *song_list_key_4;
    lv_obj_t *song_list_key_4_label;
    lv_obj_t *song_list_key_5;
    lv_obj_t *song_list_key_5_label;
    lv_obj_t *song_list_key_6;
    lv_obj_t *song_list_key_6_label;
    lv_obj_t *song_list_key_7;
    lv_obj_t *song_list_key_7_label;
    lv_obj_t *song_list_key_8;
    lv_obj_t *song_list_key_8_label;
    lv_obj_t *song_list_key_9;
    lv_obj_t *song_list_key_9_label;
    lv_obj_t *song_list_key_0;
    lv_obj_t *song_list_key_0_label;
    lv_obj_t *song_list_key_clear;
    lv_obj_t *song_list_key_clear_label;
    lv_obj_t *song_list_key_del;
    lv_obj_t *song_list_key_del_label;
    lv_obj_t *song_list_song_panel;
    lv_obj_t *song_list_row1;
    lv_obj_t *song_list_row2;
    lv_obj_t *song_list_row3;
    lv_obj_t *song_list_row4;
    lv_obj_t *song_list_row5;
    lv_obj_t *song_list_row6;
    lv_obj_t *song_list_mic1;
    lv_obj_t *song_list_heart1;
    lv_obj_t *song_list_plus1;
    lv_obj_t *song_list_mic2;
    lv_obj_t *song_list_heart2;
    lv_obj_t *song_list_plus2;
    lv_obj_t *song_list_mic3;
    lv_obj_t *song_list_heart3;
    lv_obj_t *song_list_plus3;
    lv_obj_t *song_list_mic4;
    lv_obj_t *song_list_heart4;
    lv_obj_t *song_list_plus4;
    lv_obj_t *song_list_mic5;
    lv_obj_t *song_list_heart5;
    lv_obj_t *song_list_plus5;
    lv_obj_t *song_list_mic6;
    lv_obj_t *song_list_heart6;
    lv_obj_t *song_list_plus6;
    lv_obj_t *song_list_tab_all;
    lv_obj_t *song_list_tab_all_label;
    lv_obj_t *song_list_tab_cn;
    lv_obj_t *song_list_tab_cn_label;
    lv_obj_t *song_list_tab_en;
    lv_obj_t *song_list_tab_en_label;
    lv_obj_t *song_list_tab_other;
    lv_obj_t *song_list_tab_other_label;
    lv_obj_t *song_list_sl_bottom;
    lv_obj_t *song_list_bot_actions;
    lv_obj_t *song_list_mute_btn;
    lv_obj_t *song_list_mute_btn_label;
    lv_obj_t *song_list_vol_down_btn;
    lv_obj_t *song_list_vol_down_btn_label;
    lv_obj_t *song_list_vol_up_btn;
    lv_obj_t *song_list_vol_up_btn_label;
    lv_obj_t *song_list_vocal_btn;
    lv_obj_t *song_list_vocal_btn_label;
    lv_obj_t *song_list_replay_btn;
    lv_obj_t *song_list_replay_btn_label;
    lv_obj_t *song_list_next_btn;
    lv_obj_t *song_list_next_btn_label;
    lv_obj_t *song_list_play_btn;
    lv_obj_t *song_list_play_btn_label;
    lv_obj_t *song_list_full_btn;
    lv_obj_t *song_list_full_btn_label;
    lv_obj_t *song_list_volume_bar;
    lv_obj_t *song_list_top_status;
    lv_obj_t *song_list_vip_info;
    lv_obj_t *song_list_vip_info_label;
    lv_obj_t *song_list_vip_day;
    lv_obj_t *song_list_vip_day_label;
    lv_obj_t *song_list_mix_btn;
    lv_obj_t *song_list_mix_btn_label;
    lv_obj_t *song_list_mix_icon;
    lv_obj_t *song_list_queue_btn;
    lv_obj_t *song_list_queue_btn_label;
    lv_obj_t *song_list_queue_num;
    lv_obj_t *song_list_queue_num_label;
    /* Page: 4 objects */
    lv_obj_t *mv_play;
    lv_obj_t *mv_play_mv_root;
    lv_obj_t *mv_play_video_img;
    lv_obj_t *mv_play_back_btn;
    lv_obj_t *mv_play_back_btn_label;
    lv_obj_t *mv_play_qr_card;
    lv_obj_t *mv_play_qr_code;
    lv_obj_t *mv_play_qr_tip;
    lv_obj_t *mv_play_fix_btn;
    lv_obj_t *mv_play_fix_btn_label;
    lv_obj_t *mv_play_star_ic;
    lv_obj_t *mv_play_fix_txt;
    lv_obj_t *mv_play_ctrl_next;
    lv_obj_t *mv_play_ctrl_next_label;
    lv_obj_t *mv_play_ctrl_pause;
    lv_obj_t *mv_play_ctrl_pause_label;
    lv_obj_t *mv_play_ctrl_replay;
    lv_obj_t *mv_play_ctrl_replay_label;
    lv_obj_t *mv_play_ctrl_duet;
    lv_obj_t *mv_play_ctrl_duet_label;
    lv_obj_t *mv_play_ctrl_queue;
    lv_obj_t *mv_play_ctrl_queue_label;
    lv_obj_t *mv_play_next_ic;
    lv_obj_t *mv_play_next_txt;
    lv_obj_t *mv_play_pause_ic;
    lv_obj_t *mv_play_pause_txt;
    lv_obj_t *mv_play_replay_ic;
    lv_obj_t *mv_play_replay_txt;
    lv_obj_t *mv_play_duet_ic;
    lv_obj_t *mv_play_duet_txt;
    lv_obj_t *mv_play_queue_ic;
    lv_obj_t *mv_play_queue_txt;
    lv_obj_t *mv_play_q_badge;
} bk_lv_ui_t;

void init_page_home(bk_lv_ui_t *bk_ui);
void destroy_page_home(bk_lv_ui_t *bk_ui);
void init_page_screen_saver(bk_lv_ui_t *bk_ui);
void destroy_page_screen_saver(bk_lv_ui_t *bk_ui);
void init_page_klok_main(bk_lv_ui_t *bk_ui);
void destroy_page_klok_main(bk_lv_ui_t *bk_ui);
void init_page_song_list(bk_lv_ui_t *bk_ui);
void destroy_page_song_list(bk_lv_ui_t *bk_ui);
void klok_song_list_refresh(bk_lv_ui_t *bk_ui);
void init_page_mv_play(bk_lv_ui_t *bk_ui);
void destroy_page_mv_play(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(icon_crown_96x96_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_heart_36_36x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_mic_36_36x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_photo_add_140x140_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_plus_36_36x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_record_220x220_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_search_36_36x36_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_search_96x96_RGB565A8_NONE);
LV_IMAGE_DECLARE(icon_trophy_96x96_RGB565A8_NONE);
LV_IMAGE_DECLARE(klok_home_bg_ARGB8888);
LV_IMAGE_DECLARE(klok_icon_album_v2_104x104_RGB565A8_NONE);
LV_IMAGE_DECLARE(klok_icon_bt_v2_104x104_RGB565A8_NONE);
LV_IMAGE_DECLARE(klok_icon_dance_v2_104x104_RGB565A8_NONE);
LV_IMAGE_DECLARE(klok_icon_media_v2_104x104_RGB565A8_NONE);
LV_IMAGE_DECLARE(klok_icon_singer_v2_300x360_RGB565A8_NONE);
LV_IMAGE_DECLARE(klok_icon_system_v2_104x104_RGB565A8_NONE);
LV_IMAGE_DECLARE(mv_list_32x32_RGB565A8_NONE);
LV_IMAGE_DECLARE(mv_mic_32x32_RGB565A8_NONE);
LV_IMAGE_DECLARE(mv_next_32x32_RGB565A8_NONE);
LV_IMAGE_DECLARE(mv_pause_32x32_RGB565A8_NONE);
LV_IMAGE_DECLARE(mv_replay_32x32_RGB565A8_NONE);
LV_IMAGE_DECLARE(mv_star_32x32_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_battery_40x40_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_eq_40x40_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_gear_40x40_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_user_40x40_RGB565A8_NONE);
LV_IMAGE_DECLARE(nav_wifi_40x40_RGB565A8_NONE);
LV_IMAGE_DECLARE(ss_art_card_240x350_RGB565A8_NONE);
LV_IMAGE_DECLARE(ss_clock_card_240x350_RGB565A8_NONE);
LV_IMAGE_DECLARE(ss_scenery_card_240x350_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_montserrat_regular_30);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_24);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_24);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_16);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_26);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_25);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_21);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_19);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_13);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_20);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_22);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_18);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_15);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_18);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_30);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_29);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_16);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_95_ExtraBold_95_ExtraBold_16);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_17);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_15);
LV_FONT_DECLARE(lv_font_Alibaba_PuHuiTi_2_0_95_ExtraBold_95_ExtraBold_18);
LV_FONT_DECLARE(lv_font_montserrat_regular_16);

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
