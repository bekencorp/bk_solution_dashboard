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
 * @brief: init page nav_cast
 */
void init_page_nav_cast(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->nav_cast != NULL && lv_obj_is_valid(bk_ui->nav_cast)) {
        destroy_page_nav_cast(bk_ui);
    }
    

    bk_ui->nav_cast = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->nav_cast, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->nav_cast, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->nav_cast, lv_color_hex(0x06080e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_bg_img = lv_image_create(bk_ui->nav_cast);
    lv_image_set_src(bk_ui->nav_cast_bg_img, &nav_bg_tech_1280x720_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_bg_img, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_bg_img, 0);
    lv_obj_set_x(bk_ui->nav_cast_bg_img, 0);
    lv_obj_set_y(bk_ui->nav_cast_bg_img, 0);
    lv_obj_set_width(bk_ui->nav_cast_bg_img, 1280);
    lv_obj_set_height(bk_ui->nav_cast_bg_img, 720);
    lv_obj_remove_flag(bk_ui->nav_cast_bg_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_bg_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_bg_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_bg_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_bg_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_bg_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_bg_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_bg_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_top_bar = lv_obj_create(bk_ui->nav_cast);
    lv_obj_set_x(bk_ui->nav_cast_top_bar, 31);
    lv_obj_set_y(bk_ui->nav_cast_top_bar, 22);
    lv_obj_set_width(bk_ui->nav_cast_top_bar, 1220);
    lv_obj_set_height(bk_ui->nav_cast_top_bar, 62);
    lv_obj_remove_flag(bk_ui->nav_cast_top_bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_top_bar, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_top_bar, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_top_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_top_bar, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_top_bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_top_bar, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_top_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_top_bar, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_top_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_top_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_top_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_dash_title = lv_label_create(bk_ui->nav_cast_top_bar);
    lv_label_set_text(bk_ui->nav_cast_dash_title, "NAV CAST");
    lv_label_set_long_mode(bk_ui->nav_cast_dash_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_dash_title, 25);
    lv_obj_set_y(bk_ui->nav_cast_dash_title, 12);
    lv_obj_set_width(bk_ui->nav_cast_dash_title, 285);
    lv_obj_set_height(bk_ui->nav_cast_dash_title, 38);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_dash_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_dash_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_dash_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_dash_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_dash_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_dash_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_dash_title, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_dash_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_dash_title, &lv_font_pingfang_SC_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_dash_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_dash_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_dash_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_dash_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_time_clk = lv_label_create(bk_ui->nav_cast_top_bar);
    lv_label_set_text(bk_ui->nav_cast_time_clk, "");
    lv_digital_clock_register(bk_ui->nav_cast_time_clk, 0, 0, 15, 29, 0);
    lv_obj_set_x(bk_ui->nav_cast_time_clk, 1015);
    lv_obj_set_y(bk_ui->nav_cast_time_clk, 12);
    lv_obj_set_width(bk_ui->nav_cast_time_clk, 175);
    lv_obj_set_height(bk_ui->nav_cast_time_clk, 38);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_time_clk, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_time_clk, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_time_clk, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_time_clk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_time_clk, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_time_clk, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_time_clk, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_time_clk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_time_clk, &lv_font_montserrat_regular_22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_time_clk, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_time_clk, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_time_clk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_icons_panel = lv_obj_create(bk_ui->nav_cast_top_bar);
    lv_obj_set_flex_flow(bk_ui->nav_cast_icons_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bk_ui->nav_cast_icons_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_x(bk_ui->nav_cast_icons_panel, 360);
    lv_obj_set_y(bk_ui->nav_cast_icons_panel, 14);
    lv_obj_set_width(bk_ui->nav_cast_icons_panel, 500);
    lv_obj_set_height(bk_ui->nav_cast_icons_panel, 34);
    lv_obj_remove_flag(bk_ui->nav_cast_icons_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_icons_panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_icons_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_icons_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_icons_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_icons_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_icons_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_icons_panel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_icons_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_icons_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_left = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_left, &nav_ic_turn_l_copy2_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_left, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_left, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_left, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_left, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_left, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_left, 31);
    lv_obj_remove_flag(bk_ui->nav_cast_ic_left, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_left, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_left, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_left, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_left, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_left, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_left, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_left, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_left, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_left, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_left, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_left, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_right = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_right, &nav_ic_turn_r_copy2_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_right, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_right, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_right, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_right, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_right, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_right, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_right, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_right, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_right, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_right, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_right, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_right, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_right, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_right, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_right, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_right, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_abs = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_abs, &nav_ic_abs_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_abs, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_abs, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_abs, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_abs, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_abs, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_abs, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_abs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_abs, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_abs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_abs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_abs, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_abs, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_abs, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_abs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_abs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_abs, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_tcs = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_tcs, &nav_ic_tcs_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_tcs, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_tcs, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_tcs, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_tcs, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_tcs, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_tcs, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_tcs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_tcs, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_tcs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_tcs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_tcs, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_tcs, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_tcs, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_tcs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_tcs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_tcs, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_gps = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_gps, &nav_ic_gps_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_gps, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_gps, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_gps, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_gps, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_gps, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_gps, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_gps, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_gps, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_gps, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_gps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_gps, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_gps, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_gps, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_gps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_gps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_gps, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_bt = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_bt, &nav_ic_bt_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_bt, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_bt, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_bt, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_bt, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_bt, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_bt, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_bt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_bt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_bt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_bt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_bt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_bt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_bt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_bt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_bt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_bt, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_light = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_light, &nav_ic_light_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_light, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_light, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_light, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_light, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_light, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_light, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_light, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_light, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_light, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_light, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_light, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_light, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_light, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_light, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_light, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_light, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_eng = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_eng, &nav_ic_eng_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_eng, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_eng, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_eng, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_eng, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_eng, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_eng, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_eng, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_eng, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_eng, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_eng, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_eng, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_eng, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_eng, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_eng, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_eng, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_eng, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_warn = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_warn, &nav_ic_warn_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_warn, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_warn, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_warn, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_warn, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_warn, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_warn, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_warn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_warn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_warn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_warn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_warn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_warn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_warn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_warn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_warn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_warn, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_batt = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_batt, &nav_ic_batt_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_batt, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_batt, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_batt, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_batt, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_batt, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_batt, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_batt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_batt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_batt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_batt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_batt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_batt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_batt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_batt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_batt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_batt, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_tire = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_tire, &nav_ic_tire_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_tire, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_tire, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_tire, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_tire, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_tire, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_tire, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_tire, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_tire, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_tire, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_tire, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_tire, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_tire, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_tire, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_tire, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_tire, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_tire, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ic_temp = lv_image_create(bk_ui->nav_cast_icons_panel);
    lv_image_set_src(bk_ui->nav_cast_ic_temp, &nav_ic_temp_copy_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->nav_cast_ic_temp, 50, 50);
    lv_image_set_rotation(bk_ui->nav_cast_ic_temp, 0);
    lv_obj_set_x(bk_ui->nav_cast_ic_temp, 0);
    lv_obj_set_y(bk_ui->nav_cast_ic_temp, 0);
    lv_obj_set_width(bk_ui->nav_cast_ic_temp, 33);
    lv_obj_set_height(bk_ui->nav_cast_ic_temp, 31);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ic_temp, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ic_temp, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ic_temp, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ic_temp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ic_temp, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ic_temp, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ic_temp, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ic_temp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->nav_cast_ic_temp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->nav_cast_ic_temp, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->nav_cast_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_map_panel = lv_obj_create(bk_ui->nav_cast);
    lv_obj_set_x(bk_ui->nav_cast_map_panel, 290);
    lv_obj_set_y(bk_ui->nav_cast_map_panel, 98);
    lv_obj_set_width(bk_ui->nav_cast_map_panel, 700);
    lv_obj_set_height(bk_ui->nav_cast_map_panel, 593);
    lv_obj_remove_flag(bk_ui->nav_cast_map_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_map_panel, lv_color_hex(0xc81e2d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_map_panel, 36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_map_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_map_panel, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_map_panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_map_panel, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_map_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_map_panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_map_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_map_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_map_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_map_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_map_hint = lv_label_create(bk_ui->nav_cast_map_panel);
    lv_label_set_text(bk_ui->nav_cast_map_hint, "MAP AREA");
    lv_label_set_long_mode(bk_ui->nav_cast_map_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_map_hint, 250);
    lv_obj_set_y(bk_ui->nav_cast_map_hint, 284);
    lv_obj_set_width(bk_ui->nav_cast_map_hint, 200);
    lv_obj_set_height(bk_ui->nav_cast_map_hint, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_map_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_map_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_map_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_map_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_map_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_map_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_map_hint, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_map_hint, 136, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_map_hint, &lv_font_montserrat_regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_map_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_map_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_map_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_map_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_side_panel = lv_obj_create(bk_ui->nav_cast);
    lv_obj_set_flex_flow(bk_ui->nav_cast_side_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bk_ui->nav_cast_side_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_x(bk_ui->nav_cast_side_panel, 1000);
    lv_obj_set_y(bk_ui->nav_cast_side_panel, 98);
    lv_obj_set_width(bk_ui->nav_cast_side_panel, 250);
    lv_obj_set_height(bk_ui->nav_cast_side_panel, 593);
    lv_obj_remove_flag(bk_ui->nav_cast_side_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_side_panel, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_side_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_side_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_side_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_side_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_side_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_side_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_side_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_side_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_eta_card = lv_obj_create(bk_ui->nav_cast_side_panel);
    lv_obj_set_x(bk_ui->nav_cast_eta_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_eta_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_eta_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_eta_card, 198);
    lv_obj_remove_flag(bk_ui->nav_cast_eta_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_eta_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_eta_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_eta_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_eta_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_eta_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_eta_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_eta_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_eta_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_eta_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_eta_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_eta_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_eta_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_eta_title = lv_label_create(bk_ui->nav_cast_eta_card);
    lv_label_set_text(bk_ui->nav_cast_eta_title, "NAVIGATION");
    lv_label_set_long_mode(bk_ui->nav_cast_eta_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_eta_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_eta_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_eta_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_eta_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_eta_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_eta_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_eta_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_eta_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_eta_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_eta_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_eta_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_eta_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_eta_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_eta_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_eta_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_eta_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_eta_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_eta_value = lv_label_create(bk_ui->nav_cast_eta_card);
    lv_label_set_text(bk_ui->nav_cast_eta_value, "8 min");
    lv_label_set_long_mode(bk_ui->nav_cast_eta_value, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_eta_value, 20);
    lv_obj_set_y(bk_ui->nav_cast_eta_value, 46);
    lv_obj_set_width(bk_ui->nav_cast_eta_value, 210);
    lv_obj_set_height(bk_ui->nav_cast_eta_value, 46);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_eta_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_eta_value, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_eta_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_eta_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_eta_value, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_eta_value, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_eta_value, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_eta_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_eta_value, &lv_font_pingfang_SC_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_eta_value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_eta_value, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_eta_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_eta_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_nav_state = lv_label_create(bk_ui->nav_cast_eta_card);
    lv_label_set_text(bk_ui->nav_cast_nav_state, "2.8 km left");
    lv_label_set_long_mode(bk_ui->nav_cast_nav_state, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_nav_state, 20);
    lv_obj_set_y(bk_ui->nav_cast_nav_state, 142);
    lv_obj_set_width(bk_ui->nav_cast_nav_state, 210);
    lv_obj_set_height(bk_ui->nav_cast_nav_state, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_nav_state, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_nav_state, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_nav_state, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_nav_state, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_nav_state, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_nav_state, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_nav_state, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_nav_state, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_nav_state, &lv_font_pingfang_SC_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_nav_state, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_nav_state, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_nav_state, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_nav_state, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_sys_card = lv_obj_create(bk_ui->nav_cast_side_panel);
    lv_obj_set_x(bk_ui->nav_cast_sys_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_sys_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_sys_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_sys_card, 198);
    lv_obj_remove_flag(bk_ui->nav_cast_sys_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_sys_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_sys_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_sys_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_sys_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_sys_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_sys_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_sys_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_sys_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_sys_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_sys_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_sys_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_sys_title = lv_label_create(bk_ui->nav_cast_sys_card);
    lv_label_set_text(bk_ui->nav_cast_sys_title, "SYSTEM");
    lv_label_set_long_mode(bk_ui->nav_cast_sys_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_sys_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_sys_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_sys_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_sys_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_sys_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_sys_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_sys_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_sys_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_sys_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_sys_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_sys_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_sys_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_sys_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_sys_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_sys_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_sys_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_volt_lbl = lv_label_create(bk_ui->nav_cast_sys_card);
    lv_label_set_text(bk_ui->nav_cast_volt_lbl, "48.2V");
    lv_label_set_long_mode(bk_ui->nav_cast_volt_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_volt_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_volt_lbl, 48);
    lv_obj_set_width(bk_ui->nav_cast_volt_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_volt_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_volt_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_volt_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_volt_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_volt_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_volt_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_volt_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_volt_lbl, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_volt_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_volt_lbl, &lv_font_pingfang_SC_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_volt_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_volt_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_volt_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_volt_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_tire_lbl = lv_label_create(bk_ui->nav_cast_sys_card);
    lv_label_set_text(bk_ui->nav_cast_tire_lbl, "F 2.4 / R 2.5 bar");
    lv_label_set_long_mode(bk_ui->nav_cast_tire_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_tire_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_tire_lbl, 86);
    lv_obj_set_width(bk_ui->nav_cast_tire_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_tire_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_tire_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_tire_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_tire_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_tire_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_tire_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_tire_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_tire_lbl, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_tire_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_tire_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_tire_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_tire_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_tire_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_tire_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_temp_lbl = lv_label_create(bk_ui->nav_cast_sys_card);
    lv_label_set_text(bk_ui->nav_cast_temp_lbl, "M 38C");
    lv_label_set_long_mode(bk_ui->nav_cast_temp_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_temp_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_temp_lbl, 125);
    lv_obj_set_width(bk_ui->nav_cast_temp_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_temp_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_temp_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_temp_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_temp_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_temp_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_temp_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_temp_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_temp_lbl, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_temp_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_temp_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_temp_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_temp_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_temp_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_temp_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_brake_lbl = lv_label_create(bk_ui->nav_cast_sys_card);
    lv_label_set_text(bk_ui->nav_cast_brake_lbl, "ABS OK");
    lv_label_set_long_mode(bk_ui->nav_cast_brake_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_brake_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_brake_lbl, 163);
    lv_obj_set_width(bk_ui->nav_cast_brake_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_brake_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_brake_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_brake_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_brake_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_brake_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_brake_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_brake_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_brake_lbl, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_brake_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_brake_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_brake_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_brake_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_brake_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_brake_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_stat_card = lv_obj_create(bk_ui->nav_cast_side_panel);
    lv_obj_set_x(bk_ui->nav_cast_stat_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_stat_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_stat_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_stat_card, 196);
    lv_obj_remove_flag(bk_ui->nav_cast_stat_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_stat_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_stat_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_stat_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_stat_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_stat_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_stat_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_stat_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_stat_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_stat_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_stat_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_stat_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_stat_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_stat_title = lv_label_create(bk_ui->nav_cast_stat_card);
    lv_label_set_text(bk_ui->nav_cast_stat_title, "STATS");
    lv_label_set_long_mode(bk_ui->nav_cast_stat_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_stat_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_stat_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_stat_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_stat_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_stat_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_stat_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_stat_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_stat_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_stat_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_stat_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_stat_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_stat_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_stat_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_stat_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_stat_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_stat_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_stat_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_avg_lbl = lv_label_create(bk_ui->nav_cast_stat_card);
    lv_label_set_text(bk_ui->nav_cast_avg_lbl, "AVG 15 Wh/km");
    lv_label_set_long_mode(bk_ui->nav_cast_avg_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_avg_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_avg_lbl, 48);
    lv_obj_set_width(bk_ui->nav_cast_avg_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_avg_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_avg_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_avg_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_avg_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_avg_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_avg_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_avg_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_avg_lbl, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_avg_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_avg_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_avg_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_avg_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_avg_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_avg_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_ride_lbl = lv_label_create(bk_ui->nav_cast_stat_card);
    lv_label_set_text(bk_ui->nav_cast_ride_lbl, "RIDE 00:32:18");
    lv_label_set_long_mode(bk_ui->nav_cast_ride_lbl, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_ride_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_ride_lbl, 91);
    lv_obj_set_width(bk_ui->nav_cast_ride_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_ride_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_ride_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_ride_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_ride_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_ride_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_ride_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_ride_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_ride_lbl, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_ride_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_ride_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_ride_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_ride_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_ride_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_ride_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_gps_lbl = lv_label_create(bk_ui->nav_cast_stat_card);
    lv_label_set_text(bk_ui->nav_cast_gps_lbl, "GPS LOCKED");
    lv_label_set_long_mode(bk_ui->nav_cast_gps_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_gps_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_gps_lbl, 134);
    lv_obj_set_width(bk_ui->nav_cast_gps_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_gps_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_gps_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_gps_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_gps_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_gps_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_gps_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_gps_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_gps_lbl, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_gps_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_gps_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_gps_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_gps_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_gps_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_gps_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_left_panel = lv_obj_create(bk_ui->nav_cast);
    lv_obj_set_flex_flow(bk_ui->nav_cast_left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bk_ui->nav_cast_left_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_x(bk_ui->nav_cast_left_panel, 30);
    lv_obj_set_y(bk_ui->nav_cast_left_panel, 98);
    lv_obj_set_width(bk_ui->nav_cast_left_panel, 250);
    lv_obj_set_height(bk_ui->nav_cast_left_panel, 593);
    lv_obj_remove_flag(bk_ui->nav_cast_left_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_left_panel, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_left_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_left_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_left_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_left_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_left_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_left_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_left_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_left_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_speed_card = lv_obj_create(bk_ui->nav_cast_left_panel);
    lv_obj_set_x(bk_ui->nav_cast_speed_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_speed_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_speed_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_speed_card, 146);
    lv_obj_remove_flag(bk_ui->nav_cast_speed_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_speed_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_speed_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_speed_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_speed_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_speed_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_speed_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_speed_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_speed_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_speed_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_speed_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_speed_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_speed_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_spd_title = lv_label_create(bk_ui->nav_cast_speed_card);
    lv_label_set_text(bk_ui->nav_cast_spd_title, "SPEED");
    lv_label_set_long_mode(bk_ui->nav_cast_spd_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_spd_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_spd_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_spd_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_spd_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_spd_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_spd_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_spd_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_spd_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_spd_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_spd_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_spd_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_spd_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_spd_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_spd_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_spd_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_spd_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_spd_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_speed_span = lv_spangroup_create(bk_ui->nav_cast_speed_card);
    lv_obj_set_x(bk_ui->nav_cast_speed_span, 20);
    lv_obj_set_y(bk_ui->nav_cast_speed_span, 43);
    lv_obj_set_width(bk_ui->nav_cast_speed_span, 210);
    lv_obj_set_height(bk_ui->nav_cast_speed_span, 43);
    lv_obj_remove_flag(bk_ui->nav_cast_speed_span, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    bk_ui->nav_cast_speed_span_span_0 = lv_spangroup_new_span(bk_ui->nav_cast_speed_span);
    lv_span_set_text(bk_ui->nav_cast_speed_span_span_0, "28");
    lv_style_set_text_color(lv_span_get_style(bk_ui->nav_cast_speed_span_span_0), lv_color_hex(0xFFFFFF));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->nav_cast_speed_span_span_0), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->nav_cast_speed_span_span_0), &lv_font_pingfang_SC_28);
    bk_ui->nav_cast_speed_span_span_1 = lv_spangroup_new_span(bk_ui->nav_cast_speed_span);
    lv_span_set_text(bk_ui->nav_cast_speed_span_span_1, " km/h");
    lv_style_set_text_color(lv_span_get_style(bk_ui->nav_cast_speed_span_span_1), lv_color_hex(0xC0C8D0));
    lv_style_set_text_decor(lv_span_get_style(bk_ui->nav_cast_speed_span_span_1), LV_TEXT_DECOR_NONE);
    lv_style_set_text_font(lv_span_get_style(bk_ui->nav_cast_speed_span_span_1), &lv_font_pingfang_SC_14);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_speed_span, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_speed_span, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_speed_span, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_speed_span, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_speed_span, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_speed_span, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_speed_span, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_speed_span, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_speed_span, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_mode_lbl = lv_label_create(bk_ui->nav_cast_speed_card);
    lv_label_set_text(bk_ui->nav_cast_mode_lbl, "SPORT");
    lv_label_set_long_mode(bk_ui->nav_cast_mode_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_mode_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_mode_lbl, 110);
    lv_obj_set_width(bk_ui->nav_cast_mode_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_mode_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_mode_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_mode_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_mode_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_mode_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_mode_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_mode_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_mode_lbl, lv_color_hex(0xFF9A40), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_mode_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_mode_lbl, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_mode_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_mode_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_mode_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_mode_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_mile_card = lv_obj_create(bk_ui->nav_cast_left_panel);
    lv_obj_set_x(bk_ui->nav_cast_mile_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_mile_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_mile_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_mile_card, 146);
    lv_obj_remove_flag(bk_ui->nav_cast_mile_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_mile_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_mile_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_mile_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_mile_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_mile_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_mile_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_mile_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_mile_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_mile_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_mile_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_mile_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_mile_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_mile_title = lv_label_create(bk_ui->nav_cast_mile_card);
    lv_label_set_text(bk_ui->nav_cast_mile_title, "RANGE");
    lv_label_set_long_mode(bk_ui->nav_cast_mile_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_mile_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_mile_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_mile_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_mile_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_mile_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_mile_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_mile_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_mile_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_mile_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_mile_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_mile_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_mile_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_mile_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_mile_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_mile_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_mile_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_mile_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_mile_value = lv_label_create(bk_ui->nav_cast_mile_card);
    lv_label_set_text(bk_ui->nav_cast_mile_value, "58 km");
    lv_label_set_long_mode(bk_ui->nav_cast_mile_value, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_mile_value, 20);
    lv_obj_set_y(bk_ui->nav_cast_mile_value, 46);
    lv_obj_set_width(bk_ui->nav_cast_mile_value, 210);
    lv_obj_set_height(bk_ui->nav_cast_mile_value, 43);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_mile_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_mile_value, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_mile_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_mile_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_mile_value, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_mile_value, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_mile_value, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_mile_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_mile_value, &lv_font_pingfang_SC_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_mile_value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_mile_value, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_mile_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_mile_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_mile_hint = lv_label_create(bk_ui->nav_cast_mile_card);
    lv_label_set_text(bk_ui->nav_cast_mile_hint, "SPORT MODE");
    lv_label_set_long_mode(bk_ui->nav_cast_mile_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_mile_hint, 20);
    lv_obj_set_y(bk_ui->nav_cast_mile_hint, 110);
    lv_obj_set_width(bk_ui->nav_cast_mile_hint, 210);
    lv_obj_set_height(bk_ui->nav_cast_mile_hint, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_mile_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_mile_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_mile_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_mile_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_mile_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_mile_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_mile_hint, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_mile_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_mile_hint, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_mile_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_mile_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_mile_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_mile_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_card = lv_obj_create(bk_ui->nav_cast_left_panel);
    lv_obj_set_x(bk_ui->nav_cast_batt_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_batt_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_batt_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_batt_card, 146);
    lv_obj_remove_flag(bk_ui->nav_cast_batt_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_title = lv_label_create(bk_ui->nav_cast_batt_card);
    lv_label_set_text(bk_ui->nav_cast_batt_title, "BATTERY");
    lv_label_set_long_mode(bk_ui->nav_cast_batt_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_batt_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_batt_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_batt_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_batt_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_batt_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_batt_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_batt_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_batt_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg01 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg01, 20);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg01, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg01, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg01, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg01, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg01, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg01, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg01, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg01, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg01, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg01, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg01, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg01, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg01, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg02 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg02, 41);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg02, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg02, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg02, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg02, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg02, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg02, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg02, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg02, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg02, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg02, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg02, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg02, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg02, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg03 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg03, 63);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg03, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg03, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg03, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg03, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg03, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg03, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg03, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg03, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg03, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg03, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg03, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg03, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg03, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg04 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg04, 84);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg04, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg04, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg04, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg04, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg04, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg04, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg04, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg04, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg04, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg04, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg04, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg04, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg04, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg05 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg05, 105);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg05, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg05, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg05, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg05, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg05, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg05, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg05, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg05, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg05, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg05, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg05, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg05, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg05, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg06 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg06, 126);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg06, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg06, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg06, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg06, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg06, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg06, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg06, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg06, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg06, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg06, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg06, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg06, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg06, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg07 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg07, 148);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg07, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg07, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg07, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg07, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg07, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg07, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg07, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg07, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg07, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg07, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg07, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg07, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg07, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg08 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg08, 169);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg08, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg08, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg08, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg08, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg08, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg08, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg08, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg08, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg08, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg08, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg08, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg08, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg08, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg09 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg09, 190);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg09, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg09, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg09, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg09, lv_color_hex(0x223344), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg09, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg09, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg09, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg09, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg09, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg09, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg09, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg09, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg09, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg09, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_batt_seg10 = lv_obj_create(bk_ui->nav_cast_batt_card);
    lv_obj_set_x(bk_ui->nav_cast_batt_seg10, 211);
    lv_obj_set_y(bk_ui->nav_cast_batt_seg10, 48);
    lv_obj_set_width(bk_ui->nav_cast_batt_seg10, 19);
    lv_obj_set_height(bk_ui->nav_cast_batt_seg10, 29);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_batt_seg10, lv_color_hex(0x223344), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_batt_seg10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_batt_seg10, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_batt_seg10, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_batt_seg10, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_batt_seg10, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_batt_seg10, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_batt_seg10, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_batt_seg10, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_batt_seg10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_batt_seg10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_assist_lbl = lv_label_create(bk_ui->nav_cast_batt_card);
    lv_label_set_text(bk_ui->nav_cast_assist_lbl, "ASSIST 68%");
    lv_label_set_long_mode(bk_ui->nav_cast_assist_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_assist_lbl, 20);
    lv_obj_set_y(bk_ui->nav_cast_assist_lbl, 113);
    lv_obj_set_width(bk_ui->nav_cast_assist_lbl, 210);
    lv_obj_set_height(bk_ui->nav_cast_assist_lbl, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_assist_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_assist_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_assist_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_assist_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_assist_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_assist_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_assist_lbl, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_assist_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_assist_lbl, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_assist_lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_assist_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_assist_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_assist_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_odo_card = lv_obj_create(bk_ui->nav_cast_left_panel);
    lv_obj_set_x(bk_ui->nav_cast_odo_card, 0);
    lv_obj_set_y(bk_ui->nav_cast_odo_card, 0);
    lv_obj_set_width(bk_ui->nav_cast_odo_card, 248);
    lv_obj_set_height(bk_ui->nav_cast_odo_card, 146);
    lv_obj_remove_flag(bk_ui->nav_cast_odo_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_odo_card, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_odo_card, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_odo_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_odo_card, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_odo_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_odo_card, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_odo_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_odo_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_odo_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_odo_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_odo_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_odo_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_odo_title = lv_label_create(bk_ui->nav_cast_odo_card);
    lv_label_set_text(bk_ui->nav_cast_odo_title, "ODOMETER");
    lv_label_set_long_mode(bk_ui->nav_cast_odo_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->nav_cast_odo_title, 20);
    lv_obj_set_y(bk_ui->nav_cast_odo_title, 14);
    lv_obj_set_width(bk_ui->nav_cast_odo_title, 210);
    lv_obj_set_height(bk_ui->nav_cast_odo_title, 24);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_odo_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_odo_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_odo_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_odo_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_odo_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_odo_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_odo_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_odo_title, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_odo_title, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_odo_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_odo_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_odo_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_odo_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_odo_value = lv_label_create(bk_ui->nav_cast_odo_card);
    lv_label_set_text(bk_ui->nav_cast_odo_value, "ODO 3286 km");
    lv_label_set_long_mode(bk_ui->nav_cast_odo_value, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_odo_value, 20);
    lv_obj_set_y(bk_ui->nav_cast_odo_value, 41);
    lv_obj_set_width(bk_ui->nav_cast_odo_value, 210);
    lv_obj_set_height(bk_ui->nav_cast_odo_value, 26);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_odo_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_odo_value, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_odo_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_odo_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_odo_value, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_odo_value, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_odo_value, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_odo_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_odo_value, &lv_font_pingfang_SC_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_odo_value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_odo_value, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_odo_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_odo_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->nav_cast_trip_value = lv_label_create(bk_ui->nav_cast_odo_card);
    lv_label_set_text(bk_ui->nav_cast_trip_value, "TRIP 12.6 km");
    lv_label_set_long_mode(bk_ui->nav_cast_trip_value, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->nav_cast_trip_value, 20);
    lv_obj_set_y(bk_ui->nav_cast_trip_value, 103);
    lv_obj_set_width(bk_ui->nav_cast_trip_value, 210);
    lv_obj_set_height(bk_ui->nav_cast_trip_value, 26);
    lv_obj_set_style_bg_color(bk_ui->nav_cast_trip_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->nav_cast_trip_value, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->nav_cast_trip_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->nav_cast_trip_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->nav_cast_trip_value, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->nav_cast_trip_value, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->nav_cast_trip_value, lv_color_hex(0xC0C8D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->nav_cast_trip_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->nav_cast_trip_value, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->nav_cast_trip_value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->nav_cast_trip_value, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->nav_cast_trip_value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->nav_cast_trip_value, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->nav_cast);
}

/*
 * @brief: destroy page nav_cast
 */
void destroy_page_nav_cast(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->nav_cast != NULL) {
        lv_obj_del(bk_ui->nav_cast);
        bk_ui->nav_cast = NULL;
    }
}