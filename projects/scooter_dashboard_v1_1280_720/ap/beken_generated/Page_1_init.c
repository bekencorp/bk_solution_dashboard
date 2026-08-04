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
#include "custom_func.h"
#include "event_runtime.h"
#include <stdio.h>
#include <string.h>

/*
 * @brief: init page Page_1
 */
void init_page_Page_1(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->Page_1 != NULL && lv_obj_is_valid(bk_ui->Page_1)) {
        destroy_page_Page_1(bk_ui);
    }
    

    bk_ui->Page_1 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->Page_1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->Page_1, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->Page_1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_obj_1 = lv_obj_create(bk_ui->Page_1);
    lv_obj_set_x(bk_ui->Page_1_obj_1, 99);
    lv_obj_set_y(bk_ui->Page_1_obj_1, 191);
    lv_obj_set_width(bk_ui->Page_1_obj_1, 333);
    lv_obj_set_height(bk_ui->Page_1_obj_1, 180);
    lv_obj_set_style_bg_color(bk_ui->Page_1_obj_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_obj_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_obj_1, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_obj_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_obj_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_obj_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_obj_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_obj_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_obj_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->Page_1_obj_1, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_obj_1, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_obj_1, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_obj_1, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_obj_1, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_obj_1, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_obj_1, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_obj_1, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_obj_1, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->Page_1_label_1 = lv_label_create(bk_ui->Page_1_obj_1);
    lv_label_set_text(bk_ui->Page_1_label_1, "TRIP");
    lv_label_set_long_mode(bk_ui->Page_1_label_1, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->Page_1_label_1, 0);
    lv_obj_set_y(bk_ui->Page_1_label_1, 0);
    lv_obj_set_width(bk_ui->Page_1_label_1, 173);
    lv_obj_set_height(bk_ui->Page_1_label_1, 77);
    lv_obj_set_style_bg_color(bk_ui->Page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_label_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_label_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_label_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->Page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->Page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->Page_1_label_1, &lv_font_Tinos_Regular_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->Page_1_label_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_label_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_spangroup_1 = lv_spangroup_create(bk_ui->Page_1_obj_1);
    lv_obj_set_x(bk_ui->Page_1_spangroup_1, 0);
    lv_obj_set_y(bk_ui->Page_1_spangroup_1, 93);
    lv_obj_set_width(bk_ui->Page_1_spangroup_1, 267);
    lv_obj_set_height(bk_ui->Page_1_spangroup_1, 79);
    bk_ui->Page_1_spangroup_1_span_0 = lv_spangroup_new_span(bk_ui->Page_1_spangroup_1);
    lv_span_set_text(bk_ui->Page_1_spangroup_1_span_0, "300");
    lv_style_set_text_color(lv_span_get_style(bk_ui->Page_1_spangroup_1_span_0), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->Page_1_spangroup_1_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->Page_1_spangroup_1_span_0), &lv_font_Tinos_Bold_80);
    bk_ui->Page_1_spangroup_1_span_1 = lv_spangroup_new_span(bk_ui->Page_1_spangroup_1);
    lv_span_set_text(bk_ui->Page_1_spangroup_1_span_1, " km");
    lv_style_set_text_color(lv_span_get_style(bk_ui->Page_1_spangroup_1_span_1), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->Page_1_spangroup_1_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->Page_1_spangroup_1_span_1), &lv_font_Tinos_Regular_60);
    lv_obj_set_style_bg_color(bk_ui->Page_1_spangroup_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_spangroup_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_spangroup_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_spangroup_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_spangroup_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_spangroup_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_spangroup_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_spangroup_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_spangroup_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_obj_2 = lv_obj_create(bk_ui->Page_1);
    lv_obj_set_x(bk_ui->Page_1_obj_2, 963);
    lv_obj_set_y(bk_ui->Page_1_obj_2, 191);
    lv_obj_set_width(bk_ui->Page_1_obj_2, 269);
    lv_obj_set_height(bk_ui->Page_1_obj_2, 180);
    lv_obj_set_style_bg_color(bk_ui->Page_1_obj_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_obj_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_obj_2, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_obj_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_obj_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_obj_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_obj_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_obj_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_obj_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->Page_1_obj_2, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_obj_2, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_obj_2, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_obj_2, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_obj_2, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_obj_2, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_obj_2, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_obj_2, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_obj_2, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->Page_1_label_2 = lv_label_create(bk_ui->Page_1_obj_2);
    lv_label_set_text(bk_ui->Page_1_label_2, "ODO");
    lv_label_set_long_mode(bk_ui->Page_1_label_2, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->Page_1_label_2, -2);
    lv_obj_set_y(bk_ui->Page_1_label_2, 0);
    lv_obj_set_width(bk_ui->Page_1_label_2, 187);
    lv_obj_set_height(bk_ui->Page_1_label_2, 77);
    lv_obj_set_style_bg_color(bk_ui->Page_1_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_label_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_label_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_label_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->Page_1_label_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->Page_1_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->Page_1_label_2, &lv_font_Tinos_Regular_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->Page_1_label_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_label_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_label_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_label_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_spangroup_2 = lv_spangroup_create(bk_ui->Page_1_obj_2);
    lv_obj_set_x(bk_ui->Page_1_spangroup_2, 1);
    lv_obj_set_y(bk_ui->Page_1_spangroup_2, 93);
    lv_obj_set_width(bk_ui->Page_1_spangroup_2, 267);
    lv_obj_set_height(bk_ui->Page_1_spangroup_2, 79);
    bk_ui->Page_1_spangroup_2_span_0 = lv_spangroup_new_span(bk_ui->Page_1_spangroup_2);
    lv_span_set_text(bk_ui->Page_1_spangroup_2_span_0, "182");
    lv_style_set_text_color(lv_span_get_style(bk_ui->Page_1_spangroup_2_span_0), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->Page_1_spangroup_2_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->Page_1_spangroup_2_span_0), &lv_font_Tinos_Bold_80);
    bk_ui->Page_1_spangroup_2_span_1 = lv_spangroup_new_span(bk_ui->Page_1_spangroup_2);
    lv_span_set_text(bk_ui->Page_1_spangroup_2_span_1, " km");
    lv_style_set_text_color(lv_span_get_style(bk_ui->Page_1_spangroup_2_span_1), lv_color_hex(0xffffff));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->Page_1_spangroup_2_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->Page_1_spangroup_2_span_1), &lv_font_Tinos_Regular_60);
    lv_obj_set_style_bg_color(bk_ui->Page_1_spangroup_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_spangroup_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_spangroup_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_spangroup_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_spangroup_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_spangroup_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_spangroup_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_spangroup_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_spangroup_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_dclock_1 = lv_label_create(bk_ui->Page_1);
    lv_label_set_text(bk_ui->Page_1_dclock_1, "");
    lv_digital_clock_register(bk_ui->Page_1_dclock_1, 1, 1, 11, 25, 50);
    lv_obj_set_x(bk_ui->Page_1_dclock_1, 45);
    lv_obj_set_y(bk_ui->Page_1_dclock_1, 20);
    lv_obj_set_width(bk_ui->Page_1_dclock_1, 323);
    lv_obj_set_height(bk_ui->Page_1_dclock_1, 58);
    lv_obj_set_style_bg_color(bk_ui->Page_1_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_dclock_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_dclock_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_dclock_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->Page_1_dclock_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->Page_1_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->Page_1_dclock_1, &lv_font_Tinos_Regular_60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->Page_1_dclock_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_dclock_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_dclock_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_dclock_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_scale_1 = lv_scale_create(bk_ui->Page_1);
    lv_obj_update_layout(bk_ui->Page_1_scale_1);
    lv_scale_set_mode(bk_ui->Page_1_scale_1, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(bk_ui->Page_1_scale_1, 41);
    lv_scale_set_major_tick_every(bk_ui->Page_1_scale_1, 8);
    lv_scale_set_label_show(bk_ui->Page_1_scale_1, true);
    lv_scale_set_range(bk_ui->Page_1_scale_1, 0, 100);
    lv_scale_set_angle_range(bk_ui->Page_1_scale_1, 260);
    lv_scale_set_rotation(bk_ui->Page_1_scale_1, 140);
    lv_scale_set_post_draw(bk_ui->Page_1_scale_1, true);
    lv_obj_set_x(bk_ui->Page_1_scale_1, 389);
    lv_obj_set_y(bk_ui->Page_1_scale_1, 171);
    lv_obj_set_width(bk_ui->Page_1_scale_1, 507);
    lv_obj_set_height(bk_ui->Page_1_scale_1, 507);
    bk_ui->Page_1_scale_1_needle_0 = lv_line_create(bk_ui->Page_1_scale_1);
    lv_obj_set_style_line_width(bk_ui->Page_1_scale_1_needle_0, 10, LV_PART_MAIN);
    lv_obj_set_style_line_color(bk_ui->Page_1_scale_1_needle_0, lv_color_hex(0x0000ff), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(bk_ui->Page_1_scale_1_needle_0, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(bk_ui->Page_1_scale_1, bk_ui->Page_1_scale_1_needle_0, 250, 20);
    lv_scale_section_t * Page_1_scale_1_section_0 = lv_scale_add_section(bk_ui->Page_1_scale_1);
    static lv_style_t Page_1_scale_1_section_0_minor_tick_style;
    static lv_style_t Page_1_scale_1_section_0_label_style;
    static lv_style_t Page_1_scale_1_section_0_main_line_style;
    lv_style_init(&Page_1_scale_1_section_0_label_style);
    lv_style_init(&Page_1_scale_1_section_0_minor_tick_style);
    lv_style_init(&Page_1_scale_1_section_0_main_line_style);
    lv_style_set_text_color(&Page_1_scale_1_section_0_label_style, lv_color_hex(0xffff00));
    lv_style_set_line_color(&Page_1_scale_1_section_0_label_style, lv_color_hex(0x08cffb));
    lv_style_set_line_color(&Page_1_scale_1_section_0_minor_tick_style, lv_color_hex(0x08cffb));
    lv_style_set_line_width(&Page_1_scale_1_section_0_minor_tick_style, 4);
    lv_style_set_arc_color(&Page_1_scale_1_section_0_main_line_style, lv_color_hex(0x03ffb0));
    lv_style_set_arc_width(&Page_1_scale_1_section_0_main_line_style, 2);
    lv_scale_section_set_range(Page_1_scale_1_section_0, 0, 20);
    lv_scale_section_set_style(Page_1_scale_1_section_0, LV_PART_INDICATOR, &Page_1_scale_1_section_0_label_style);
    lv_scale_section_set_style(Page_1_scale_1_section_0, LV_PART_MAIN, &Page_1_scale_1_section_0_main_line_style);
    lv_scale_section_set_style(Page_1_scale_1_section_0, LV_PART_ITEMS, &Page_1_scale_1_section_0_minor_tick_style);
    lv_obj_set_style_bg_color(bk_ui->Page_1_scale_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_scale_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_scale_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_scale_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->Page_1_scale_1, lv_color_hex(0xff0027), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->Page_1_scale_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->Page_1_scale_1, &lv_font_Tinos_Regular_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->Page_1_scale_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->Page_1_scale_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->Page_1_scale_1, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->Page_1_scale_1, lv_color_hex(0xff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->Page_1_scale_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->Page_1_scale_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_length(bk_ui->Page_1_scale_1, 18, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(bk_ui->Page_1_scale_1, 4, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(bk_ui->Page_1_scale_1, lv_color_hex(0xff0000), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(bk_ui->Page_1_scale_1, 255, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(bk_ui->Page_1_scale_1, false, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_length(bk_ui->Page_1_scale_1, 25, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(bk_ui->Page_1_scale_1, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(bk_ui->Page_1_scale_1, lv_color_hex(0x00ffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(bk_ui->Page_1_scale_1, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(bk_ui->Page_1_scale_1, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_8 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_8, &kmbg_301x291_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_8, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_8, 0);
    lv_obj_set_x(bk_ui->Page_1_image_8, 489);
    lv_obj_set_y(bk_ui->Page_1_image_8, 270);
    lv_obj_set_width(bk_ui->Page_1_image_8, 301);
    lv_obj_set_height(bk_ui->Page_1_image_8, 291);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_8, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_8, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_8, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_label_3 = lv_label_create(bk_ui->Page_1);
    lv_label_set_text(bk_ui->Page_1_label_3, "20");
    lv_label_set_long_mode(bk_ui->Page_1_label_3, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->Page_1_label_3, 562);
    lv_obj_set_y(bk_ui->Page_1_label_3, 368);
    lv_obj_set_width(bk_ui->Page_1_label_3, 156);
    lv_obj_set_height(bk_ui->Page_1_label_3, 79);
    lv_obj_set_style_bg_color(bk_ui->Page_1_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_label_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_label_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_label_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->Page_1_label_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->Page_1_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->Page_1_label_3, &lv_font_Tinos_Bold_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->Page_1_label_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_label_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_label_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_label_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_10 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_10, &right_light_grey_128x128_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_10, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_10, 0);
    lv_obj_set_x(bk_ui->Page_1_image_10, 1040);
    lv_obj_set_y(bk_ui->Page_1_image_10, 504);
    lv_obj_set_width(bk_ui->Page_1_image_10, 128);
    lv_obj_set_height(bk_ui->Page_1_image_10, 128);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_10, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_10, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_10, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_10, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_10, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_10, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_10, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_11 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_11, &light_11_83x64_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_11, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_11, 0);
    lv_obj_set_x(bk_ui->Page_1_image_11, 600);
    lv_obj_set_y(bk_ui->Page_1_image_11, 598);
    lv_obj_set_width(bk_ui->Page_1_image_11, 83);
    lv_obj_set_height(bk_ui->Page_1_image_11, 64);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_11, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_11, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_11, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_11, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_11, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_11, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_11, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_13 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_13, &beken_logo_blue_190x102_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_13, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_13, 0);
    lv_obj_set_x(bk_ui->Page_1_image_13, 545);
    lv_obj_set_y(bk_ui->Page_1_image_13, 22);
    lv_obj_set_width(bk_ui->Page_1_image_13, 190);
    lv_obj_set_height(bk_ui->Page_1_image_13, 102);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_13, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_13, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_13, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_13, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_13, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_13, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_13, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_13, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_13, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_13, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_13, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_14 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_14, &battery_bak_95x54_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_14, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_14, 0);
    lv_obj_set_x(bk_ui->Page_1_image_14, 1146);
    lv_obj_set_y(bk_ui->Page_1_image_14, 37);
    lv_obj_set_width(bk_ui->Page_1_image_14, 95);
    lv_obj_set_height(bk_ui->Page_1_image_14, 54);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_14, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_14, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_14, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_14, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_14, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_14, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_14, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_15 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_15, &battery_ind_102x54_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_15, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_15, 0);
    lv_obj_set_x(bk_ui->Page_1_image_15, 1142);
    lv_obj_set_y(bk_ui->Page_1_image_15, 39);
    lv_obj_set_width(bk_ui->Page_1_image_15, 102);
    lv_obj_set_height(bk_ui->Page_1_image_15, 54);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_15, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_15, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_15, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_15, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_15, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_15, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_15, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_15, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_16 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_16, &l_green_128x128_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_16, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_16, 0);
    lv_obj_set_x(bk_ui->Page_1_image_16, 114);
    lv_obj_set_y(bk_ui->Page_1_image_16, 504);
    lv_obj_set_width(bk_ui->Page_1_image_16, 128);
    lv_obj_set_height(bk_ui->Page_1_image_16, 128);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_16, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_16, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_16, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_16, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_16, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_16, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_16, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_16, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_16, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_16, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_16, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->Page_1_image_17 = lv_image_create(bk_ui->Page_1);
    lv_image_set_src(bk_ui->Page_1_image_17, &l_grey_128x128_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->Page_1_image_17, 50, 50);
    lv_image_set_rotation(bk_ui->Page_1_image_17, 0);
    lv_obj_set_x(bk_ui->Page_1_image_17, 114);
    lv_obj_set_y(bk_ui->Page_1_image_17, 504);
    lv_obj_set_width(bk_ui->Page_1_image_17, 128);
    lv_obj_set_height(bk_ui->Page_1_image_17, 128);
    lv_obj_set_style_bg_color(bk_ui->Page_1_image_17, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->Page_1_image_17, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->Page_1_image_17, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->Page_1_image_17, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->Page_1_image_17, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->Page_1_image_17, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->Page_1_image_17, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->Page_1_image_17, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->Page_1_image_17, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->Page_1_image_17, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->Page_1_image_17, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->Page_1);
}

/*
 * @brief: destroy page Page_1
 */
void destroy_page_Page_1(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->Page_1 != NULL) {
        lv_obj_del(bk_ui->Page_1);
        bk_ui->Page_1 = NULL;
    }
}