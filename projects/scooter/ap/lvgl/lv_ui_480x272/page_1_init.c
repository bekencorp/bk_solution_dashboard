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

static lv_timer_t *mileage_timer = NULL;
static lv_timer_t *turn_signal_timer = NULL;
static lv_timer_t *digital_clock_timer = NULL;
static lv_timer_t *speed_timer = NULL;

/*
 * @brief: init page page_1
 */
void init_page_page_1(bk_lv_ui_t *bk_ui)
{

    bk_ui->page_1 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_1, 480, 272);
    lv_obj_set_style_bg_color(bk_ui->page_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(bk_ui->page_1, &moto_ARGB8888, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(bk_ui->page_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor(bk_ui->page_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(bk_ui->page_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_obj_1 = lv_obj_create(bk_ui->page_1);
    lv_obj_set_x(bk_ui->page_1_obj_1, 37);
    lv_obj_set_y(bk_ui->page_1_obj_1, 72);
    lv_obj_set_width(bk_ui->page_1_obj_1, 125);
    lv_obj_set_height(bk_ui->page_1_obj_1, 68);
    lv_obj_set_style_bg_color(bk_ui->page_1_obj_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_obj_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_obj_1, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_obj_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_obj_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_obj_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_obj_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_label_1 = lv_label_create(bk_ui->page_1_obj_1);
    lv_label_set_text(bk_ui->page_1_label_1, "TRIP");
    lv_label_set_long_mode(bk_ui->page_1_label_1, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_1_label_1, 0);
    lv_obj_set_y(bk_ui->page_1_label_1, 0);
    lv_obj_set_width(bk_ui->page_1_label_1, 65);
    lv_obj_set_height(bk_ui->page_1_label_1, 29);
    lv_obj_set_style_bg_color(bk_ui->page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_label_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_label_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_label_1, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_label_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_label_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_spangroup_1 = lv_spangroup_create(bk_ui->page_1_obj_1);
    lv_obj_set_x(bk_ui->page_1_spangroup_1, 0);
    lv_obj_set_y(bk_ui->page_1_spangroup_1, 35);
    lv_obj_set_width(bk_ui->page_1_spangroup_1, 100);
    lv_obj_set_height(bk_ui->page_1_spangroup_1, 30);
    bk_ui->page_1_spangroup_1_span_0 = lv_spangroup_new_span(bk_ui->page_1_spangroup_1);
    lv_span_set_text(bk_ui->page_1_spangroup_1_span_0, "18.8");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_1_spangroup_1_span_0), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_1_spangroup_1_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_1_spangroup_1_span_0), &lv_font_montserrat_24);
    bk_ui->page_1_spangroup_1_span_1 = lv_spangroup_new_span(bk_ui->page_1_spangroup_1);
    lv_span_set_text(bk_ui->page_1_spangroup_1_span_1, " km");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_1_spangroup_1_span_1), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_1_spangroup_1_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_1_spangroup_1_span_1), &lv_font_montserrat_16);
    lv_obj_set_style_bg_color(bk_ui->page_1_spangroup_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_spangroup_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_spangroup_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_spangroup_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_spangroup_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_spangroup_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_spangroup_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_obj_2 = lv_obj_create(bk_ui->page_1);
    lv_obj_set_x(bk_ui->page_1_obj_2, 361);
    lv_obj_set_y(bk_ui->page_1_obj_2, 73);
    lv_obj_set_width(bk_ui->page_1_obj_2, 101);
    lv_obj_set_height(bk_ui->page_1_obj_2, 68);
    lv_obj_set_style_bg_color(bk_ui->page_1_obj_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_obj_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_obj_2, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_obj_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_obj_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_obj_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_obj_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_label_2 = lv_label_create(bk_ui->page_1_obj_2);
    lv_label_set_text(bk_ui->page_1_label_2, "ODO");
    lv_label_set_long_mode(bk_ui->page_1_label_2, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_1_label_2, 0);
    lv_obj_set_y(bk_ui->page_1_label_2, 0);
    lv_obj_set_width(bk_ui->page_1_label_2, 70);
    lv_obj_set_height(bk_ui->page_1_label_2, 29);
    lv_obj_set_style_bg_color(bk_ui->page_1_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_label_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_label_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_label_2, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_label_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_label_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_spangroup_2 = lv_spangroup_create(bk_ui->page_1_obj_2);
    lv_obj_set_x(bk_ui->page_1_spangroup_2, 0);
    lv_obj_set_y(bk_ui->page_1_spangroup_2, 35);
    lv_obj_set_width(bk_ui->page_1_spangroup_2, 100);
    lv_obj_set_height(bk_ui->page_1_spangroup_2, 30);
    bk_ui->page_1_spangroup_2_span_0 = lv_spangroup_new_span(bk_ui->page_1_spangroup_2);
    lv_span_set_text(bk_ui->page_1_spangroup_2_span_0, "300");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_1_spangroup_2_span_0), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_1_spangroup_2_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_1_spangroup_2_span_0), &lv_font_montserrat_24);
    bk_ui->page_1_spangroup_2_span_1 = lv_spangroup_new_span(bk_ui->page_1_spangroup_2);
    lv_span_set_text(bk_ui->page_1_spangroup_2_span_1, " km");
    lv_style_set_text_color(lv_span_get_style(bk_ui->page_1_spangroup_2_span_1), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->page_1_spangroup_2_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->page_1_spangroup_2_span_1), &lv_font_montserrat_16);
    lv_obj_set_style_bg_color(bk_ui->page_1_spangroup_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_spangroup_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_spangroup_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_spangroup_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_spangroup_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_spangroup_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_spangroup_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    mileage_timer = lv_timer_create(lv_mileage_timer_cb, 3000, NULL);

    bk_ui->page_1_image_1 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_1, &light_green_45x45_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_1, 0);
    lv_obj_set_x(bk_ui->page_1_image_1, 44);
    lv_obj_set_y(bk_ui->page_1_image_1, 161);
    lv_obj_set_width(bk_ui->page_1_image_1, 45);
    lv_obj_set_height(bk_ui->page_1_image_1, 45);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    turn_signal_timer = lv_timer_create(lv_turn_signal_timer_cb, 700, bk_ui->page_1_image_1);

    bk_ui->page_1_image_2 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_2, &light_grey_46x46_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_2, 1800);
    lv_obj_set_x(bk_ui->page_1_image_2, 387);
    lv_obj_set_y(bk_ui->page_1_image_2, 157);
    lv_obj_set_width(bk_ui->page_1_image_2, 46);
    lv_obj_set_height(bk_ui->page_1_image_2, 46);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    static bool screen_digital_clock_1_timer_enabled = false;
    bk_ui->page_1_dclock_1 = lv_label_create(bk_ui->page_1);
    lv_label_set_text(bk_ui->page_1_dclock_1, "11:25 AM");
    lv_obj_set_x(bk_ui->page_1_dclock_1, 16);
    lv_obj_set_y(bk_ui->page_1_dclock_1, 11);
    lv_obj_set_width(bk_ui->page_1_dclock_1, 121);
    lv_obj_set_height(bk_ui->page_1_dclock_1, 22);
    lv_obj_set_style_bg_color(bk_ui->page_1_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_dclock_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_dclock_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_dclock_1, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_dclock_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_dclock_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!screen_digital_clock_1_timer_enabled) {
        digital_clock_timer = lv_timer_create(lv_digital_clock_timer_cb, 1000, bk_ui->page_1_dclock_1);
        screen_digital_clock_1_timer_enabled = true;
    }

    bk_ui->page_1_image_3 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_3, &beken_logo_blue_76x42_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_3, 0);
    lv_obj_set_x(bk_ui->page_1_image_3, 198);
    lv_obj_set_y(bk_ui->page_1_image_3, 8);
    lv_obj_set_width(bk_ui->page_1_image_3, 76);
    lv_obj_set_height(bk_ui->page_1_image_3, 42);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_obj_3 = lv_obj_create(bk_ui->page_1);
    lv_obj_set_x(bk_ui->page_1_obj_3, 434);
    lv_obj_set_y(bk_ui->page_1_obj_3, 9);
    lv_obj_set_width(bk_ui->page_1_obj_3, 45);
    lv_obj_set_height(bk_ui->page_1_obj_3, 43);
    lv_obj_set_style_bg_color(bk_ui->page_1_obj_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_obj_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_obj_3, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_obj_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_obj_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_obj_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_obj_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_obj_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_image_4 = lv_image_create(bk_ui->page_1_obj_3);
    lv_image_set_src(bk_ui->page_1_image_4, &battery_bak_38x23_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_4, 0);
    lv_obj_set_x(bk_ui->page_1_image_4, 0);
    lv_obj_set_y(bk_ui->page_1_image_4, 0);
    lv_obj_set_width(bk_ui->page_1_image_4, 38);
    lv_obj_set_height(bk_ui->page_1_image_4, 23);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_4, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_image_5 = lv_image_create(bk_ui->page_1_obj_3);
    lv_image_set_src(bk_ui->page_1_image_5, &battery_ind_34x22_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_5, 0);
    lv_obj_set_x(bk_ui->page_1_image_5, 1);
    lv_obj_set_y(bk_ui->page_1_image_5, -1);
    lv_obj_set_width(bk_ui->page_1_image_5, 34);
    lv_obj_set_height(bk_ui->page_1_image_5, 22);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_5, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_image_6 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_6, &wifi_1_23x23_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_6, 0);
    lv_obj_set_x(bk_ui->page_1_image_6, 399);
    lv_obj_set_y(bk_ui->page_1_image_6, 8);
    lv_obj_set_width(bk_ui->page_1_image_6, 23);
    lv_obj_set_height(bk_ui->page_1_image_6, 23);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_6, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_image_7 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_7, &bluetooth_23x23_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_7, 0);
    lv_obj_set_x(bk_ui->page_1_image_7, 366);
    lv_obj_set_y(bk_ui->page_1_image_7, 8);
    lv_obj_set_width(bk_ui->page_1_image_7, 23);
    lv_obj_set_height(bk_ui->page_1_image_7, 23);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_7, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_7, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_7, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_scale_1 = lv_scale_create(bk_ui->page_1);
    lv_obj_update_layout(bk_ui->page_1_scale_1);
    lv_scale_set_mode(bk_ui->page_1_scale_1, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(bk_ui->page_1_scale_1, 41);
    lv_scale_set_major_tick_every(bk_ui->page_1_scale_1, 8);
    lv_scale_set_label_show(bk_ui->page_1_scale_1, true);
    lv_scale_set_range(bk_ui->page_1_scale_1, 0, 100);
    lv_scale_set_angle_range(bk_ui->page_1_scale_1, 260);
    lv_scale_set_rotation(bk_ui->page_1_scale_1, 140);
    lv_scale_set_post_draw(bk_ui->page_1_scale_1, true);
    lv_obj_set_x(bk_ui->page_1_scale_1, 146);
    lv_obj_set_y(bk_ui->page_1_scale_1, 59);
    lv_obj_set_width(bk_ui->page_1_scale_1, 190);
    lv_obj_set_height(bk_ui->page_1_scale_1, 190);
    bk_ui->page_1_scale_1_needle_0 = lv_line_create(bk_ui->page_1_scale_1);
    lv_obj_set_style_line_width(bk_ui->page_1_scale_1_needle_0, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(bk_ui->page_1_scale_1_needle_0, lv_color_hex(0x0933fe), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(bk_ui->page_1_scale_1_needle_0, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(bk_ui->page_1_scale_1, bk_ui->page_1_scale_1_needle_0, 65, 20);
    lv_scale_section_t * page_1_scale_1_section_0 = lv_scale_add_section(bk_ui->page_1_scale_1);
    static lv_style_t page_1_scale_1_section_0_minor_tick_style;
    static lv_style_t page_1_scale_1_section_0_label_style;
    static lv_style_t page_1_scale_1_section_0_main_line_style;
    lv_style_init(&page_1_scale_1_section_0_label_style);
    lv_style_init(&page_1_scale_1_section_0_minor_tick_style);
    lv_style_init(&page_1_scale_1_section_0_main_line_style);
    lv_style_set_text_color(&page_1_scale_1_section_0_label_style, lv_color_hex(0xffff00));
    lv_style_set_line_color(&page_1_scale_1_section_0_label_style, lv_color_hex(0x08cffb));
    lv_style_set_line_color(&page_1_scale_1_section_0_minor_tick_style, lv_color_hex(0x08cffb));
    lv_style_set_line_width(&page_1_scale_1_section_0_minor_tick_style, 1);
    lv_style_set_arc_color(&page_1_scale_1_section_0_main_line_style, lv_color_hex(0x03ffb0));
    lv_style_set_arc_width(&page_1_scale_1_section_0_main_line_style, 2);
    lv_scale_section_set_range(page_1_scale_1_section_0, 0, 20);
    lv_scale_section_set_style(page_1_scale_1_section_0, LV_PART_INDICATOR, &page_1_scale_1_section_0_label_style);
    lv_scale_section_set_style(page_1_scale_1_section_0, LV_PART_MAIN, &page_1_scale_1_section_0_main_line_style);
    lv_scale_section_set_style(page_1_scale_1_section_0, LV_PART_ITEMS, &page_1_scale_1_section_0_minor_tick_style);
    lv_obj_set_style_bg_color(bk_ui->page_1_scale_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_scale_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_scale_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_scale_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_scale_1, lv_color_hex(0xff0027), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_scale_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_scale_1, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_scale_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->page_1_scale_1, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->page_1_scale_1, lv_color_hex(0xff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->page_1_scale_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->page_1_scale_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_length(bk_ui->page_1_scale_1, 10, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(bk_ui->page_1_scale_1, 1, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(bk_ui->page_1_scale_1, lv_color_hex(0xff0000), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(bk_ui->page_1_scale_1, 255, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(bk_ui->page_1_scale_1, false, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_length(bk_ui->page_1_scale_1, 20, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(bk_ui->page_1_scale_1, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(bk_ui->page_1_scale_1, lv_color_hex(0x00ffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(bk_ui->page_1_scale_1, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(bk_ui->page_1_scale_1, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    bk_ui->page_1_image_8 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_8, &kmbg_113x110_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_8, 0);
    lv_obj_set_x(bk_ui->page_1_image_8, 186);
    lv_obj_set_y(bk_ui->page_1_image_8, 104);
    lv_obj_set_width(bk_ui->page_1_image_8, 113);
    lv_obj_set_height(bk_ui->page_1_image_8, 110);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_8, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_8, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_label_3 = lv_label_create(bk_ui->page_1);
    lv_label_set_text(bk_ui->page_1_label_3, "20");
    lv_label_set_long_mode(bk_ui->page_1_label_3, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_1_label_3, 212);
    lv_obj_set_y(bk_ui->page_1_label_3, 142);
    lv_obj_set_width(bk_ui->page_1_label_3, 60);
    lv_obj_set_height(bk_ui->page_1_label_3, 36);
    lv_obj_set_style_bg_color(bk_ui->page_1_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_label_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_label_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_label_3, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_label_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_label_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    speed_timer = lv_timer_create(lv_speed_timer_cb, 150, NULL);

    bk_ui->page_1_image_9 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_9, &light_11_34x26_RGB565A8_NONE);
    lv_image_set_rotation(bk_ui->page_1_image_9, 0);
    lv_obj_set_x(bk_ui->page_1_image_9, 223);
    lv_obj_set_y(bk_ui->page_1_image_9, 212);
    lv_obj_set_width(bk_ui->page_1_image_9, 34);
    lv_obj_set_height(bk_ui->page_1_image_9, 26);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_9, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_9, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_9, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_9, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);


    lv_obj_update_layout(bk_ui->page_1);
}

/*
 * @brief: destroy page page_1
 */
void destroy_page_page_1(bk_lv_ui_t *bk_ui)
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

    if (bk_ui->page_1 != NULL) {
        lv_obj_del(bk_ui->page_1);
        bk_ui->page_1 = NULL;
    }
}