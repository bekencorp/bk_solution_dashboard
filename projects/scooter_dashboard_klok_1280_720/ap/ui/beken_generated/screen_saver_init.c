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
/* Animation exec wrappers for style properties */
typedef void (*anim_color_setter_cb_t)(lv_obj_t * obj, lv_color_t color, lv_opa_t opa);
typedef enum {
    ANIM_EASING_LINEAR = 0,
    ANIM_EASING_EASE_IN,
    ANIM_EASING_EASE_OUT,
    ANIM_EASING_EASE_IN_OUT,
    ANIM_EASING_OVERSHOOT,
    ANIM_EASING_BOUNCE,
    ANIM_EASING_STEP,
} anim_easing_t;
typedef struct {
    uint32_t time_ms;
    int64_t value;
    anim_easing_t easing;
} anim_keyframe_t;
typedef struct {
    lv_obj_t * target;
    const anim_keyframe_t * keyframes;
    uint16_t keyframe_count;
    uint32_t duration;
    bool is_color;
    lv_anim_exec_xcb_t setter_i32;
    anim_color_setter_cb_t setter_color;
} anim_track_ctx_t;

static uint32_t anim_decode_color_rgba(uint64_t c) {
    // 新格式：marker + (RGB << 8) + A
    if (c >= 0x100000000ULL) {
        return (uint32_t)(c - 0x100000000ULL) & 0xFFFFFFFFU;
    }
    // 旧格式1：仅 RGB，默认 alpha=255
    if (c <= 0xFFFFFFULL) {
        return ((uint32_t)c << 8) | 0xFFU;
    }
    // 旧格式2：直接使用 0xRRGGBBAA
    return (uint32_t)c & 0xFFFFFFFFU;
}

static uint32_t anim_color_lerp_rgba(uint64_t c0, uint64_t c1, uint32_t t, uint32_t d) {
    c0 = anim_decode_color_rgba(c0);
    c1 = anim_decode_color_rgba(c1);
    if (d == 0) return c1 & 0xFFFFFFFFU;
    if (t > d) t = d;
    uint32_t r0 = (c0 >> 24) & 0xFFU;
    uint32_t g0 = (c0 >> 16) & 0xFFU;
    uint32_t b0 = (c0 >> 8) & 0xFFU;
    uint32_t a0 = c0 & 0xFFU;
    uint32_t r1 = (c1 >> 24) & 0xFFU;
    uint32_t g1 = (c1 >> 16) & 0xFFU;
    uint32_t b1 = (c1 >> 8) & 0xFFU;
    uint32_t a1 = c1 & 0xFFU;
    uint32_t rr = r0 + (((int32_t)r1 - (int32_t)r0) * (int32_t)t + (int32_t)(d / 2U)) / (int32_t)d;
    uint32_t gg = g0 + (((int32_t)g1 - (int32_t)g0) * (int32_t)t + (int32_t)(d / 2U)) / (int32_t)d;
    uint32_t bb = b0 + (((int32_t)b1 - (int32_t)b0) * (int32_t)t + (int32_t)(d / 2U)) / (int32_t)d;
    uint32_t aa = a0 + (((int32_t)a1 - (int32_t)a0) * (int32_t)t + (int32_t)(d / 2U)) / (int32_t)d;
    return ((rr & 0xFFU) << 24) | ((gg & 0xFFU) << 16) | ((bb & 0xFFU) << 8) | (aa & 0xFFU);
}

static uint32_t anim_apply_easing_u16(uint32_t progress, anim_easing_t easing) {
    if (progress > 1024U) progress = 1024U;
    double r = (double)progress / 1024.0;
    double out = r;
    switch (easing) {
        case ANIM_EASING_EASE_IN:
            out = r * r;
            break;
        case ANIM_EASING_EASE_OUT:
            out = 1.0 - (1.0 - r) * (1.0 - r);
            break;
        case ANIM_EASING_EASE_IN_OUT:
            out = 3.0 * r * r - 2.0 * r * r * r;
            break;
        case ANIM_EASING_OVERSHOOT: {
            const double s = 1.70158;
            out = r * r * ((s + 1.0) * r - s);
            break;
        }
        case ANIM_EASING_BOUNCE:
            if (r < 1.0 / 2.75) {
                out = 7.5625 * r * r;
            } else if (r < 2.0 / 2.75) {
                double r2 = r - 1.5 / 2.75;
                out = 7.5625 * r2 * r2 + 0.75;
            } else if (r < 2.5 / 2.75) {
                double r2 = r - 2.25 / 2.75;
                out = 7.5625 * r2 * r2 + 0.9375;
            } else {
                double r2 = r - 2.625 / 2.75;
                out = 7.5625 * r2 * r2 + 0.984375;
            }
            break;
        case ANIM_EASING_STEP:
            out = (r < 1.0) ? 0.0 : 1.0;
            break;
        case ANIM_EASING_LINEAR:
        default:
            out = r;
            break;
    }
    if (out < 0.0) out = 0.0;
    if (out > 1.0) out = 1.0;
    return (uint32_t)(out * 1024.0 + 0.5);
}

