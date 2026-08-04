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
 * @brief: init page dashcam
 */
void init_page_dashcam(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->dashcam != NULL && lv_obj_is_valid(bk_ui->dashcam)) {
        destroy_page_dashcam(bk_ui);
    }
    

    bk_ui->dashcam = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->dashcam, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->dashcam, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->dashcam, lv_color_hex(0x06080e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_bg_img = lv_image_create(bk_ui->dashcam);
    lv_image_set_src(bk_ui->dashcam_bg_img, NULL);
    lv_image_set_pivot(bk_ui->dashcam_bg_img, 50, 50);
    lv_image_set_rotation(bk_ui->dashcam_bg_img, 0);
    lv_obj_set_x(bk_ui->dashcam_bg_img, -1);
    lv_obj_set_y(bk_ui->dashcam_bg_img, 0);
    lv_obj_set_width(bk_ui->dashcam_bg_img, 1280);
    lv_obj_set_height(bk_ui->dashcam_bg_img, 720);
    lv_obj_remove_flag(bk_ui->dashcam_bg_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->dashcam_bg_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_bg_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_bg_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_bg_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_bg_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_bg_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->dashcam_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->dashcam_bg_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->dashcam_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_bottom_bar = lv_obj_create(bk_ui->dashcam);
    lv_obj_set_x(bk_ui->dashcam_bottom_bar, 24);
    lv_obj_set_y(bk_ui->dashcam_bottom_bar, 663);
    lv_obj_set_width(bk_ui->dashcam_bottom_bar, 970);
    lv_obj_set_height(bk_ui->dashcam_bottom_bar, 32);
    lv_obj_remove_flag(bk_ui->dashcam_bottom_bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->dashcam_bottom_bar, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_bottom_bar, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->dashcam_bottom_bar, lv_color_hex(0x142840), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_bottom_bar, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->dashcam_bottom_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->dashcam_bottom_bar, 96, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_bottom_bar, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_bottom_bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_bottom_bar, 48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_bottom_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_bottom_bar, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_bottom_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_bottom_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_bottom_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_ctrl_info_row = lv_obj_create(bk_ui->dashcam_bottom_bar);
    lv_obj_set_x(bk_ui->dashcam_ctrl_info_row, 18);
    lv_obj_set_y(bk_ui->dashcam_ctrl_info_row, 4);
    lv_obj_set_width(bk_ui->dashcam_ctrl_info_row, 934);
    lv_obj_set_height(bk_ui->dashcam_ctrl_info_row, 24);
    lv_obj_remove_flag(bk_ui->dashcam_ctrl_info_row, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->dashcam_ctrl_info_row, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_ctrl_info_row, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_ctrl_info_row, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_ctrl_info_row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_ctrl_info_row, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_ctrl_info_row, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_ctrl_info_row, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_ctrl_info_row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_ctrl_info_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_dur_start = lv_label_create(bk_ui->dashcam_ctrl_info_row);
    lv_label_set_text(bk_ui->dashcam_dur_start, "01:24");
    lv_label_set_long_mode(bk_ui->dashcam_dur_start, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_dur_start, 0);
    lv_obj_set_y(bk_ui->dashcam_dur_start, 2);
    lv_obj_set_width(bk_ui->dashcam_dur_start, 63);
    lv_obj_set_height(bk_ui->dashcam_dur_start, 20);
    lv_obj_set_style_bg_color(bk_ui->dashcam_dur_start, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_dur_start, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_dur_start, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_dur_start, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_dur_start, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_dur_start, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_dur_start, lv_color_hex(0x6A8094), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_dur_start, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_dur_start, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_dur_start, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_dur_start, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_dur_start, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_dur_start, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_dur_end = lv_label_create(bk_ui->dashcam_ctrl_info_row);
    lv_label_set_text(bk_ui->dashcam_dur_end, "05:30");
    lv_label_set_long_mode(bk_ui->dashcam_dur_end, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_dur_end, 871);
    lv_obj_set_y(bk_ui->dashcam_dur_end, 2);
    lv_obj_set_width(bk_ui->dashcam_dur_end, 63);
    lv_obj_set_height(bk_ui->dashcam_dur_end, 20);
    lv_obj_set_style_bg_color(bk_ui->dashcam_dur_end, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_dur_end, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_dur_end, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_dur_end, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_dur_end, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_dur_end, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_dur_end, lv_color_hex(0x6A8094), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_dur_end, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_dur_end, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_dur_end, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_dur_end, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_dur_end, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_dur_end, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_time_bar = lv_bar_create(bk_ui->dashcam_ctrl_info_row);
    lv_bar_set_range(bk_ui->dashcam_time_bar, 0, 100);
    lv_obj_set_style_anim_duration(bk_ui->dashcam_time_bar, 1000, 0);
    lv_bar_set_start_value(bk_ui->dashcam_time_bar, 0, LV_ANIM_OFF);
    lv_bar_set_value(bk_ui->dashcam_time_bar, 28, LV_ANIM_OFF);
    lv_bar_set_mode(bk_ui->dashcam_time_bar, LV_BAR_MODE_NORMAL);
    lv_obj_set_x(bk_ui->dashcam_time_bar, 68);
    lv_obj_set_y(bk_ui->dashcam_time_bar, 7);
    lv_obj_set_width(bk_ui->dashcam_time_bar, 798);
    lv_obj_set_height(bk_ui->dashcam_time_bar, 10);
    lv_obj_set_style_anim_duration(bk_ui->dashcam_time_bar, 1000, 0);
    lv_obj_set_style_bg_color(bk_ui->dashcam_time_bar, lv_color_hex(0x1A2838), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_time_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_time_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_time_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_time_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_time_bar, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_time_bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_time_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_time_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_time_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_time_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_time_bar, lv_color_hex(0x1EF2C4), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_time_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->dashcam_time_bar, lv_color_hex(0x7ee8ff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_time_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->dashcam_time_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->dashcam_time_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->dashcam_time_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    bk_ui->dashcam_front_vid_panel = lv_obj_create(bk_ui->dashcam);
    lv_obj_set_x(bk_ui->dashcam_front_vid_panel, 24);
    lv_obj_set_y(bk_ui->dashcam_front_vid_panel, 25);
    lv_obj_set_width(bk_ui->dashcam_front_vid_panel, 970);
    lv_obj_set_height(bk_ui->dashcam_front_vid_panel, 624);
    lv_obj_remove_flag(bk_ui->dashcam_front_vid_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->dashcam_front_vid_panel, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_front_vid_panel, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_front_vid_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_front_vid_panel, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_front_vid_panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_front_vid_panel, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_front_vid_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_front_vid_panel, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_front_vid_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_front_vid_panel, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_front_vid_panel, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_front_vid_panel, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_front_vid_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_info_bar = lv_obj_create(bk_ui->dashcam_front_vid_panel);
    lv_obj_set_x(bk_ui->dashcam_info_bar, 5);
    lv_obj_set_y(bk_ui->dashcam_info_bar, 576);
    lv_obj_set_width(bk_ui->dashcam_info_bar, 960);
    lv_obj_set_height(bk_ui->dashcam_info_bar, 41);
    lv_obj_set_style_bg_color(bk_ui->dashcam_info_bar, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_info_bar, 153, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_info_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_info_bar, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_info_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_info_bar, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_info_bar, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_info_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_info_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_info_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_info_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_sky_area = lv_obj_create(bk_ui->dashcam_front_vid_panel);
    lv_obj_set_x(bk_ui->dashcam_sky_area, 5);
    lv_obj_set_y(bk_ui->dashcam_sky_area, 34);
    lv_obj_set_width(bk_ui->dashcam_sky_area, 960);
    lv_obj_set_height(bk_ui->dashcam_sky_area, 540);
    lv_obj_set_style_bg_color(bk_ui->dashcam_sky_area, lv_color_hex(0x020810), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_sky_area, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_sky_area, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_sky_area, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_sky_area, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_sky_area, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_sky_area, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_sky_area, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_sky_area, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_sky_area, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_sky_area, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_front_tag = lv_label_create(bk_ui->dashcam_front_vid_panel);
    lv_label_set_text(bk_ui->dashcam_front_tag, "FRONT");
    lv_label_set_long_mode(bk_ui->dashcam_front_tag, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_front_tag, 18);
    lv_obj_set_y(bk_ui->dashcam_front_tag, 12);
    lv_obj_set_width(bk_ui->dashcam_front_tag, 100);
    lv_obj_set_height(bk_ui->dashcam_front_tag, 24);
    lv_obj_set_style_bg_color(bk_ui->dashcam_front_tag, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_front_tag, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_front_tag, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_front_tag, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_front_tag, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_front_tag, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_front_tag, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_front_tag, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_front_tag, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_front_tag, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_front_tag, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_front_tag, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_front_tag, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_ts_overlay = lv_label_create(bk_ui->dashcam_front_vid_panel);
    lv_label_set_text(bk_ui->dashcam_ts_overlay, "06-16 15:29:42");
    lv_label_set_long_mode(bk_ui->dashcam_ts_overlay, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_ts_overlay, 365);
    lv_obj_set_y(bk_ui->dashcam_ts_overlay, 588);
    lv_obj_set_width(bk_ui->dashcam_ts_overlay, 240);
    lv_obj_set_height(bk_ui->dashcam_ts_overlay, 24);
    lv_obj_set_style_bg_color(bk_ui->dashcam_ts_overlay, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_ts_overlay, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_ts_overlay, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_ts_overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_ts_overlay, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_ts_overlay, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_ts_overlay, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_ts_overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_ts_overlay, &lv_font_pingfang_SC_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_ts_overlay, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_ts_overlay, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_ts_overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_ts_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_spd_overlay = lv_label_create(bk_ui->dashcam_front_vid_panel);
    lv_label_set_text(bk_ui->dashcam_spd_overlay, "28 km/h");
    lv_label_set_long_mode(bk_ui->dashcam_spd_overlay, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_spd_overlay, 814);
    lv_obj_set_y(bk_ui->dashcam_spd_overlay, 585);
    lv_obj_set_width(bk_ui->dashcam_spd_overlay, 138);
    lv_obj_set_height(bk_ui->dashcam_spd_overlay, 24);
    lv_obj_set_style_bg_color(bk_ui->dashcam_spd_overlay, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_spd_overlay, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_spd_overlay, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_spd_overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_spd_overlay, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_spd_overlay, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_spd_overlay, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_spd_overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_spd_overlay, &lv_font_pingfang_SC_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_spd_overlay, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_spd_overlay, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_spd_overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_spd_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_file_name = lv_label_create(bk_ui->dashcam_front_vid_panel);
    lv_label_set_text(bk_ui->dashcam_file_name, "20260616_152942_F.mp4");
    lv_label_set_long_mode(bk_ui->dashcam_file_name, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_file_name, 18);
    lv_obj_set_y(bk_ui->dashcam_file_name, 587);
    lv_obj_set_width(bk_ui->dashcam_file_name, 320);
    lv_obj_set_height(bk_ui->dashcam_file_name, 24);
    lv_obj_set_style_bg_color(bk_ui->dashcam_file_name, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_file_name, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_file_name, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_file_name, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_file_name, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_file_name, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_file_name, lv_color_hex(0xC8D8E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_file_name, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_file_name, &lv_font_pingfang_SC_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_file_name, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_file_name, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_file_name, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_file_name, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_Live = lv_label_create(bk_ui->dashcam_front_vid_panel);
    lv_label_set_text(bk_ui->dashcam_Live, "LIVE");
    lv_label_set_long_mode(bk_ui->dashcam_Live, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_Live, 852);
    lv_obj_set_y(bk_ui->dashcam_Live, 12);
    lv_obj_set_width(bk_ui->dashcam_Live, 100);
    lv_obj_set_height(bk_ui->dashcam_Live, 24);
    lv_obj_set_style_bg_color(bk_ui->dashcam_Live, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_Live, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_Live, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_Live, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_Live, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_Live, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_Live, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_Live, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_Live, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_Live, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_Live, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_Live, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_Live, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_rec_list_panel = lv_obj_create(bk_ui->dashcam);
    lv_obj_set_x(bk_ui->dashcam_rec_list_panel, 1010);
    lv_obj_set_y(bk_ui->dashcam_rec_list_panel, 25);
    lv_obj_set_width(bk_ui->dashcam_rec_list_panel, 246);
    lv_obj_set_height(bk_ui->dashcam_rec_list_panel, 670);
    lv_obj_remove_flag(bk_ui->dashcam_rec_list_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_panel, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_panel, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_panel, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_panel, 80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_panel, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_rec_list_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_rec_list_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_rec_list_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_list_title = lv_label_create(bk_ui->dashcam_rec_list_panel);
    lv_label_set_text(bk_ui->dashcam_list_title, "RECORDS");
    lv_label_set_long_mode(bk_ui->dashcam_list_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->dashcam_list_title, 10);
    lv_obj_set_y(bk_ui->dashcam_list_title, 5);
    lv_obj_set_width(bk_ui->dashcam_list_title, 226);
    lv_obj_set_height(bk_ui->dashcam_list_title, 24);
    lv_obj_set_style_bg_color(bk_ui->dashcam_list_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_list_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_list_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_list_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_list_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_list_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_list_title, lv_color_hex(0x8AA4B8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_list_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_list_title, &lv_font_pingfang_SC_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_list_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_list_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_list_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_list_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->dashcam_rec_list = lv_list_create(bk_ui->dashcam_rec_list_panel);
    bk_ui->dashcam_rec_list_item_0 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 15:29:42");
    {
        lv_obj_t * _lv_list_item_img_0 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_0, 0);
        if(_lv_list_item_img_0 && lv_obj_check_type(_lv_list_item_img_0, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_0, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_0, 0, 0);
            int32_t _liw_0 = lv_image_get_src_width(_lv_list_item_img_0);
            int32_t _lih_0 = lv_image_get_src_height(_lv_list_item_img_0);
            if(_liw_0 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_0, (uint32_t)((40 * 256 + _liw_0 - 1) / _liw_0));
            }
            if(_lih_0 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_0, (uint32_t)((40 * 256 + _lih_0 - 1) / _lih_0));
            }
            lv_obj_set_width(_lv_list_item_img_0, 40);
            lv_obj_set_height(_lv_list_item_img_0, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_0);
        }
    }
    bk_ui->dashcam_rec_list_item_1 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 15:24:18");
    {
        lv_obj_t * _lv_list_item_img_1 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_1, 0);
        if(_lv_list_item_img_1 && lv_obj_check_type(_lv_list_item_img_1, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_1, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_1, 0, 0);
            int32_t _liw_1 = lv_image_get_src_width(_lv_list_item_img_1);
            int32_t _lih_1 = lv_image_get_src_height(_lv_list_item_img_1);
            if(_liw_1 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_1, (uint32_t)((40 * 256 + _liw_1 - 1) / _liw_1));
            }
            if(_lih_1 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_1, (uint32_t)((40 * 256 + _lih_1 - 1) / _lih_1));
            }
            lv_obj_set_width(_lv_list_item_img_1, 40);
            lv_obj_set_height(_lv_list_item_img_1, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_1);
        }
    }
    bk_ui->dashcam_rec_list_item_2 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 15:19:05");
    {
        lv_obj_t * _lv_list_item_img_2 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_2, 0);
        if(_lv_list_item_img_2 && lv_obj_check_type(_lv_list_item_img_2, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_2, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_2, 0, 0);
            int32_t _liw_2 = lv_image_get_src_width(_lv_list_item_img_2);
            int32_t _lih_2 = lv_image_get_src_height(_lv_list_item_img_2);
            if(_liw_2 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_2, (uint32_t)((40 * 256 + _liw_2 - 1) / _liw_2));
            }
            if(_lih_2 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_2, (uint32_t)((40 * 256 + _lih_2 - 1) / _lih_2));
            }
            lv_obj_set_width(_lv_list_item_img_2, 40);
            lv_obj_set_height(_lv_list_item_img_2, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_2);
        }
    }
    bk_ui->dashcam_rec_list_item_3 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 15:13:47");
    {
        lv_obj_t * _lv_list_item_img_3 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_3, 0);
        if(_lv_list_item_img_3 && lv_obj_check_type(_lv_list_item_img_3, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_3, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_3, 0, 0);
            int32_t _liw_3 = lv_image_get_src_width(_lv_list_item_img_3);
            int32_t _lih_3 = lv_image_get_src_height(_lv_list_item_img_3);
            if(_liw_3 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_3, (uint32_t)((40 * 256 + _liw_3 - 1) / _liw_3));
            }
            if(_lih_3 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_3, (uint32_t)((40 * 256 + _lih_3 - 1) / _lih_3));
            }
            lv_obj_set_width(_lv_list_item_img_3, 40);
            lv_obj_set_height(_lv_list_item_img_3, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_3);
        }
    }
    bk_ui->dashcam_rec_list_item_4 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 15:08:30");
    {
        lv_obj_t * _lv_list_item_img_4 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_4, 0);
        if(_lv_list_item_img_4 && lv_obj_check_type(_lv_list_item_img_4, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_4, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_4, 0, 0);
            int32_t _liw_4 = lv_image_get_src_width(_lv_list_item_img_4);
            int32_t _lih_4 = lv_image_get_src_height(_lv_list_item_img_4);
            if(_liw_4 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_4, (uint32_t)((40 * 256 + _liw_4 - 1) / _liw_4));
            }
            if(_lih_4 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_4, (uint32_t)((40 * 256 + _lih_4 - 1) / _lih_4));
            }
            lv_obj_set_width(_lv_list_item_img_4, 40);
            lv_obj_set_height(_lv_list_item_img_4, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_4);
        }
    }
    bk_ui->dashcam_rec_list_item_5 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 15:03:11");
    {
        lv_obj_t * _lv_list_item_img_5 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_5, 0);
        if(_lv_list_item_img_5 && lv_obj_check_type(_lv_list_item_img_5, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_5, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_5, 0, 0);
            int32_t _liw_5 = lv_image_get_src_width(_lv_list_item_img_5);
            int32_t _lih_5 = lv_image_get_src_height(_lv_list_item_img_5);
            if(_liw_5 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_5, (uint32_t)((40 * 256 + _liw_5 - 1) / _liw_5));
            }
            if(_lih_5 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_5, (uint32_t)((40 * 256 + _lih_5 - 1) / _lih_5));
            }
            lv_obj_set_width(_lv_list_item_img_5, 40);
            lv_obj_set_height(_lv_list_item_img_5, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_5);
        }
    }
    bk_ui->dashcam_rec_list_item_6 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 14:57:52");
    {
        lv_obj_t * _lv_list_item_img_6 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_6, 0);
        if(_lv_list_item_img_6 && lv_obj_check_type(_lv_list_item_img_6, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_6, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_6, 0, 0);
            int32_t _liw_6 = lv_image_get_src_width(_lv_list_item_img_6);
            int32_t _lih_6 = lv_image_get_src_height(_lv_list_item_img_6);
            if(_liw_6 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_6, (uint32_t)((40 * 256 + _liw_6 - 1) / _liw_6));
            }
            if(_lih_6 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_6, (uint32_t)((40 * 256 + _lih_6 - 1) / _lih_6));
            }
            lv_obj_set_width(_lv_list_item_img_6, 40);
            lv_obj_set_height(_lv_list_item_img_6, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_6);
        }
    }
    bk_ui->dashcam_rec_list_item_7 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 14:52:38");
    {
        lv_obj_t * _lv_list_item_img_7 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_7, 0);
        if(_lv_list_item_img_7 && lv_obj_check_type(_lv_list_item_img_7, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_7, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_7, 0, 0);
            int32_t _liw_7 = lv_image_get_src_width(_lv_list_item_img_7);
            int32_t _lih_7 = lv_image_get_src_height(_lv_list_item_img_7);
            if(_liw_7 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_7, (uint32_t)((40 * 256 + _liw_7 - 1) / _liw_7));
            }
            if(_lih_7 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_7, (uint32_t)((40 * 256 + _lih_7 - 1) / _lih_7));
            }
            lv_obj_set_width(_lv_list_item_img_7, 40);
            lv_obj_set_height(_lv_list_item_img_7, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_7);
        }
    }
    bk_ui->dashcam_rec_list_item_8 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 14:47:20");
    {
        lv_obj_t * _lv_list_item_img_8 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_8, 0);
        if(_lv_list_item_img_8 && lv_obj_check_type(_lv_list_item_img_8, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_8, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_8, 0, 0);
            int32_t _liw_8 = lv_image_get_src_width(_lv_list_item_img_8);
            int32_t _lih_8 = lv_image_get_src_height(_lv_list_item_img_8);
            if(_liw_8 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_8, (uint32_t)((40 * 256 + _liw_8 - 1) / _liw_8));
            }
            if(_lih_8 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_8, (uint32_t)((40 * 256 + _lih_8 - 1) / _lih_8));
            }
            lv_obj_set_width(_lv_list_item_img_8, 40);
            lv_obj_set_height(_lv_list_item_img_8, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_8);
        }
    }
    bk_ui->dashcam_rec_list_item_9 = lv_list_add_button(bk_ui->dashcam_rec_list, &play_btn_trans_ARGB8888, "06-16 14:42:03");
    {
        lv_obj_t * _lv_list_item_img_9 = lv_obj_get_child(bk_ui->dashcam_rec_list_item_9, 0);
        if(_lv_list_item_img_9 && lv_obj_check_type(_lv_list_item_img_9, &lv_image_class)) {
            /* 与独立 image 一致：TOP_LEFT + pivot + scale，避免仅 set_width/height 裁剪图源 */
            lv_image_set_inner_align(_lv_list_item_img_9, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_image_set_pivot(_lv_list_item_img_9, 0, 0);
            int32_t _liw_9 = lv_image_get_src_width(_lv_list_item_img_9);
            int32_t _lih_9 = lv_image_get_src_height(_lv_list_item_img_9);
            if(_liw_9 > 0) {
                lv_image_set_scale_x(_lv_list_item_img_9, (uint32_t)((40 * 256 + _liw_9 - 1) / _liw_9));
            }
            if(_lih_9 > 0) {
                lv_image_set_scale_y(_lv_list_item_img_9, (uint32_t)((40 * 256 + _lih_9 - 1) / _lih_9));
            }
            lv_obj_set_width(_lv_list_item_img_9, 40);
            lv_obj_set_height(_lv_list_item_img_9, 40);
            lv_obj_update_layout(bk_ui->dashcam_rec_list_item_9);
        }
    }
    lv_obj_set_x(bk_ui->dashcam_rec_list, 1);
    lv_obj_set_y(bk_ui->dashcam_rec_list, 34);
    lv_obj_set_width(bk_ui->dashcam_rec_list, 244);
    lv_obj_set_height(bk_ui->dashcam_rec_list, 635);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list, &lv_font_montserrat_regular_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->dashcam_rec_list, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list, &lv_font_pingfang_SC_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list, &lv_font_pingfang_SC_11, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list, lv_color_hex(0x1EF2C4), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list, 64, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list, 2, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_0, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_0, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_0, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_0, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_0, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_0, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_0, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_0, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_0, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_0, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_0, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_0, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_0, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_0, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_0, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_0, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_0, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_0, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_0, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_0, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_0, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_0, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_0, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_0, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_0, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_0, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_1, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_1, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_1, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_1, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_1, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_1, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_1, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_1, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_1, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_1, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_1, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_1, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_1, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_2, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_2, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_2, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_2, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_2, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_2, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_2, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_2, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_2, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_2, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_2, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_2, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_2, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_2, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_2, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_2, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_3, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_3, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_3, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_3, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_3, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_3, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_3, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_3, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_3, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_3, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_3, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_3, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_3, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_3, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_3, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_3, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_4, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_4, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_4, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_4, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_4, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_4, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_4, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_4, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_4, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_4, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_4, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_4, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_4, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_4, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_4, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_4, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_4, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_5, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_5, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_5, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_5, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_5, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_5, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_5, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_5, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_5, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_5, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_5, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_5, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_5, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_5, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_5, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_5, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_6, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_6, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_6, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_6, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_6, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_6, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_6, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_6, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_6, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_6, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_6, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_6, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_6, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_6, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_6, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_6, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_6, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_7, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_7, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_7, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_7, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_7, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_7, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_7, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_7, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_7, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_7, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_7, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_7, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_7, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_7, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_7, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_7, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_7, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_7, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_7, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_7, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_8, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_8, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_8, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_8, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_8, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_8, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_8, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_8, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_8, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_8, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_8, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_8, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_8, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_8, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_8, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_8, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_8, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_8, false, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_9, lv_color_hex(0x0D1A28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_9, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_9, lv_color_hex(0x2A4055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_9, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_9, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_9, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_9, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->dashcam_rec_list_item_9, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->dashcam_rec_list_item_9, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->dashcam_rec_list_item_9, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->dashcam_rec_list_item_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->dashcam_rec_list_item_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->dashcam_rec_list_item_9, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->dashcam_rec_list_item_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->dashcam_rec_list_item_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->dashcam_rec_list_item_9, &lv_font_montserrat_regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->dashcam_rec_list_item_9, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->dashcam_rec_list_item_9, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bk_ui->dashcam_rec_list_item_9, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(bk_ui->dashcam_rec_list_item_9, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bk_ui->dashcam_rec_list_item_9, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bk_ui->dashcam_rec_list_item_9, 1, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(bk_ui->dashcam_rec_list_item_9, 255, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bk_ui->dashcam_rec_list_item_9, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bk_ui->dashcam_rec_list_item_9, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_clip_corner(bk_ui->dashcam_rec_list_item_9, false, LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_update_layout(bk_ui->dashcam);
}

/*
 * @brief: destroy page dashcam
 */
void destroy_page_dashcam(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->dashcam != NULL) {
        lv_obj_del(bk_ui->dashcam);
        bk_ui->dashcam = NULL;
    }
}