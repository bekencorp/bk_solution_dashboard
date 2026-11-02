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
 * @brief: init page ota_update
 */
void init_page_ota_update(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->ota_update != NULL && lv_obj_is_valid(bk_ui->ota_update)) {
        destroy_page_ota_update(bk_ui);
    }
    

    bk_ui->ota_update = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->ota_update, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->ota_update, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->ota_update, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_bg_img = lv_image_create(bk_ui->ota_update);
    lv_image_set_src(bk_ui->ota_update_bg_img, NULL);
    lv_image_set_pivot(bk_ui->ota_update_bg_img, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_bg_img, 0);
    lv_obj_set_x(bk_ui->ota_update_bg_img, 0);
    lv_obj_set_y(bk_ui->ota_update_bg_img, 0);
    lv_obj_set_width(bk_ui->ota_update_bg_img, 1280);
    lv_obj_set_height(bk_ui->ota_update_bg_img, 720);
    lv_obj_set_style_bg_color(bk_ui->ota_update_bg_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_bg_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_bg_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_bg_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_bg_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_bg_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_bg_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_bg_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_bg_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_top_bar = lv_obj_create(bk_ui->ota_update);
    lv_obj_set_x(bk_ui->ota_update_top_bar, 30);
    lv_obj_set_y(bk_ui->ota_update_top_bar, 22);
    lv_obj_set_width(bk_ui->ota_update_top_bar, 1220);
    lv_obj_set_height(bk_ui->ota_update_top_bar, 62);
    lv_obj_set_style_bg_color(bk_ui->ota_update_top_bar, lv_color_hex(0x0A1520), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_top_bar, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_top_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_top_bar, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_top_bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_top_bar, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_top_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_top_bar, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_top_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_top_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_top_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ota_title = lv_label_create(bk_ui->ota_update_top_bar);
    lv_label_set_text(bk_ui->ota_update_ota_title, "升级中");
    lv_label_set_long_mode(bk_ui->ota_update_ota_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->ota_update_ota_title, 50);
    lv_obj_set_y(bk_ui->ota_update_ota_title, 12);
    lv_obj_set_width(bk_ui->ota_update_ota_title, 150);
    lv_obj_set_height(bk_ui->ota_update_ota_title, 24);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ota_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ota_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ota_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ota_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ota_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ota_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_ota_title, lv_color_hex(0x1EF2C4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_ota_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_ota_title, &lv_font_pingfang_SC_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_ota_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ota_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ota_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ota_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_back_btn = lv_imgbtn_create(bk_ui->ota_update_top_bar);
    bk_ui->ota_update_back_btn_label = lv_label_create(bk_ui->ota_update_back_btn);
    lv_label_set_text(bk_ui->ota_update_back_btn_label, "Button");
    lv_label_set_long_mode(bk_ui->ota_update_back_btn_label, LV_LABEL_LONG_MODE_WRAP);
    lv_imgbtn_set_src(bk_ui->ota_update_back_btn, LV_IMGBTN_STATE_RELEASED, NULL, &nav_ic_back_status_copy_33x31_RGB565A8_NONE, NULL);
    lv_imgbtn_set_src(bk_ui->ota_update_back_btn, LV_IMGBTN_STATE_PRESSED, NULL, &nav_ic_back_status_copy_33x31_RGB565A8_NONE, NULL);
    lv_obj_set_x(bk_ui->ota_update_back_btn, 10);
    lv_obj_set_y(bk_ui->ota_update_back_btn, 16);
    lv_obj_set_width(bk_ui->ota_update_back_btn, 33);
    lv_obj_set_height(bk_ui->ota_update_back_btn, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_back_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_back_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_back_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_back_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_back_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_back_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_back_btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_back_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_back_btn, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_back_btn, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_back_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_back_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->ota_update_back_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_back_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(bk_ui->ota_update_back_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(bk_ui->ota_update_back_btn, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_side(bk_ui->ota_update_back_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_back_btn, false, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_top(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_left(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_right(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_row(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_column(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(bk_ui->ota_update_back_btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_opa(bk_ui->ota_update_back_btn, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(bk_ui->ota_update_back_btn, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_align(bk_ui->ota_update_back_btn, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_back_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_back_btn, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_back_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);

    bk_ui->ota_update_icons_panel = lv_obj_create(bk_ui->ota_update_top_bar);
    lv_obj_set_flex_flow(bk_ui->ota_update_icons_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bk_ui->ota_update_icons_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_x(bk_ui->ota_update_icons_panel, 360);
    lv_obj_set_y(bk_ui->ota_update_icons_panel, 13);
    lv_obj_set_width(bk_ui->ota_update_icons_panel, 500);
    lv_obj_set_height(bk_ui->ota_update_icons_panel, 34);
    lv_obj_remove_flag(bk_ui->ota_update_icons_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->ota_update_icons_panel, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_icons_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_icons_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_icons_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_icons_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_icons_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_icons_panel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_icons_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_icons_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_icons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_abs = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_abs, &nav_ic_abs_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_abs, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_abs, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_abs, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_abs, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_abs, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_abs, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_abs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_abs, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_abs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_abs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_abs, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_abs, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_abs, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_abs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_abs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_abs, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_abs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_tcs = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_tcs, &nav_ic_tcs_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_tcs, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_tcs, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_tcs, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_tcs, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_tcs, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_tcs, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_tcs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_tcs, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_tcs, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_tcs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_tcs, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_tcs, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_tcs, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_tcs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_tcs, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_tcs, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_tcs, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_gps = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_gps, &nav_ic_gps_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_gps, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_gps, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_gps, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_gps, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_gps, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_gps, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_gps, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_gps, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_gps, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_gps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_gps, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_gps, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_gps, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_gps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_gps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_gps, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_gps, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_bt = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_bt, &nav_ic_bt_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_bt, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_bt, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_bt, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_bt, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_bt, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_bt, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_bt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_bt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_bt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_bt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_bt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_bt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_bt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_bt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_bt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_bt, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_bt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_light = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_light, &nav_ic_light_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_light, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_light, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_light, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_light, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_light, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_light, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_light, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_light, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_light, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_light, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_light, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_light, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_light, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_light, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_light, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_light, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_eng = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_eng, &nav_ic_eng_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_eng, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_eng, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_eng, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_eng, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_eng, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_eng, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_eng, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_eng, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_eng, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_eng, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_eng, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_eng, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_eng, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_eng, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_eng, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_eng, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_eng, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_warn = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_warn, &nav_ic_warn_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_warn, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_warn, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_warn, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_warn, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_warn, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_warn, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_warn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_warn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_warn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_warn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_warn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_warn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_warn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_warn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_warn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_warn, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_warn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_batt = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_batt, &nav_ic_batt_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_batt, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_batt, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_batt, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_batt, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_batt, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_batt, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_batt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_batt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_batt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_batt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_batt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_batt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_batt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_batt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_batt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_batt, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_batt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_tire = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_tire, &nav_ic_tire_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_tire, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_tire, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_tire, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_tire, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_tire, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_tire, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_tire, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_tire, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_tire, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_tire, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_tire, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_tire, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_tire, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_tire, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_tire, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_tire, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_tire, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_ic_temp = lv_image_create(bk_ui->ota_update_icons_panel);
    lv_image_set_src(bk_ui->ota_update_ic_temp, &nav_ic_temp_copy_33x31_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->ota_update_ic_temp, 50, 50);
    lv_image_set_rotation(bk_ui->ota_update_ic_temp, 0);
    lv_obj_set_x(bk_ui->ota_update_ic_temp, 0);
    lv_obj_set_y(bk_ui->ota_update_ic_temp, 0);
    lv_obj_set_width(bk_ui->ota_update_ic_temp, 33);
    lv_obj_set_height(bk_ui->ota_update_ic_temp, 31);
    lv_obj_set_style_bg_color(bk_ui->ota_update_ic_temp, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_ic_temp, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_ic_temp, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_ic_temp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_ic_temp, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_ic_temp, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_ic_temp, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_ic_temp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->ota_update_ic_temp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->ota_update_ic_temp, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->ota_update_ic_temp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_time_clk = lv_label_create(bk_ui->ota_update_top_bar);
    lv_label_set_text(bk_ui->ota_update_time_clk, "");
    lv_digital_clock_register(bk_ui->ota_update_time_clk, 0, 1, 15, 29, 0);
    lv_obj_set_x(bk_ui->ota_update_time_clk, 1015);
    lv_obj_set_y(bk_ui->ota_update_time_clk, 12);
    lv_obj_set_width(bk_ui->ota_update_time_clk, 175);
    lv_obj_set_height(bk_ui->ota_update_time_clk, 38);
    lv_obj_set_style_bg_color(bk_ui->ota_update_time_clk, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_time_clk, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_time_clk, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_time_clk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_time_clk, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_time_clk, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_time_clk, lv_color_hex(0xF0F0F0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_time_clk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_time_clk, &lv_font_montserrat_regular_22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_time_clk, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_time_clk, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_time_clk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_time_clk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_gauge_panel = lv_obj_create(bk_ui->ota_update);
    lv_obj_set_x(bk_ui->ota_update_gauge_panel, 313);
    lv_obj_set_y(bk_ui->ota_update_gauge_panel, 140);
    lv_obj_set_width(bk_ui->ota_update_gauge_panel, 650);
    lv_obj_set_height(bk_ui->ota_update_gauge_panel, 456);
    lv_obj_remove_flag(bk_ui->ota_update_gauge_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->ota_update_gauge_panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_gauge_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_gauge_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_gauge_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_gauge_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_gauge_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_gauge_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_gauge_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_gauge_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_prog_ring = lv_arc_create(bk_ui->ota_update_gauge_panel);
    lv_arc_set_range(bk_ui->ota_update_prog_ring, 0, 100);
    lv_arc_set_value(bk_ui->ota_update_prog_ring, 64);
    lv_arc_set_bg_angles(bk_ui->ota_update_prog_ring, 180, 0);
    lv_arc_set_rotation(bk_ui->ota_update_prog_ring, 0);
    lv_arc_set_mode(bk_ui->ota_update_prog_ring, LV_ARC_MODE_NORMAL);
    lv_obj_set_x(bk_ui->ota_update_prog_ring, 13);
    lv_obj_set_y(bk_ui->ota_update_prog_ring, 79);
    lv_obj_set_width(bk_ui->ota_update_prog_ring, 640);
    lv_obj_set_height(bk_ui->ota_update_prog_ring, 614);
    lv_obj_remove_flag(bk_ui->ota_update_prog_ring, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->ota_update_prog_ring, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_prog_ring, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_prog_ring, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_prog_ring, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_prog_ring, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_prog_ring, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_prog_ring, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_prog_ring, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_prog_ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->ota_update_prog_ring, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->ota_update_prog_ring, lv_color_hex(0x30363D), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->ota_update_prog_ring, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->ota_update_prog_ring, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->ota_update_prog_ring, 50, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->ota_update_prog_ring, lv_color_hex(0x35A8FF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->ota_update_prog_ring, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->ota_update_prog_ring, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->ota_update_prog_ring, lv_color_hex(0x2195f6), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_prog_ring, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_prog_ring, LV_GRAD_DIR_NONE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bk_ui->ota_update_prog_ring, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    bk_ui->ota_update_pct_num = lv_label_create(bk_ui->ota_update_gauge_panel);
    lv_label_set_text(bk_ui->ota_update_pct_num, "64");
    lv_label_set_long_mode(bk_ui->ota_update_pct_num, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->ota_update_pct_num, 10);
    lv_obj_set_y(bk_ui->ota_update_pct_num, 184);
    lv_obj_set_width(bk_ui->ota_update_pct_num, 650);
    lv_obj_set_height(bk_ui->ota_update_pct_num, 96);
    lv_obj_set_style_bg_color(bk_ui->ota_update_pct_num, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_pct_num, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_pct_num, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_pct_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_pct_num, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_pct_num, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_pct_num, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_pct_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_pct_num, &lv_font_pingfang_SC_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_pct_num, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_pct_num, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_pct_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_pct_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_pct_sym = lv_label_create(bk_ui->ota_update_gauge_panel);
    lv_label_set_text(bk_ui->ota_update_pct_sym, "%");
    lv_label_set_long_mode(bk_ui->ota_update_pct_sym, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->ota_update_pct_sym, 93);
    lv_obj_set_y(bk_ui->ota_update_pct_sym, 262);
    lv_obj_set_width(bk_ui->ota_update_pct_sym, 650);
    lv_obj_set_height(bk_ui->ota_update_pct_sym, 34);
    lv_obj_set_style_bg_color(bk_ui->ota_update_pct_sym, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_pct_sym, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_pct_sym, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_pct_sym, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_pct_sym, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_pct_sym, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_pct_sym, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_pct_sym, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_pct_sym, &lv_font_montserrat_regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_pct_sym, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_pct_sym, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_pct_sym, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_pct_sym, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_stat_lbl = lv_label_create(bk_ui->ota_update_gauge_panel);
    lv_label_set_text(bk_ui->ota_update_stat_lbl, "升级中");
    lv_label_set_long_mode(bk_ui->ota_update_stat_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->ota_update_stat_lbl, 10);
    lv_obj_set_y(bk_ui->ota_update_stat_lbl, 312);
    lv_obj_set_width(bk_ui->ota_update_stat_lbl, 650);
    lv_obj_set_height(bk_ui->ota_update_stat_lbl, 38);
    lv_obj_set_style_bg_color(bk_ui->ota_update_stat_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_stat_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_stat_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_stat_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_stat_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_stat_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_stat_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_stat_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_stat_lbl, &lv_font_pingfang_SC_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_stat_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_stat_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_stat_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_stat_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->ota_update_eta_lbl = lv_label_create(bk_ui->ota_update_gauge_panel);
    lv_label_set_text(bk_ui->ota_update_eta_lbl, "预计需要 3 分钟");
    lv_label_set_long_mode(bk_ui->ota_update_eta_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->ota_update_eta_lbl, 10);
    lv_obj_set_y(bk_ui->ota_update_eta_lbl, 353);
    lv_obj_set_width(bk_ui->ota_update_eta_lbl, 650);
    lv_obj_set_height(bk_ui->ota_update_eta_lbl, 34);
    lv_obj_set_style_bg_color(bk_ui->ota_update_eta_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->ota_update_eta_lbl, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->ota_update_eta_lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->ota_update_eta_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->ota_update_eta_lbl, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->ota_update_eta_lbl, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->ota_update_eta_lbl, lv_color_hex(0x999999), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->ota_update_eta_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->ota_update_eta_lbl, &lv_font_pingfang_SC_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->ota_update_eta_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->ota_update_eta_lbl, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->ota_update_eta_lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->ota_update_eta_lbl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->ota_update);
}

/*
 * @brief: destroy page ota_update
 */
void destroy_page_ota_update(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->ota_update != NULL) {
        lv_obj_del(bk_ui->ota_update);
        bk_ui->ota_update = NULL;
    }
}