static int64_t anim_keyframe_value_at(const anim_track_ctx_t * ctx, uint32_t t) {
    if (ctx == NULL || ctx->keyframes == NULL || ctx->keyframe_count == 0U) return 0;
    const anim_keyframe_t * kfs = ctx->keyframes;
    uint16_t n = ctx->keyframe_count;
    if (t <= kfs[0].time_ms) return kfs[0].value;
    if (t >= kfs[n - 1].time_ms) return kfs[n - 1].value;

    uint16_t i = 0;
    while ((i + 1U) < n && t >= kfs[i + 1U].time_ms) i++;
    const anim_keyframe_t * k0 = &kfs[i];
    const anim_keyframe_t * k1 = &kfs[i + 1U];
    uint32_t span = (k1->time_ms > k0->time_ms) ? (k1->time_ms - k0->time_ms) : 0U;
    if (span == 0U) return k1->value;

    uint32_t local_t = t - k0->time_ms;
    uint32_t p = (uint32_t)(((uint64_t)local_t * 1024ULL + (uint64_t)(span / 2U)) / (uint64_t)span);
    p = anim_apply_easing_u16(p, k1->easing);

    if (ctx->is_color) {
        uint32_t rgba = anim_color_lerp_rgba((uint64_t)k0->value, (uint64_t)k1->value, p, 1024U);
        return (int64_t)(uint64_t)rgba;
    }

    int64_t dv = k1->value - k0->value;
    int64_t out = k0->value + (int64_t)(((dv * (int64_t)p) + 512LL) / 1024LL);
    return out;
}

static void anim_track_exec_common(lv_anim_t * a, int32_t v) {
    anim_track_ctx_t * ctx = (anim_track_ctx_t *)lv_anim_get_user_data(a);
    if (ctx == NULL || ctx->target == NULL || ctx->keyframes == NULL || ctx->keyframe_count == 0U) return;
    uint32_t d = ctx->duration > 0U ? ctx->duration : 1U;
    uint32_t t = (v < 0) ? 0U : (uint32_t)v;
    if (t > d) t = d;
    int64_t raw = anim_keyframe_value_at(ctx, t);

    if (ctx->is_color) {
        if (ctx->setter_color == NULL) return;
        uint32_t rgba = anim_decode_color_rgba((uint64_t)raw);
        ctx->setter_color(ctx->target, lv_color_hex((rgba >> 8) & 0xFFFFFFU), (lv_opa_t)(rgba & 0xFFU));
        return;
    }
    if (ctx->setter_i32 == NULL) return;
    ctx->setter_i32(ctx->target, (int32_t)raw);
}

