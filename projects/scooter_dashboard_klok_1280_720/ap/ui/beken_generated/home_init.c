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


/**
 * @brief Event callback for home_left_card - handles all events
 * @param e LVGL event object
 */
void home_left_card_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.klok_main, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_klok_main);
    }
}


/*
 * @brief: init page home
 */
void init_page_home(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->home != NULL && lv_obj_is_valid(bk_ui->home)) {
        destroy_page_home(bk_ui);
    }
    

    bk_ui->home = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->home, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->home, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->home, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_bg_panel = lv_obj_create(bk_ui->home);
    lv_obj_set_x(bk_ui->home_bg_panel, 0);
    lv_obj_set_y(bk_ui->home_bg_panel, -2);
    lv_obj_set_width(bk_ui->home_bg_panel, 1280);
    lv_obj_set_height(bk_ui->home_bg_panel, 720);
    lv_obj_remove_flag(bk_ui->home_bg_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_bg_panel, lv_color_hex(0x120a35), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bg_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bg_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(bk_ui->home_bg_panel, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(bk_ui->home_bg_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor(bk_ui->home_bg_panel, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bg_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bg_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bg_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bg_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_bg_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_bg_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_bg_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_bg_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bg_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bg_panel, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bg_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bg_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bg_panel, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bg_panel, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bg_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bg_panel, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_logo_eye = lv_label_create(bk_ui->home_bg_panel);
    lv_label_set_text(bk_ui->home_logo_eye, "O");
    lv_label_set_long_mode(bk_ui->home_logo_eye, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_logo_eye, 24);
    lv_obj_set_y(bk_ui->home_logo_eye, 32);
    lv_obj_set_width(bk_ui->home_logo_eye, 44);
    lv_obj_set_height(bk_ui->home_logo_eye, 28);
    lv_obj_remove_flag(bk_ui->home_logo_eye, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_logo_eye, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_logo_eye, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_logo_eye, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_logo_eye, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_logo_eye, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_logo_eye, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_logo_eye, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_logo_eye, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_logo_eye, &lv_font_montserrat_regular_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_logo_eye, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_logo_eye, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_logo_eye, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_logo_eye, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_brand_txt = lv_label_create(bk_ui->home_bg_panel);
    lv_label_set_text(bk_ui->home_brand_txt, "Klok 点歌系统");
    lv_label_set_long_mode(bk_ui->home_brand_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_brand_txt, 75);
    lv_obj_set_y(bk_ui->home_brand_txt, 34);
    lv_obj_set_width(bk_ui->home_brand_txt, 230);
    lv_obj_set_height(bk_ui->home_brand_txt, 34);
    lv_obj_remove_flag(bk_ui->home_brand_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_brand_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_brand_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_brand_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_brand_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_brand_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_brand_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_brand_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_brand_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_brand_txt, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_brand_txt, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_brand_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_brand_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_brand_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_date_txt = lv_label_create(bk_ui->home_bg_panel);
    lv_label_set_text(bk_ui->home_date_txt, "2026 / 07 / 06  星期一");
    lv_label_set_long_mode(bk_ui->home_date_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_date_txt, 610);
    lv_obj_set_y(bk_ui->home_date_txt, 34);
    lv_obj_set_width(bk_ui->home_date_txt, 310);
    lv_obj_set_height(bk_ui->home_date_txt, 32);
    lv_obj_remove_flag(bk_ui->home_date_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_date_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_date_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_date_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_date_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_date_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_date_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_date_txt, lv_color_hex(0xf3f6ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_date_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_date_txt, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_date_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_date_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_date_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_date_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_left_card = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_left_card, 20);
    lv_obj_set_y(bk_ui->home_left_card, 92);
    lv_obj_set_width(bk_ui->home_left_card, 400);
    lv_obj_set_height(bk_ui->home_left_card, 575);
    lv_obj_remove_flag(bk_ui->home_left_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_left_card, lv_color_hex(0xf2c400), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_left_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_left_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_left_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_left_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_left_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_left_card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_left_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_left_card, lv_color_hex(0x090018), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_left_card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_left_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_left_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_left_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_left_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_left_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_left_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_left_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_left_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_left_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_left_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_left_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->home_left_card, home_left_card_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->home_main_title = lv_label_create(bk_ui->home_left_card);
    lv_label_set_text(bk_ui->home_main_title, "歌手点歌");
    lv_label_set_long_mode(bk_ui->home_main_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_main_title, 48);
    lv_obj_set_y(bk_ui->home_main_title, 466);
    lv_obj_set_width(bk_ui->home_main_title, 300);
    lv_obj_set_height(bk_ui->home_main_title, 46);
    lv_obj_remove_flag(bk_ui->home_main_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_main_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_main_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_main_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_main_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_main_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_main_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_main_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_main_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_main_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_main_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_main_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_main_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_main_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_main_hint = lv_label_create(bk_ui->home_left_card);
    lv_label_set_text(bk_ui->home_main_hint, "按歌手快速查找歌曲");
    lv_label_set_long_mode(bk_ui->home_main_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_main_hint, 48);
    lv_obj_set_y(bk_ui->home_main_hint, 520);
    lv_obj_set_width(bk_ui->home_main_hint, 310);
    lv_obj_set_height(bk_ui->home_main_hint, 24);
    lv_obj_remove_flag(bk_ui->home_main_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_main_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_main_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_main_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_main_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_main_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_main_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_main_hint, lv_color_hex(0xfff2b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_main_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_main_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_main_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_main_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_main_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_main_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_singer_img = lv_image_create(bk_ui->home_left_card);
    lv_image_set_src(bk_ui->home_singer_img, &klok_icon_singer_v2_300x360_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_singer_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_singer_img, 0);
    lv_obj_set_x(bk_ui->home_singer_img, 50);
    lv_obj_set_y(bk_ui->home_singer_img, 62);
    lv_obj_set_width(bk_ui->home_singer_img, 300);
    lv_obj_set_height(bk_ui->home_singer_img, 360);
    lv_obj_remove_flag(bk_ui->home_singer_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_singer_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_singer_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_singer_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_singer_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_singer_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_singer_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_singer_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_singer_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_singer_img, 230, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_singer_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_singer_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_bt_card = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_bt_card, 440);
    lv_obj_set_y(bk_ui->home_bt_card, 100);
    lv_obj_set_width(bk_ui->home_bt_card, 400);
    lv_obj_set_height(bk_ui->home_bt_card, 250);
    lv_obj_remove_flag(bk_ui->home_bt_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_bt_card, lv_color_hex(0x168bff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bt_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bt_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bt_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bt_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bt_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bt_card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bt_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_bt_card, lv_color_hex(0x03133c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_bt_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_bt_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_bt_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_bt_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bt_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bt_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bt_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bt_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bt_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bt_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bt_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bt_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_bt_title = lv_label_create(bk_ui->home_bt_card);
    lv_label_set_text(bk_ui->home_bt_title, "蓝牙音乐");
    lv_label_set_long_mode(bk_ui->home_bt_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_bt_title, 35);
    lv_obj_set_y(bk_ui->home_bt_title, 138);
    lv_obj_set_width(bk_ui->home_bt_title, 310);
    lv_obj_set_height(bk_ui->home_bt_title, 46);
    lv_obj_remove_flag(bk_ui->home_bt_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_bt_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bt_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bt_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bt_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bt_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bt_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_bt_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_bt_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_bt_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_bt_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_bt_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_bt_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_bt_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_bt_hint = lv_label_create(bk_ui->home_bt_card);
    lv_label_set_text(bk_ui->home_bt_hint, "手机连接播放音乐");
    lv_label_set_long_mode(bk_ui->home_bt_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_bt_hint, 38);
    lv_obj_set_y(bk_ui->home_bt_hint, 186);
    lv_obj_set_width(bk_ui->home_bt_hint, 270);
    lv_obj_set_height(bk_ui->home_bt_hint, 22);
    lv_obj_remove_flag(bk_ui->home_bt_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_bt_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bt_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bt_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bt_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bt_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bt_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_bt_hint, lv_color_hex(0xd5efff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_bt_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_bt_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_bt_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_bt_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_bt_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_bt_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_bt_img = lv_image_create(bk_ui->home_bt_card);
    lv_image_set_src(bk_ui->home_bt_img, &klok_icon_bt_v2_104x104_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_bt_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_bt_img, 0);
    lv_obj_set_x(bk_ui->home_bt_img, 264);
    lv_obj_set_y(bk_ui->home_bt_img, 62);
    lv_obj_set_width(bk_ui->home_bt_img, 104);
    lv_obj_set_height(bk_ui->home_bt_img, 104);
    lv_obj_remove_flag(bk_ui->home_bt_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_bt_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_bt_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_bt_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_bt_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_bt_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_bt_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_bt_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_bt_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_bt_img, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_bt_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_bt_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_media_card = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_media_card, 860);
    lv_obj_set_y(bk_ui->home_media_card, 100);
    lv_obj_set_width(bk_ui->home_media_card, 400);
    lv_obj_set_height(bk_ui->home_media_card, 250);
    lv_obj_remove_flag(bk_ui->home_media_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_media_card, lv_color_hex(0x36e21f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_media_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_media_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_media_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_media_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_media_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_media_card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_media_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_media_card, lv_color_hex(0x023800), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_media_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_media_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_media_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_media_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_media_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_media_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_media_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_media_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_media_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_media_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_media_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_media_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_med_title = lv_label_create(bk_ui->home_media_card);
    lv_label_set_text(bk_ui->home_med_title, "媒体中心");
    lv_label_set_long_mode(bk_ui->home_med_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_med_title, 35);
    lv_obj_set_y(bk_ui->home_med_title, 138);
    lv_obj_set_width(bk_ui->home_med_title, 260);
    lv_obj_set_height(bk_ui->home_med_title, 46);
    lv_obj_remove_flag(bk_ui->home_med_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_med_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_med_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_med_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_med_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_med_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_med_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_med_title, lv_color_hex(0xf0fff0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_med_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_med_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_med_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_med_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_med_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_med_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_med_hint = lv_label_create(bk_ui->home_media_card);
    lv_label_set_text(bk_ui->home_med_hint, "本地视频与照片管理");
    lv_label_set_long_mode(bk_ui->home_med_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_med_hint, 38);
    lv_obj_set_y(bk_ui->home_med_hint, 186);
    lv_obj_set_width(bk_ui->home_med_hint, 300);
    lv_obj_set_height(bk_ui->home_med_hint, 22);
    lv_obj_remove_flag(bk_ui->home_med_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_med_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_med_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_med_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_med_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_med_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_med_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_med_hint, lv_color_hex(0xd8ffd6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_med_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_med_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_med_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_med_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_med_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_med_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_media_img = lv_image_create(bk_ui->home_media_card);
    lv_image_set_src(bk_ui->home_media_img, &klok_icon_media_v2_104x104_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_media_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_media_img, 0);
    lv_obj_set_x(bk_ui->home_media_img, 260);
    lv_obj_set_y(bk_ui->home_media_img, 62);
    lv_obj_set_width(bk_ui->home_media_img, 104);
    lv_obj_set_height(bk_ui->home_media_img, 104);
    lv_obj_remove_flag(bk_ui->home_media_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_media_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_media_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_media_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_media_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_media_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_media_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_media_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_media_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_media_img, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_media_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_media_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_dance_card = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_dance_card, 440);
    lv_obj_set_y(bk_ui->home_dance_card, 370);
    lv_obj_set_width(bk_ui->home_dance_card, 260);
    lv_obj_set_height(bk_ui->home_dance_card, 300);
    lv_obj_remove_flag(bk_ui->home_dance_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_dance_card, lv_color_hex(0xef39de), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_dance_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_dance_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_dance_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_dance_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_dance_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_dance_card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_dance_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_dance_card, lv_color_hex(0x44003f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_dance_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_dance_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_dance_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_dance_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_dance_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_dance_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_dance_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_dance_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_dance_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_dance_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_dance_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_dance_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_dance_title = lv_label_create(bk_ui->home_dance_card);
    lv_label_set_text(bk_ui->home_dance_title, "热舞派对");
    lv_label_set_long_mode(bk_ui->home_dance_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_dance_title, 28);
    lv_obj_set_y(bk_ui->home_dance_title, 195);
    lv_obj_set_width(bk_ui->home_dance_title, 220);
    lv_obj_set_height(bk_ui->home_dance_title, 40);
    lv_obj_remove_flag(bk_ui->home_dance_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_dance_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_dance_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_dance_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_dance_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_dance_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_dance_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_dance_title, lv_color_hex(0x5d1a6a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_dance_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_dance_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_dance_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_dance_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_dance_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_dance_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_dance_hint = lv_label_create(bk_ui->home_dance_card);
    lv_label_set_text(bk_ui->home_dance_hint, "动感舞曲专区");
    lv_label_set_long_mode(bk_ui->home_dance_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_dance_hint, 30);
    lv_obj_set_y(bk_ui->home_dance_hint, 240);
    lv_obj_set_width(bk_ui->home_dance_hint, 210);
    lv_obj_set_height(bk_ui->home_dance_hint, 24);
    lv_obj_remove_flag(bk_ui->home_dance_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_dance_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_dance_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_dance_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_dance_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_dance_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_dance_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_dance_hint, lv_color_hex(0x8a3595), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_dance_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_dance_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_dance_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_dance_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_dance_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_dance_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_dance_img = lv_image_create(bk_ui->home_dance_card);
    lv_image_set_src(bk_ui->home_dance_img, &klok_icon_dance_v2_104x104_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_dance_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_dance_img, 0);
    lv_obj_set_x(bk_ui->home_dance_img, 78);
    lv_obj_set_y(bk_ui->home_dance_img, 50);
    lv_obj_set_width(bk_ui->home_dance_img, 104);
    lv_obj_set_height(bk_ui->home_dance_img, 104);
    lv_obj_remove_flag(bk_ui->home_dance_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_dance_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_dance_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_dance_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_dance_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_dance_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_dance_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_dance_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_dance_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_dance_img, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_dance_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_dance_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_album_card = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_album_card, 720);
    lv_obj_set_y(bk_ui->home_album_card, 370);
    lv_obj_set_width(bk_ui->home_album_card, 260);
    lv_obj_set_height(bk_ui->home_album_card, 300);
    lv_obj_add_flag(bk_ui->home_album_card, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_remove_flag(bk_ui->home_album_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_album_card, lv_color_hex(0x8ceaf2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_album_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_album_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_album_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_album_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_album_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_album_card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_album_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_album_card, lv_color_hex(0x003a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_album_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_album_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_album_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_album_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_album_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_album_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_album_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_album_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_album_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_album_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_album_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_album_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_album_title = lv_label_create(bk_ui->home_album_card);
    lv_label_set_text(bk_ui->home_album_title, "相册");
    lv_label_set_long_mode(bk_ui->home_album_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_album_title, 28);
    lv_obj_set_y(bk_ui->home_album_title, 195);
    lv_obj_set_width(bk_ui->home_album_title, 220);
    lv_obj_set_height(bk_ui->home_album_title, 40);
    lv_obj_remove_flag(bk_ui->home_album_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_album_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_album_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_album_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_album_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_album_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_album_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_album_title, lv_color_hex(0x276b75), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_album_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_album_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_album_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_album_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_album_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_album_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_album_hint = lv_label_create(bk_ui->home_album_card);
    lv_label_set_text(bk_ui->home_album_hint, "照片屏保管理");
    lv_label_set_long_mode(bk_ui->home_album_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_album_hint, 30);
    lv_obj_set_y(bk_ui->home_album_hint, 240);
    lv_obj_set_width(bk_ui->home_album_hint, 210);
    lv_obj_set_height(bk_ui->home_album_hint, 24);
    lv_obj_remove_flag(bk_ui->home_album_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_album_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_album_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_album_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_album_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_album_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_album_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_album_hint, lv_color_hex(0x407e87), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_album_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_album_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_album_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_album_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_album_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_album_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_album_img = lv_image_create(bk_ui->home_album_card);
    lv_image_set_src(bk_ui->home_album_img, &klok_icon_album_v2_104x104_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_album_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_album_img, 0);
    lv_obj_set_x(bk_ui->home_album_img, 78);
    lv_obj_set_y(bk_ui->home_album_img, 50);
    lv_obj_set_width(bk_ui->home_album_img, 104);
    lv_obj_set_height(bk_ui->home_album_img, 104);
    lv_obj_remove_flag(bk_ui->home_album_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_album_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_album_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_album_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_album_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_album_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_album_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_album_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_album_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_album_img, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_album_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_album_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_sys_card = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_sys_card, 1000);
    lv_obj_set_y(bk_ui->home_sys_card, 370);
    lv_obj_set_width(bk_ui->home_sys_card, 260);
    lv_obj_set_height(bk_ui->home_sys_card, 300);
    lv_obj_remove_flag(bk_ui->home_sys_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_sys_card, lv_color_hex(0xfa2b56), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_sys_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_sys_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_sys_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_sys_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_sys_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_sys_card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_sys_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_sys_card, lv_color_hex(0x560017), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_sys_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_sys_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_sys_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_sys_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_sys_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_sys_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_sys_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_sys_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_sys_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_sys_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_sys_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_sys_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_sys_title = lv_label_create(bk_ui->home_sys_card);
    lv_label_set_text(bk_ui->home_sys_title, "系统设置");
    lv_label_set_long_mode(bk_ui->home_sys_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_sys_title, 26);
    lv_obj_set_y(bk_ui->home_sys_title, 195);
    lv_obj_set_width(bk_ui->home_sys_title, 225);
    lv_obj_set_height(bk_ui->home_sys_title, 40);
    lv_obj_remove_flag(bk_ui->home_sys_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_sys_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_sys_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_sys_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_sys_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_sys_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_sys_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_sys_title, lv_color_hex(0x9d001c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_sys_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_sys_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_sys_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_sys_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_sys_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_sys_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_sys_hint = lv_label_create(bk_ui->home_sys_card);
    lv_label_set_text(bk_ui->home_sys_hint, "设备与网络设置");
    lv_label_set_long_mode(bk_ui->home_sys_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->home_sys_hint, 30);
    lv_obj_set_y(bk_ui->home_sys_hint, 240);
    lv_obj_set_width(bk_ui->home_sys_hint, 210);
    lv_obj_set_height(bk_ui->home_sys_hint, 24);
    lv_obj_remove_flag(bk_ui->home_sys_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_sys_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_sys_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_sys_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_sys_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_sys_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_sys_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->home_sys_hint, lv_color_hex(0xb81f3d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->home_sys_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->home_sys_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->home_sys_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_sys_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_sys_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_sys_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_sys_img = lv_image_create(bk_ui->home_sys_card);
    lv_image_set_src(bk_ui->home_sys_img, &klok_icon_system_v2_104x104_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_sys_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_sys_img, 0);
    lv_obj_set_x(bk_ui->home_sys_img, 78);
    lv_obj_set_y(bk_ui->home_sys_img, 50);
    lv_obj_set_width(bk_ui->home_sys_img, 104);
    lv_obj_set_height(bk_ui->home_sys_img, 104);
    lv_obj_remove_flag(bk_ui->home_sys_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_sys_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_sys_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_sys_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_sys_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_sys_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_sys_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_sys_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_sys_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_sys_img, 245, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_sys_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_sys_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_nav_grp = lv_obj_create(bk_ui->home_bg_panel);
    lv_obj_set_x(bk_ui->home_nav_grp, 938);
    lv_obj_set_y(bk_ui->home_nav_grp, 25);
    lv_obj_set_width(bk_ui->home_nav_grp, 330);
    lv_obj_set_height(bk_ui->home_nav_grp, 46);
    lv_obj_remove_flag(bk_ui->home_nav_grp, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_nav_grp, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_grp, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_grp, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_grp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_grp, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_grp, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_nav_grp, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_nav_grp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_nav_grp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->home_nav_grp, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_grp, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_grp, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_grp, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_grp, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_grp, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_grp, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_grp, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_grp, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->home_nav_eq_img = lv_image_create(bk_ui->home_nav_grp);
    lv_image_set_src(bk_ui->home_nav_eq_img, &nav_eq_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_nav_eq_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_nav_eq_img, 0);
    lv_obj_set_x(bk_ui->home_nav_eq_img, 0);
    lv_obj_set_y(bk_ui->home_nav_eq_img, 3);
    lv_obj_set_width(bk_ui->home_nav_eq_img, 40);
    lv_obj_set_height(bk_ui->home_nav_eq_img, 40);
    lv_obj_remove_flag(bk_ui->home_nav_eq_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_nav_eq_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_eq_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_eq_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_eq_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_eq_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_eq_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_nav_eq_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_nav_eq_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_nav_eq_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_nav_eq_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_nav_eq_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_nav_set_img = lv_image_create(bk_ui->home_nav_grp);
    lv_image_set_src(bk_ui->home_nav_set_img, &nav_gear_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_nav_set_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_nav_set_img, 0);
    lv_obj_set_x(bk_ui->home_nav_set_img, 78);
    lv_obj_set_y(bk_ui->home_nav_set_img, 3);
    lv_obj_set_width(bk_ui->home_nav_set_img, 40);
    lv_obj_set_height(bk_ui->home_nav_set_img, 40);
    lv_obj_remove_flag(bk_ui->home_nav_set_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_nav_set_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_set_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_set_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_set_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_set_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_set_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_nav_set_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_nav_set_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_nav_set_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_nav_set_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_nav_set_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_nav_user_img = lv_image_create(bk_ui->home_nav_grp);
    lv_image_set_src(bk_ui->home_nav_user_img, &nav_user_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_nav_user_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_nav_user_img, 0);
    lv_obj_set_x(bk_ui->home_nav_user_img, 150);
    lv_obj_set_y(bk_ui->home_nav_user_img, 3);
    lv_obj_set_width(bk_ui->home_nav_user_img, 40);
    lv_obj_set_height(bk_ui->home_nav_user_img, 40);
    lv_obj_remove_flag(bk_ui->home_nav_user_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_nav_user_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_user_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_user_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_user_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_user_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_user_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_nav_user_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_nav_user_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_nav_user_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_nav_user_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_nav_user_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_nav_wifi_img = lv_image_create(bk_ui->home_nav_grp);
    lv_image_set_src(bk_ui->home_nav_wifi_img, &nav_wifi_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_nav_wifi_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_nav_wifi_img, 0);
    lv_obj_set_x(bk_ui->home_nav_wifi_img, 218);
    lv_obj_set_y(bk_ui->home_nav_wifi_img, 3);
    lv_obj_set_width(bk_ui->home_nav_wifi_img, 40);
    lv_obj_set_height(bk_ui->home_nav_wifi_img, 40);
    lv_obj_remove_flag(bk_ui->home_nav_wifi_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_nav_wifi_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_wifi_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_wifi_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_wifi_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_wifi_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_wifi_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_nav_wifi_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_nav_wifi_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_nav_wifi_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_nav_wifi_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_nav_wifi_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->home_nav_bat_img = lv_image_create(bk_ui->home_nav_grp);
    lv_image_set_src(bk_ui->home_nav_bat_img, &nav_battery_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->home_nav_bat_img, 50, 50);
    lv_image_set_rotation(bk_ui->home_nav_bat_img, 0);
    lv_obj_set_x(bk_ui->home_nav_bat_img, 288);
    lv_obj_set_y(bk_ui->home_nav_bat_img, 3);
    lv_obj_set_width(bk_ui->home_nav_bat_img, 40);
    lv_obj_set_height(bk_ui->home_nav_bat_img, 40);
    lv_obj_remove_flag(bk_ui->home_nav_bat_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->home_nav_bat_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->home_nav_bat_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->home_nav_bat_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->home_nav_bat_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->home_nav_bat_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->home_nav_bat_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->home_nav_bat_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->home_nav_bat_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->home_nav_bat_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->home_nav_bat_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->home_nav_bat_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->home);
}

/*
 * @brief: destroy page home
 */
void destroy_page_home(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->home != NULL) {
        lv_obj_del(bk_ui->home);
        bk_ui->home = NULL;
    }
}