static void anim_track_exec_x(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_y(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_width(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_height(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_opa(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_transform_rotation(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_bg_color(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_border_width(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_radius(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_border_color(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_shadow_color(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_shadow_width(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_shadow_spread(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_shadow_offset_x(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_shadow_offset_y(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_pad_top(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_pad_bottom(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_pad_left(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_pad_right(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_pad_row(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }
static void anim_track_exec_pad_column(lv_anim_t * a, int32_t v) { anim_track_exec_common(a, v); }

static void anim_set_style_opa(void * var, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, LV_PART_MAIN); }
static void anim_set_style_transform_rotation(void * var, int32_t v) { lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_bg_color(void * var, int32_t v) { lv_obj_set_style_bg_color((lv_obj_t *)var, lv_color_hex((uint32_t)v), LV_PART_MAIN); }
static void anim_apply_style_bg_color(lv_obj_t * obj, lv_color_t color, lv_opa_t opa) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
}
static void anim_set_style_border_width(void * var, int32_t v) { lv_obj_set_style_border_width((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_radius(void * var, int32_t v) { lv_obj_set_style_radius((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_border_color(void * var, int32_t v) { lv_obj_set_style_border_color((lv_obj_t *)var, lv_color_hex((uint32_t)v), LV_PART_MAIN); }
static void anim_apply_style_border_color(lv_obj_t * obj, lv_color_t color, lv_opa_t opa) {
    lv_obj_set_style_border_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, opa, LV_PART_MAIN);
}
static void anim_set_style_shadow_color(void * var, int32_t v) { lv_obj_set_style_shadow_color((lv_obj_t *)var, lv_color_hex((uint32_t)v), LV_PART_MAIN); }
static void anim_apply_style_shadow_color(lv_obj_t * obj, lv_color_t color, lv_opa_t opa) {
    lv_obj_set_style_shadow_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, opa, LV_PART_MAIN);
}
static void anim_set_style_shadow_width(void * var, int32_t v) { lv_obj_set_style_shadow_width((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_shadow_spread(void * var, int32_t v) { lv_obj_set_style_shadow_spread((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_shadow_offset_x(void * var, int32_t v) { lv_obj_set_style_shadow_offset_x((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_shadow_offset_y(void * var, int32_t v) { lv_obj_set_style_shadow_offset_y((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_pad_top(void * var, int32_t v) { lv_obj_set_style_pad_top((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_pad_bottom(void * var, int32_t v) { lv_obj_set_style_pad_bottom((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_pad_left(void * var, int32_t v) { lv_obj_set_style_pad_left((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_pad_right(void * var, int32_t v) { lv_obj_set_style_pad_right((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_pad_row(void * var, int32_t v) { lv_obj_set_style_pad_row((lv_obj_t *)var, v, LV_PART_MAIN); }
static void anim_set_style_pad_column(void * var, int32_t v) { lv_obj_set_style_pad_column((lv_obj_t *)var, v, LV_PART_MAIN); }



/*
 * @brief: init page screen_saver
 */
void init_page_screen_saver(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->screen_saver != NULL && lv_obj_is_valid(bk_ui->screen_saver)) {
        destroy_page_screen_saver(bk_ui);
    }
    

    bk_ui->screen_saver = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->screen_saver, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->screen_saver, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->screen_saver, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_ss_bg = lv_obj_create(bk_ui->screen_saver);
    lv_obj_set_x(bk_ui->screen_saver_ss_bg, 0);
    lv_obj_set_y(bk_ui->screen_saver_ss_bg, 0);
    lv_obj_set_width(bk_ui->screen_saver_ss_bg, 1280);
    lv_obj_set_height(bk_ui->screen_saver_ss_bg, 720);
    lv_obj_remove_flag(bk_ui->screen_saver_ss_bg, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_ss_bg, lv_color_hex(0x130935), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_ss_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_ss_bg, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(bk_ui->screen_saver_ss_bg, &klok_home_bg_ARGB8888, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(bk_ui->screen_saver_ss_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor(bk_ui->screen_saver_ss_bg, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_ss_bg, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_ss_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_ss_bg, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_ss_bg, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_ss_bg, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_ss_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_ss_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_ss_bg, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_ss_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_ss_bg, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_ss_bg, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_ss_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_ss_bg, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_ss_bg, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_ss_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_ss_bg, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_ss_back = lv_label_create(bk_ui->screen_saver_ss_bg);
    lv_label_set_text(bk_ui->screen_saver_ss_back, "返回首页");
    lv_label_set_long_mode(bk_ui->screen_saver_ss_back, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_ss_back, 38);
    lv_obj_set_y(bk_ui->screen_saver_ss_back, 38);
    lv_obj_set_width(bk_ui->screen_saver_ss_back, 230);
    lv_obj_set_height(bk_ui->screen_saver_ss_back, 32);
    lv_obj_remove_flag(bk_ui->screen_saver_ss_back, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_ss_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_ss_back, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_ss_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_ss_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_ss_back, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_ss_back, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_ss_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_ss_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_ss_back, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_ss_back, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_ss_back, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_ss_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_ss_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_ss_title = lv_label_create(bk_ui->screen_saver_ss_bg);
    lv_label_set_text(bk_ui->screen_saver_ss_title, "屏保设置");
    lv_label_set_long_mode(bk_ui->screen_saver_ss_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_ss_title, 465);
    lv_obj_set_y(bk_ui->screen_saver_ss_title, 110);
    lv_obj_set_width(bk_ui->screen_saver_ss_title, 360);
    lv_obj_set_height(bk_ui->screen_saver_ss_title, 42);
    lv_obj_remove_flag(bk_ui->screen_saver_ss_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_ss_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_ss_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_ss_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_ss_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_ss_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_ss_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_ss_title, lv_color_hex(0xfff6d8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_ss_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_ss_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_ss_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_ss_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_ss_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_ss_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_clock_card = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_clock_card, 28);
    lv_obj_set_y(bk_ui->screen_saver_clock_card, 185);
    lv_obj_set_width(bk_ui->screen_saver_clock_card, 265);
    lv_obj_set_height(bk_ui->screen_saver_clock_card, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_clock_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_clock_card, lv_color_hex(0x0a0820), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_clock_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_clock_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_clock_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_clock_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_clock_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_clock_card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_clock_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_clock_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_clock_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_clock_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_clock_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_clock_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_clock_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_clock_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_clock_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_clock_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_clock_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_clock_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_clock_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_clock_img = lv_image_create(bk_ui->screen_saver_clock_card);
    lv_image_set_src(bk_ui->screen_saver_clock_img, &ss_clock_card_240x350_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->screen_saver_clock_img, 50, 50);
    lv_image_set_rotation(bk_ui->screen_saver_clock_img, 0);
    lv_obj_set_x(bk_ui->screen_saver_clock_img, 12);
    lv_obj_set_y(bk_ui->screen_saver_clock_img, 0);
    lv_obj_set_width(bk_ui->screen_saver_clock_img, 240);
    lv_obj_set_height(bk_ui->screen_saver_clock_img, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_clock_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_clock_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_clock_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_clock_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_clock_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_clock_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_clock_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_clock_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_clock_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->screen_saver_clock_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->screen_saver_clock_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->screen_saver_clock_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_clock_txt = lv_label_create(bk_ui->screen_saver_clock_card);
    lv_label_set_text(bk_ui->screen_saver_clock_txt, "时钟屏保");
    lv_label_set_long_mode(bk_ui->screen_saver_clock_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_clock_txt, 20);
    lv_obj_set_y(bk_ui->screen_saver_clock_txt, 258);
    lv_obj_set_width(bk_ui->screen_saver_clock_txt, 225);
    lv_obj_set_height(bk_ui->screen_saver_clock_txt, 30);
    lv_obj_remove_flag(bk_ui->screen_saver_clock_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_clock_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_clock_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_clock_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_clock_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_clock_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_clock_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_clock_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_clock_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_clock_txt, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_clock_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_clock_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_clock_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_clock_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_clock_en = lv_label_create(bk_ui->screen_saver_clock_card);
    lv_label_set_text(bk_ui->screen_saver_clock_en, "经典时钟显示");
    lv_label_set_long_mode(bk_ui->screen_saver_clock_en, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_clock_en, 20);
    lv_obj_set_y(bk_ui->screen_saver_clock_en, 300);
    lv_obj_set_width(bk_ui->screen_saver_clock_en, 225);
    lv_obj_set_height(bk_ui->screen_saver_clock_en, 24);
    lv_obj_remove_flag(bk_ui->screen_saver_clock_en, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_clock_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_clock_en, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_clock_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_clock_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_clock_en, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_clock_en, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_clock_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_clock_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_clock_en, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_clock_en, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_clock_en, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_clock_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_clock_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_scene_card = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_scene_card, 335);
    lv_obj_set_y(bk_ui->screen_saver_scene_card, 185);
    lv_obj_set_width(bk_ui->screen_saver_scene_card, 265);
    lv_obj_set_height(bk_ui->screen_saver_scene_card, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_scene_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_scene_card, lv_color_hex(0xf3eee6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_scene_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_scene_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_scene_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_scene_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_scene_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_scene_card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_scene_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_scene_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_scene_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_scene_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_scene_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_scene_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_scene_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_scene_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_scene_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_scene_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_scene_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_scene_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_scene_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_scene_img = lv_image_create(bk_ui->screen_saver_scene_card);
    lv_image_set_src(bk_ui->screen_saver_scene_img, &ss_scenery_card_240x350_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->screen_saver_scene_img, 50, 50);
    lv_image_set_rotation(bk_ui->screen_saver_scene_img, 0);
    lv_obj_set_x(bk_ui->screen_saver_scene_img, 12);
    lv_obj_set_y(bk_ui->screen_saver_scene_img, 0);
    lv_obj_set_width(bk_ui->screen_saver_scene_img, 240);
    lv_obj_set_height(bk_ui->screen_saver_scene_img, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_scene_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_scene_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_scene_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_scene_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_scene_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_scene_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_scene_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_scene_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_scene_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->screen_saver_scene_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->screen_saver_scene_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->screen_saver_scene_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_scene_txt = lv_label_create(bk_ui->screen_saver_scene_card);
    lv_label_set_text(bk_ui->screen_saver_scene_txt, "风景屏保");
    lv_label_set_long_mode(bk_ui->screen_saver_scene_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_scene_txt, 20);
    lv_obj_set_y(bk_ui->screen_saver_scene_txt, 265);
    lv_obj_set_width(bk_ui->screen_saver_scene_txt, 225);
    lv_obj_set_height(bk_ui->screen_saver_scene_txt, 30);
    lv_obj_remove_flag(bk_ui->screen_saver_scene_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_scene_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_scene_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_scene_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_scene_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_scene_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_scene_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_scene_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_scene_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_scene_txt, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_scene_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_scene_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_scene_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_scene_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_scene_en = lv_label_create(bk_ui->screen_saver_scene_card);
    lv_label_set_text(bk_ui->screen_saver_scene_en, "自然风景轮播");
    lv_label_set_long_mode(bk_ui->screen_saver_scene_en, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_scene_en, 20);
    lv_obj_set_y(bk_ui->screen_saver_scene_en, 304);
    lv_obj_set_width(bk_ui->screen_saver_scene_en, 225);
    lv_obj_set_height(bk_ui->screen_saver_scene_en, 24);
    lv_obj_remove_flag(bk_ui->screen_saver_scene_en, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_scene_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_scene_en, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_scene_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_scene_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_scene_en, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_scene_en, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_scene_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_scene_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_scene_en, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_scene_en, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_scene_en, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_scene_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_scene_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_art_card = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_art_card, 645);
    lv_obj_set_y(bk_ui->screen_saver_art_card, 185);
    lv_obj_set_width(bk_ui->screen_saver_art_card, 265);
    lv_obj_set_height(bk_ui->screen_saver_art_card, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_art_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_art_card, lv_color_hex(0x001dbd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_art_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_art_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_art_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_art_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_art_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_art_card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_art_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_art_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_art_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_art_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_art_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_art_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_art_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_art_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_art_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_art_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_art_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_art_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_art_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_art_img = lv_image_create(bk_ui->screen_saver_art_card);
    lv_image_set_src(bk_ui->screen_saver_art_img, &ss_art_card_240x350_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->screen_saver_art_img, 50, 50);
    lv_image_set_rotation(bk_ui->screen_saver_art_img, 0);
    lv_obj_set_x(bk_ui->screen_saver_art_img, 12);
    lv_obj_set_y(bk_ui->screen_saver_art_img, 0);
    lv_obj_set_width(bk_ui->screen_saver_art_img, 240);
    lv_obj_set_height(bk_ui->screen_saver_art_img, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_art_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_art_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_art_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_art_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_art_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_art_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_art_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_art_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_art_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->screen_saver_art_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->screen_saver_art_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->screen_saver_art_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_art_txt = lv_label_create(bk_ui->screen_saver_art_card);
    lv_label_set_text(bk_ui->screen_saver_art_txt, "艺术屏保");
    lv_label_set_long_mode(bk_ui->screen_saver_art_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_art_txt, 20);
    lv_obj_set_y(bk_ui->screen_saver_art_txt, 265);
    lv_obj_set_width(bk_ui->screen_saver_art_txt, 225);
    lv_obj_set_height(bk_ui->screen_saver_art_txt, 30);
    lv_obj_remove_flag(bk_ui->screen_saver_art_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_art_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_art_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_art_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_art_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_art_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_art_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_art_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_art_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_art_txt, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_art_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_art_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_art_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_art_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_art_en = lv_label_create(bk_ui->screen_saver_art_card);
    lv_label_set_text(bk_ui->screen_saver_art_en, "光影艺术主题");
    lv_label_set_long_mode(bk_ui->screen_saver_art_en, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_art_en, 20);
    lv_obj_set_y(bk_ui->screen_saver_art_en, 304);
    lv_obj_set_width(bk_ui->screen_saver_art_en, 225);
    lv_obj_set_height(bk_ui->screen_saver_art_en, 24);
    lv_obj_remove_flag(bk_ui->screen_saver_art_en, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_art_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_art_en, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_art_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_art_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_art_en, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_art_en, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_art_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_art_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_art_en, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_art_en, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_art_en, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_art_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_art_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_photo_card = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_photo_card, 955);
    lv_obj_set_y(bk_ui->screen_saver_photo_card, 185);
    lv_obj_set_width(bk_ui->screen_saver_photo_card, 265);
    lv_obj_set_height(bk_ui->screen_saver_photo_card, 350);
    lv_obj_remove_flag(bk_ui->screen_saver_photo_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_photo_card, lv_color_hex(0x101d4b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_photo_card, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_photo_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_photo_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_photo_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_photo_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_photo_card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_photo_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_photo_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_photo_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_photo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_photo_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_photo_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_photo_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_photo_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_photo_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_photo_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_photo_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_photo_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_photo_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_photo_img = lv_image_create(bk_ui->screen_saver_photo_card);
    lv_image_set_src(bk_ui->screen_saver_photo_img, &icon_photo_add_140x140_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->screen_saver_photo_img, 50, 50);
    lv_image_set_rotation(bk_ui->screen_saver_photo_img, 0);
    lv_obj_set_x(bk_ui->screen_saver_photo_img, 62);
    lv_obj_set_y(bk_ui->screen_saver_photo_img, 88);
    lv_obj_set_width(bk_ui->screen_saver_photo_img, 140);
    lv_obj_set_height(bk_ui->screen_saver_photo_img, 140);
    lv_obj_remove_flag(bk_ui->screen_saver_photo_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_photo_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_photo_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_photo_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_photo_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_photo_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_photo_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_photo_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_photo_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->screen_saver_photo_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->screen_saver_photo_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->screen_saver_photo_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_photo_txt = lv_label_create(bk_ui->screen_saver_photo_card);
    lv_label_set_text(bk_ui->screen_saver_photo_txt, "自定义相册");
    lv_label_set_long_mode(bk_ui->screen_saver_photo_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_photo_txt, 18);
    lv_obj_set_y(bk_ui->screen_saver_photo_txt, 263);
    lv_obj_set_width(bk_ui->screen_saver_photo_txt, 230);
    lv_obj_set_height(bk_ui->screen_saver_photo_txt, 34);
    lv_obj_remove_flag(bk_ui->screen_saver_photo_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_photo_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_photo_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_photo_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_photo_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_photo_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_photo_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_photo_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_photo_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_photo_txt, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_photo_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_photo_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_photo_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_photo_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_photo_en = lv_label_create(bk_ui->screen_saver_photo_card);
    lv_label_set_text(bk_ui->screen_saver_photo_en, "上传照片作为屏保");
    lv_label_set_long_mode(bk_ui->screen_saver_photo_en, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_photo_en, 18);
    lv_obj_set_y(bk_ui->screen_saver_photo_en, 304);
    lv_obj_set_width(bk_ui->screen_saver_photo_en, 230);
    lv_obj_set_height(bk_ui->screen_saver_photo_en, 24);
    lv_obj_remove_flag(bk_ui->screen_saver_photo_en, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_photo_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_photo_en, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_photo_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_photo_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_photo_en, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_photo_en, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_photo_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_photo_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_photo_en, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_photo_en, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_photo_en, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_photo_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_photo_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_btn1 = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_sel_btn1, 118);
    lv_obj_set_y(bk_ui->screen_saver_sel_btn1, 575);
    lv_obj_set_width(bk_ui->screen_saver_sel_btn1, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_btn1, 38);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_btn1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn1, lv_color_hex(0x0b061b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn1, 232, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn1, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn1, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_btn1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_btn1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_btn1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn1, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn1, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn1, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn1, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn1, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn1, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn1, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn1, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn1, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_txt1 = lv_label_create(bk_ui->screen_saver_sel_btn1);
    lv_label_set_text(bk_ui->screen_saver_sel_txt1, "选择");
    lv_label_set_long_mode(bk_ui->screen_saver_sel_txt1, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_sel_txt1, 0);
    lv_obj_set_y(bk_ui->screen_saver_sel_txt1, 9);
    lv_obj_set_width(bk_ui->screen_saver_sel_txt1, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_txt1, 20);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_txt1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_txt1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_txt1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_txt1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_txt1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_txt1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_txt1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_sel_txt1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_sel_txt1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_sel_txt1, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_sel_txt1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_txt1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_txt1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_txt1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_btn2 = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_sel_btn2, 425);
    lv_obj_set_y(bk_ui->screen_saver_sel_btn2, 575);
    lv_obj_set_width(bk_ui->screen_saver_sel_btn2, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_btn2, 38);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_btn2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn2, lv_color_hex(0x0b061b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn2, 232, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn2, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn2, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_btn2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_btn2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_btn2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn2, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn2, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn2, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn2, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn2, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn2, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn2, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn2, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn2, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_txt2 = lv_label_create(bk_ui->screen_saver_sel_btn2);
    lv_label_set_text(bk_ui->screen_saver_sel_txt2, "选择");
    lv_label_set_long_mode(bk_ui->screen_saver_sel_txt2, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_sel_txt2, 0);
    lv_obj_set_y(bk_ui->screen_saver_sel_txt2, 9);
    lv_obj_set_width(bk_ui->screen_saver_sel_txt2, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_txt2, 20);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_txt2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_txt2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_txt2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_txt2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_txt2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_txt2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_txt2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_sel_txt2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_sel_txt2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_sel_txt2, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_sel_txt2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_txt2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_txt2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_txt2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_btn3 = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_sel_btn3, 735);
    lv_obj_set_y(bk_ui->screen_saver_sel_btn3, 575);
    lv_obj_set_width(bk_ui->screen_saver_sel_btn3, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_btn3, 38);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_btn3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn3, lv_color_hex(0x0b061b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn3, 232, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn3, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn3, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_btn3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_btn3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_btn3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn3, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn3, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn3, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn3, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn3, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn3, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn3, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn3, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn3, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_txt3 = lv_label_create(bk_ui->screen_saver_sel_btn3);
    lv_label_set_text(bk_ui->screen_saver_sel_txt3, "选择");
    lv_label_set_long_mode(bk_ui->screen_saver_sel_txt3, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_sel_txt3, 0);
    lv_obj_set_y(bk_ui->screen_saver_sel_txt3, 9);
    lv_obj_set_width(bk_ui->screen_saver_sel_txt3, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_txt3, 20);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_txt3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_txt3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_txt3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_txt3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_txt3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_txt3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_txt3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_sel_txt3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_sel_txt3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_sel_txt3, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_sel_txt3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_txt3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_txt3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_txt3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_btn4 = lv_obj_create(bk_ui->screen_saver_ss_bg);
    lv_obj_set_x(bk_ui->screen_saver_sel_btn4, 1045);
    lv_obj_set_y(bk_ui->screen_saver_sel_btn4, 575);
    lv_obj_set_width(bk_ui->screen_saver_sel_btn4, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_btn4, 38);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_btn4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn4, lv_color_hex(0x0b061b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn4, 232, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn4, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn4, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_btn4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_btn4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_btn4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_btn4, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_btn4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_btn4, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_btn4, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_btn4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_btn4, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_btn4, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_btn4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_btn4, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->screen_saver_sel_txt4 = lv_label_create(bk_ui->screen_saver_sel_btn4);
    lv_label_set_text(bk_ui->screen_saver_sel_txt4, "选择");
    lv_label_set_long_mode(bk_ui->screen_saver_sel_txt4, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->screen_saver_sel_txt4, 0);
    lv_obj_set_y(bk_ui->screen_saver_sel_txt4, 9);
    lv_obj_set_width(bk_ui->screen_saver_sel_txt4, 86);
    lv_obj_set_height(bk_ui->screen_saver_sel_txt4, 20);
    lv_obj_remove_flag(bk_ui->screen_saver_sel_txt4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->screen_saver_sel_txt4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->screen_saver_sel_txt4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->screen_saver_sel_txt4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->screen_saver_sel_txt4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->screen_saver_sel_txt4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->screen_saver_sel_txt4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->screen_saver_sel_txt4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->screen_saver_sel_txt4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->screen_saver_sel_txt4, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->screen_saver_sel_txt4, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->screen_saver_sel_txt4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->screen_saver_sel_txt4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->screen_saver_sel_txt4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->screen_saver);
}

/*
 * @brief: destroy page screen_saver
 */
void destroy_page_screen_saver(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->screen_saver != NULL) {
        lv_obj_del(bk_ui->screen_saver);
        bk_ui->screen_saver = NULL;
    }
}