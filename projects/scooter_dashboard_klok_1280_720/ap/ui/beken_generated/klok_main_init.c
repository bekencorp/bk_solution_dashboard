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
#include "klok_player_adapter.h"
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
 * @brief Event callback for klok_main_name_card - handles all events
 * @param e LVGL event object
 */
void klok_main_name_card_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.song_list, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_song_list);
    }
}

/**
 * @brief Event callback for klok_main_singer_card - handles all events
 * @param e LVGL event object
 */
void klok_main_singer_card_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.song_list, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_song_list);
    }
}

/**
 * @brief Event callback for klok_main_mute_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_mute_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_mute_toggle() == 0) {
            lv_obj_set_style_bg_color(bk_ui->klok_main_mute_btn, lv_color_hex(0xd64c75), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(bk_ui->klok_main_mute_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/**
 * @brief Event callback for klok_main_vol_down_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_vol_down_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_volume_down() == 0) {
            lv_slider_set_value(bk_ui->klok_main_volume_bar,
                                klok_player_get_volume(),
                                LV_ANIM_OFF);
        }
    }
}

/**
 * @brief Event callback for klok_main_vol_up_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_vol_up_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_volume_up() == 0) {
            lv_slider_set_value(bk_ui->klok_main_volume_bar,
                                klok_player_get_volume(),
                                LV_ANIM_OFF);
        }
    }
}

/**
 * @brief Event callback for klok_main_vocal_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_vocal_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_set_style_bg_color(bk_ui->klok_main_vocal_btn, lv_color_hex(0x356eda), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(bk_ui->klok_main_vocal_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/**
 * @brief Event callback for klok_main_replay_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_replay_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_set_style_bg_color(bk_ui->klok_main_replay_btn, lv_color_hex(0x356eda), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(bk_ui->klok_main_replay_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/**
 * @brief Event callback for klok_main_next_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_next_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.mv_play, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false, init_page_mv_play);
    }
}

/**
 * @brief Event callback for klok_main_play_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_play_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.mv_play, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false, init_page_mv_play);
    }
}

/**
 * @brief Event callback for klok_main_full_btn - handles all events
 * @param e LVGL event object
 */
void klok_main_full_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.mv_play, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false, init_page_mv_play);
    }
}

/**
 * @brief Event callback for klok_main_volume_bar - handles all events
 * @param e LVGL event object
 */
void klok_main_volume_bar_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        (void)klok_player_set_volume(
            (uint8_t)lv_slider_get_value(bk_ui->klok_main_volume_bar));
        lv_obj_set_style_bg_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffd6f1), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
}


/*
 * @brief: init page klok_main
 */
void init_page_klok_main(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->klok_main != NULL && lv_obj_is_valid(bk_ui->klok_main)) {
        destroy_page_klok_main(bk_ui);
    }
    

    bk_ui->klok_main = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->klok_main, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->klok_main, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->klok_main, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_km_bg = lv_obj_create(bk_ui->klok_main);
    lv_obj_set_x(bk_ui->klok_main_km_bg, 0);
    lv_obj_set_y(bk_ui->klok_main_km_bg, 0);
    lv_obj_set_width(bk_ui->klok_main_km_bg, 1280);
    lv_obj_set_height(bk_ui->klok_main_km_bg, 720);
    lv_obj_remove_flag(bk_ui->klok_main_km_bg, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_km_bg, lv_color_hex(0x140936), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_km_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_km_bg, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(bk_ui->klok_main_km_bg, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(bk_ui->klok_main_km_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor(bk_ui->klok_main_km_bg, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_km_bg, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_km_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_km_bg, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_km_bg, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_km_bg, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_km_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_km_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_km_bg, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_km_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_km_bg, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_km_bg, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_km_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_km_bg, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_km_bg, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_km_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_km_bg, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_km_back = lv_label_create(bk_ui->klok_main_km_bg);
    lv_label_set_text(bk_ui->klok_main_km_back, "返回首页");
    lv_label_set_long_mode(bk_ui->klok_main_km_back, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_km_back, 28);
    lv_obj_set_y(bk_ui->klok_main_km_back, 38);
    lv_obj_set_width(bk_ui->klok_main_km_back, 160);
    lv_obj_set_height(bk_ui->klok_main_km_back, 30);
    lv_obj_remove_flag(bk_ui->klok_main_km_back, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_km_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_km_back, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_km_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_km_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_km_back, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_km_back, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_km_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_km_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_km_back, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_km_back, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_km_back, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_km_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_km_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_video_box = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_video_box, 22);
    lv_obj_set_y(bk_ui->klok_main_video_box, 92);
    lv_obj_set_width(bk_ui->klok_main_video_box, 576);
    lv_obj_set_height(bk_ui->klok_main_video_box, 304);
    lv_obj_remove_flag(bk_ui->klok_main_video_box, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_video_box, lv_color_hex(0x02020a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_video_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_video_box, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_video_box, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_video_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_video_box, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_video_box, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_video_box, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_video_box, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_video_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_video_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_video_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_video_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_video_box, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_video_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_video_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_video_box, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_video_box, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_video_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_video_box, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_fav_card = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_fav_card, 615);
    lv_obj_set_y(bk_ui->klok_main_fav_card, 92);
    lv_obj_set_width(bk_ui->klok_main_fav_card, 235);
    lv_obj_set_height(bk_ui->klok_main_fav_card, 310);
    lv_obj_remove_flag(bk_ui->klok_main_fav_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_fav_card, lv_color_hex(0xff155d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_fav_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_fav_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_fav_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_fav_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_fav_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_fav_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_fav_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_fav_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_fav_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_fav_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_fav_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_fav_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_fav_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_fav_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_fav_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_fav_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_fav_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_fav_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_fav_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_record_img = lv_image_create(bk_ui->klok_main_fav_card);
    lv_image_set_src(bk_ui->klok_main_record_img, &icon_record_220x220_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_record_img, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_record_img, 0);
    lv_obj_set_x(bk_ui->klok_main_record_img, 8);
    lv_obj_set_y(bk_ui->klok_main_record_img, -5);
    lv_obj_set_width(bk_ui->klok_main_record_img, 220);
    lv_obj_set_height(bk_ui->klok_main_record_img, 220);
    lv_obj_remove_flag(bk_ui->klok_main_record_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_record_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_record_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_record_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_record_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_record_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_record_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_record_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_record_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_record_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_record_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_record_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_fav_text = lv_label_create(bk_ui->klok_main_fav_card);
    lv_label_set_text(bk_ui->klok_main_fav_text, "我的收藏");
    lv_label_set_long_mode(bk_ui->klok_main_fav_text, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_fav_text, 22);
    lv_obj_set_y(bk_ui->klok_main_fav_text, 230);
    lv_obj_set_width(bk_ui->klok_main_fav_text, 190);
    lv_obj_set_height(bk_ui->klok_main_fav_text, 42);
    lv_obj_remove_flag(bk_ui->klok_main_fav_text, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_fav_text, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_fav_text, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_fav_text, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_fav_text, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_fav_text, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_fav_text, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_fav_text, lv_color_hex(0xfff4e6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_fav_text, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_fav_text, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_fav_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_fav_text, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_fav_text, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_fav_text, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_panel = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_rec_panel, 870);
    lv_obj_set_y(bk_ui->klok_main_rec_panel, 92);
    lv_obj_set_width(bk_ui->klok_main_rec_panel, 385);
    lv_obj_set_height(bk_ui->klok_main_rec_panel, 310);
    lv_obj_remove_flag(bk_ui->klok_main_rec_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_panel, lv_color_hex(0xd9ebff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_panel, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_panel, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_panel, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_panel, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_panel, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_title = lv_label_create(bk_ui->klok_main_rec_panel);
    lv_label_set_text(bk_ui->klok_main_rec_title, "歌曲推荐");
    lv_label_set_long_mode(bk_ui->klok_main_rec_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rec_title, 30);
    lv_obj_set_y(bk_ui->klok_main_rec_title, 24);
    lv_obj_set_width(bk_ui->klok_main_rec_title, 210);
    lv_obj_set_height(bk_ui->klok_main_rec_title, 30);
    lv_obj_remove_flag(bk_ui->klok_main_rec_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rec_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rec_title, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rec_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rec_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_more = lv_label_create(bk_ui->klok_main_rec_panel);
    lv_label_set_text(bk_ui->klok_main_rec_more, "更多 >");
    lv_label_set_long_mode(bk_ui->klok_main_rec_more, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rec_more, 286);
    lv_obj_set_y(bk_ui->klok_main_rec_more, 24);
    lv_obj_set_width(bk_ui->klok_main_rec_more, 86);
    lv_obj_set_height(bk_ui->klok_main_rec_more, 28);
    lv_obj_remove_flag(bk_ui->klok_main_rec_more, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_more, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_more, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_more, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_more, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_more, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_more, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rec_more, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rec_more, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rec_more, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rec_more, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_more, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_more, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_more, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_row1 = lv_label_create(bk_ui->klok_main_rec_panel);
    lv_label_set_text(bk_ui->klok_main_rec_row1, "后来 - 刘若英");
    lv_label_set_long_mode(bk_ui->klok_main_rec_row1, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rec_row1, 35);
    lv_obj_set_y(bk_ui->klok_main_rec_row1, 72);
    lv_obj_set_width(bk_ui->klok_main_rec_row1, 220);
    lv_obj_set_height(bk_ui->klok_main_rec_row1, 28);
    lv_obj_remove_flag(bk_ui->klok_main_rec_row1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_row1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_row1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_row1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_row1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_row1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_row1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rec_row1, lv_color_hex(0x5c6c88), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rec_row1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rec_row1, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rec_row1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_row1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_row1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_row2 = lv_label_create(bk_ui->klok_main_rec_panel);
    lv_label_set_text(bk_ui->klok_main_rec_row2, "朋友 - 周华健");
    lv_label_set_long_mode(bk_ui->klok_main_rec_row2, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rec_row2, 35);
    lv_obj_set_y(bk_ui->klok_main_rec_row2, 122);
    lv_obj_set_width(bk_ui->klok_main_rec_row2, 220);
    lv_obj_set_height(bk_ui->klok_main_rec_row2, 28);
    lv_obj_remove_flag(bk_ui->klok_main_rec_row2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_row2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_row2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_row2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_row2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_row2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_row2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rec_row2, lv_color_hex(0x5c6c88), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rec_row2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rec_row2, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rec_row2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_row2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_row2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_row3 = lv_label_create(bk_ui->klok_main_rec_panel);
    lv_label_set_text(bk_ui->klok_main_rec_row3, "小幸运 - 田馥甄");
    lv_label_set_long_mode(bk_ui->klok_main_rec_row3, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rec_row3, 35);
    lv_obj_set_y(bk_ui->klok_main_rec_row3, 172);
    lv_obj_set_width(bk_ui->klok_main_rec_row3, 220);
    lv_obj_set_height(bk_ui->klok_main_rec_row3, 28);
    lv_obj_remove_flag(bk_ui->klok_main_rec_row3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_row3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_row3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_row3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_row3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_row3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_row3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rec_row3, lv_color_hex(0x5c6c88), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rec_row3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rec_row3, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rec_row3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_row3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_row3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_row4 = lv_label_create(bk_ui->klok_main_rec_panel);
    lv_label_set_text(bk_ui->klok_main_rec_row4, "月亮代表我的心");
    lv_label_set_long_mode(bk_ui->klok_main_rec_row4, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rec_row4, 35);
    lv_obj_set_y(bk_ui->klok_main_rec_row4, 222);
    lv_obj_set_width(bk_ui->klok_main_rec_row4, 230);
    lv_obj_set_height(bk_ui->klok_main_rec_row4, 28);
    lv_obj_remove_flag(bk_ui->klok_main_rec_row4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_row4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_row4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_row4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_row4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_row4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_row4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rec_row4, lv_color_hex(0x5c6c88), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rec_row4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rec_row4, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rec_row4, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_row4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_row4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_mic1 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_mic1, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_mic1, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_mic1, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_mic1, 274);
    lv_obj_set_y(bk_ui->klok_main_rec_mic1, 66);
    lv_obj_set_width(bk_ui->klok_main_rec_mic1, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_mic1, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_mic1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_mic1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_mic1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_mic1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_mic1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_mic1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_mic1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_mic1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_mic1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_mic1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_mic1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_add1 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_add1, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_add1, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_add1, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_add1, 334);
    lv_obj_set_y(bk_ui->klok_main_rec_add1, 66);
    lv_obj_set_width(bk_ui->klok_main_rec_add1, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_add1, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_add1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_add1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_add1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_add1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_add1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_add1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_add1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_add1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_add1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_add1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_add1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_add1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_mic2 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_mic2, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_mic2, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_mic2, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_mic2, 274);
    lv_obj_set_y(bk_ui->klok_main_rec_mic2, 116);
    lv_obj_set_width(bk_ui->klok_main_rec_mic2, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_mic2, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_mic2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_mic2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_mic2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_mic2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_mic2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_mic2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_mic2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_mic2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_mic2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_mic2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_mic2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_add2 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_add2, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_add2, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_add2, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_add2, 334);
    lv_obj_set_y(bk_ui->klok_main_rec_add2, 116);
    lv_obj_set_width(bk_ui->klok_main_rec_add2, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_add2, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_add2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_add2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_add2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_add2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_add2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_add2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_add2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_add2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_add2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_add2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_add2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_add2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_mic3 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_mic3, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_mic3, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_mic3, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_mic3, 274);
    lv_obj_set_y(bk_ui->klok_main_rec_mic3, 166);
    lv_obj_set_width(bk_ui->klok_main_rec_mic3, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_mic3, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_mic3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_mic3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_mic3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_mic3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_mic3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_mic3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_mic3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_mic3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_mic3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_mic3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_mic3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_add3 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_add3, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_add3, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_add3, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_add3, 334);
    lv_obj_set_y(bk_ui->klok_main_rec_add3, 166);
    lv_obj_set_width(bk_ui->klok_main_rec_add3, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_add3, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_add3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_add3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_add3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_add3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_add3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_add3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_add3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_add3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_add3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_add3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_add3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_add3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_mic4 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_mic4, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_mic4, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_mic4, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_mic4, 274);
    lv_obj_set_y(bk_ui->klok_main_rec_mic4, 216);
    lv_obj_set_width(bk_ui->klok_main_rec_mic4, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_mic4, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_mic4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_mic4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_mic4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_mic4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_mic4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_mic4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_mic4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_mic4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_mic4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_mic4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_mic4, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rec_add4 = lv_image_create(bk_ui->klok_main_rec_panel);
    lv_image_set_src(bk_ui->klok_main_rec_add4, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_rec_add4, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_rec_add4, 0);
    lv_obj_set_x(bk_ui->klok_main_rec_add4, 334);
    lv_obj_set_y(bk_ui->klok_main_rec_add4, 216);
    lv_obj_set_width(bk_ui->klok_main_rec_add4, 36);
    lv_obj_set_height(bk_ui->klok_main_rec_add4, 36);
    lv_obj_remove_flag(bk_ui->klok_main_rec_add4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rec_add4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rec_add4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rec_add4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rec_add4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rec_add4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rec_add4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rec_add4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rec_add4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_rec_add4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_rec_add4, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_rec_add4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_name_card = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_name_card, 22);
    lv_obj_set_y(bk_ui->klok_main_name_card, 430);
    lv_obj_set_width(bk_ui->klok_main_name_card, 395);
    lv_obj_set_height(bk_ui->klok_main_name_card, 210);
    lv_obj_remove_flag(bk_ui->klok_main_name_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_name_card, lv_color_hex(0xff20b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_name_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_name_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_name_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_name_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_name_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_name_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_name_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_name_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_name_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_name_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_name_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_name_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_name_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_name_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_name_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_name_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_name_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_name_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_name_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_name_card, klok_main_name_card_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_name_title = lv_label_create(bk_ui->klok_main_name_card);
    lv_label_set_text(bk_ui->klok_main_name_title, "歌名点歌");
    lv_label_set_long_mode(bk_ui->klok_main_name_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_name_title, 32);
    lv_obj_set_y(bk_ui->klok_main_name_title, 76);
    lv_obj_set_width(bk_ui->klok_main_name_title, 245);
    lv_obj_set_height(bk_ui->klok_main_name_title, 46);
    lv_obj_remove_flag(bk_ui->klok_main_name_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_name_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_name_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_name_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_name_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_name_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_name_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_name_title, lv_color_hex(0xfff4ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_name_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_name_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_name_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_name_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_name_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_name_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_name_hint = lv_label_create(bk_ui->klok_main_name_card);
    lv_label_set_text(bk_ui->klok_main_name_hint, "输入歌名快速搜索");
    lv_label_set_long_mode(bk_ui->klok_main_name_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_name_hint, 34);
    lv_obj_set_y(bk_ui->klok_main_name_hint, 122);
    lv_obj_set_width(bk_ui->klok_main_name_hint, 250);
    lv_obj_set_height(bk_ui->klok_main_name_hint, 24);
    lv_obj_remove_flag(bk_ui->klok_main_name_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_name_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_name_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_name_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_name_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_name_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_name_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_name_hint, lv_color_hex(0xffd7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_name_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_name_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_name_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_name_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_name_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_name_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_search_img = lv_image_create(bk_ui->klok_main_name_card);
    lv_image_set_src(bk_ui->klok_main_search_img, &icon_search_96x96_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_search_img, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_search_img, 0);
    lv_obj_set_x(bk_ui->klok_main_search_img, 292);
    lv_obj_set_y(bk_ui->klok_main_search_img, 62);
    lv_obj_set_width(bk_ui->klok_main_search_img, 96);
    lv_obj_set_height(bk_ui->klok_main_search_img, 96);
    lv_obj_remove_flag(bk_ui->klok_main_search_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_search_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_search_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_search_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_search_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_search_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_search_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_search_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_search_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_search_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_search_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_search_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_singer_card = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_singer_card, 442);
    lv_obj_set_y(bk_ui->klok_main_singer_card, 428);
    lv_obj_set_width(bk_ui->klok_main_singer_card, 405);
    lv_obj_set_height(bk_ui->klok_main_singer_card, 210);
    lv_obj_remove_flag(bk_ui->klok_main_singer_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_singer_card, lv_color_hex(0x078efa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_singer_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_singer_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_singer_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_singer_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_singer_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_singer_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_singer_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_singer_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_singer_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_singer_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_singer_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_singer_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_singer_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_singer_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_singer_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_singer_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_singer_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_singer_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_singer_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_singer_card, klok_main_singer_card_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_singer_title = lv_label_create(bk_ui->klok_main_singer_card);
    lv_label_set_text(bk_ui->klok_main_singer_title, "歌手点歌");
    lv_label_set_long_mode(bk_ui->klok_main_singer_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_singer_title, 38);
    lv_obj_set_y(bk_ui->klok_main_singer_title, 76);
    lv_obj_set_width(bk_ui->klok_main_singer_title, 245);
    lv_obj_set_height(bk_ui->klok_main_singer_title, 46);
    lv_obj_remove_flag(bk_ui->klok_main_singer_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_singer_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_singer_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_singer_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_singer_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_singer_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_singer_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_singer_title, lv_color_hex(0xf2fbff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_singer_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_singer_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_singer_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_singer_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_singer_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_singer_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_singer_hint = lv_label_create(bk_ui->klok_main_singer_card);
    lv_label_set_text(bk_ui->klok_main_singer_hint, "按歌手分类查找");
    lv_label_set_long_mode(bk_ui->klok_main_singer_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_singer_hint, 40);
    lv_obj_set_y(bk_ui->klok_main_singer_hint, 122);
    lv_obj_set_width(bk_ui->klok_main_singer_hint, 250);
    lv_obj_set_height(bk_ui->klok_main_singer_hint, 24);
    lv_obj_remove_flag(bk_ui->klok_main_singer_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_singer_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_singer_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_singer_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_singer_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_singer_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_singer_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_singer_hint, lv_color_hex(0xd7efff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_singer_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_singer_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_singer_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_singer_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_singer_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_singer_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_crown_img = lv_image_create(bk_ui->klok_main_singer_card);
    lv_image_set_src(bk_ui->klok_main_crown_img, &icon_crown_96x96_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_crown_img, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_crown_img, 0);
    lv_obj_set_x(bk_ui->klok_main_crown_img, 292);
    lv_obj_set_y(bk_ui->klok_main_crown_img, 62);
    lv_obj_set_width(bk_ui->klok_main_crown_img, 96);
    lv_obj_set_height(bk_ui->klok_main_crown_img, 96);
    lv_obj_remove_flag(bk_ui->klok_main_crown_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_crown_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_crown_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_crown_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_crown_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_crown_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_crown_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_crown_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_crown_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_crown_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_crown_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_crown_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rank_card = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_rank_card, 870);
    lv_obj_set_y(bk_ui->klok_main_rank_card, 430);
    lv_obj_set_width(bk_ui->klok_main_rank_card, 385);
    lv_obj_set_height(bk_ui->klok_main_rank_card, 210);
    lv_obj_remove_flag(bk_ui->klok_main_rank_card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rank_card, lv_color_hex(0xbd25f2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rank_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rank_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rank_card, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rank_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rank_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rank_card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rank_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rank_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rank_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rank_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rank_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rank_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rank_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rank_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rank_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rank_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rank_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rank_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rank_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_rank_title = lv_label_create(bk_ui->klok_main_rank_card);
    lv_label_set_text(bk_ui->klok_main_rank_title, "排行榜");
    lv_label_set_long_mode(bk_ui->klok_main_rank_title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rank_title, 38);
    lv_obj_set_y(bk_ui->klok_main_rank_title, 76);
    lv_obj_set_width(bk_ui->klok_main_rank_title, 235);
    lv_obj_set_height(bk_ui->klok_main_rank_title, 46);
    lv_obj_remove_flag(bk_ui->klok_main_rank_title, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rank_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rank_title, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rank_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rank_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rank_title, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rank_title, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rank_title, lv_color_hex(0xfff0ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rank_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rank_title, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_29, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rank_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rank_title, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rank_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rank_title, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_rank_hint = lv_label_create(bk_ui->klok_main_rank_card);
    lv_label_set_text(bk_ui->klok_main_rank_hint, "热门金曲实时榜");
    lv_label_set_long_mode(bk_ui->klok_main_rank_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_rank_hint, 40);
    lv_obj_set_y(bk_ui->klok_main_rank_hint, 122);
    lv_obj_set_width(bk_ui->klok_main_rank_hint, 250);
    lv_obj_set_height(bk_ui->klok_main_rank_hint, 24);
    lv_obj_remove_flag(bk_ui->klok_main_rank_hint, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_rank_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_rank_hint, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_rank_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_rank_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_rank_hint, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_rank_hint, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_rank_hint, lv_color_hex(0xf7d7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_rank_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_rank_hint, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_rank_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_rank_hint, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_rank_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_rank_hint, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_trophy_img = lv_image_create(bk_ui->klok_main_rank_card);
    lv_image_set_src(bk_ui->klok_main_trophy_img, &icon_trophy_96x96_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_trophy_img, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_trophy_img, 0);
    lv_obj_set_x(bk_ui->klok_main_trophy_img, 270);
    lv_obj_set_y(bk_ui->klok_main_trophy_img, 55);
    lv_obj_set_width(bk_ui->klok_main_trophy_img, 96);
    lv_obj_set_height(bk_ui->klok_main_trophy_img, 96);
    lv_obj_remove_flag(bk_ui->klok_main_trophy_img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_trophy_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_trophy_img, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_trophy_img, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_trophy_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_trophy_img, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_trophy_img, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_trophy_img, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_trophy_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_trophy_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_trophy_img, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_trophy_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_bottom_bar = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_bottom_bar, 0);
    lv_obj_set_y(bk_ui->klok_main_bottom_bar, 650);
    lv_obj_set_width(bk_ui->klok_main_bottom_bar, 1280);
    lv_obj_set_height(bk_ui->klok_main_bottom_bar, 70);
    lv_obj_remove_flag(bk_ui->klok_main_bottom_bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_bottom_bar, lv_color_hex(0x46133f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_bottom_bar, 232, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_bottom_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_bottom_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_bottom_bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_bottom_bar, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_bottom_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_bottom_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_bottom_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_bottom_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_bottom_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_bottom_bar, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_bottom_bar, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_bottom_bar, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_bottom_bar, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_bottom_bar, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_bottom_bar, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_bottom_bar, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_bottom_bar, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_bottom_bar, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_bot_actions = lv_obj_create(bk_ui->klok_main_bottom_bar);
    lv_obj_set_x(bk_ui->klok_main_bot_actions, 0);
    lv_obj_set_y(bk_ui->klok_main_bot_actions, -2);
    lv_obj_set_width(bk_ui->klok_main_bot_actions, 1280);
    lv_obj_set_height(bk_ui->klok_main_bot_actions, 70);
    lv_obj_remove_flag(bk_ui->klok_main_bot_actions, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_bot_actions, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_bot_actions, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_bot_actions, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_bot_actions, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_bot_actions, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_bot_actions, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_bot_actions, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_bot_actions, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_bot_actions, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_bot_actions, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_bot_actions, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_bot_actions, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_bot_actions, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_bot_actions, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_bot_actions, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_bot_actions, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_bot_actions, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_mute_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_mute_btn_label = lv_label_create(bk_ui->klok_main_mute_btn);
    lv_label_set_text_static(bk_ui->klok_main_mute_btn_label, "X静音");
    lv_label_set_long_mode(bk_ui->klok_main_mute_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_mute_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_mute_btn, 28);
    lv_obj_set_y(bk_ui->klok_main_mute_btn, 14);
    lv_obj_set_width(bk_ui->klok_main_mute_btn, 96);
    lv_obj_set_height(bk_ui->klok_main_mute_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->klok_main_mute_btn, lv_color_hex(0x8f3b55), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_mute_btn, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_mute_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_mute_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_mute_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_mute_btn, 61, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_mute_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_mute_btn, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_mute_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_mute_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_mute_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_mute_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_mute_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_mute_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_mute_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_mute_btn, klok_main_mute_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_vol_down_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_vol_down_btn_label = lv_label_create(bk_ui->klok_main_vol_down_btn);
    lv_label_set_text_static(bk_ui->klok_main_vol_down_btn_label, "-");
    lv_label_set_long_mode(bk_ui->klok_main_vol_down_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_vol_down_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_vol_down_btn, 232);
    lv_obj_set_y(bk_ui->klok_main_vol_down_btn, 15);
    lv_obj_set_width(bk_ui->klok_main_vol_down_btn, 42);
    lv_obj_set_height(bk_ui->klok_main_vol_down_btn, 40);
    lv_obj_set_style_bg_color(bk_ui->klok_main_vol_down_btn, lv_color_hex(0xb65078), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_vol_down_btn, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_vol_down_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_vol_down_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_vol_down_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_vol_down_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_vol_down_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_vol_down_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_vol_down_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_vol_down_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_vol_down_btn, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_vol_down_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_vol_down_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_vol_down_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_vol_down_btn, klok_main_vol_down_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_vol_up_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_vol_up_btn_label = lv_label_create(bk_ui->klok_main_vol_up_btn);
    lv_label_set_text_static(bk_ui->klok_main_vol_up_btn_label, "+");
    lv_label_set_long_mode(bk_ui->klok_main_vol_up_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_vol_up_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_vol_up_btn, 408);
    lv_obj_set_y(bk_ui->klok_main_vol_up_btn, 15);
    lv_obj_set_width(bk_ui->klok_main_vol_up_btn, 42);
    lv_obj_set_height(bk_ui->klok_main_vol_up_btn, 40);
    lv_obj_set_style_bg_color(bk_ui->klok_main_vol_up_btn, lv_color_hex(0xd34d90), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_vol_up_btn, 240, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_vol_up_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_vol_up_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_vol_up_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_vol_up_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_vol_up_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_vol_up_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_vol_up_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_vol_up_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_vol_up_btn, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_vol_up_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_vol_up_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_vol_up_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_vol_up_btn, klok_main_vol_up_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_vocal_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_vocal_btn_label = lv_label_create(bk_ui->klok_main_vocal_btn);
    lv_label_set_text_static(bk_ui->klok_main_vocal_btn_label, "原唱");
    lv_label_set_long_mode(bk_ui->klok_main_vocal_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_vocal_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_vocal_btn, 486);
    lv_obj_set_y(bk_ui->klok_main_vocal_btn, 14);
    lv_obj_set_width(bk_ui->klok_main_vocal_btn, 92);
    lv_obj_set_height(bk_ui->klok_main_vocal_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->klok_main_vocal_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_vocal_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_vocal_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_vocal_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_vocal_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_vocal_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_vocal_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_vocal_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_vocal_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_vocal_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_vocal_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_vocal_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_vocal_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_vocal_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_vocal_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_vocal_btn, klok_main_vocal_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_replay_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_replay_btn_label = lv_label_create(bk_ui->klok_main_replay_btn);
    lv_label_set_text_static(bk_ui->klok_main_replay_btn_label, "重唱");
    lv_label_set_long_mode(bk_ui->klok_main_replay_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_replay_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_replay_btn, 596);
    lv_obj_set_y(bk_ui->klok_main_replay_btn, 14);
    lv_obj_set_width(bk_ui->klok_main_replay_btn, 92);
    lv_obj_set_height(bk_ui->klok_main_replay_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->klok_main_replay_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_replay_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_replay_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_replay_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_replay_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_replay_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_replay_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_replay_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_replay_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_replay_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_replay_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_replay_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_replay_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_replay_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_replay_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_replay_btn, klok_main_replay_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_next_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_next_btn_label = lv_label_create(bk_ui->klok_main_next_btn);
    lv_label_set_text_static(bk_ui->klok_main_next_btn_label, "切歌");
    lv_label_set_long_mode(bk_ui->klok_main_next_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_next_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_next_btn, 706);
    lv_obj_set_y(bk_ui->klok_main_next_btn, 14);
    lv_obj_set_width(bk_ui->klok_main_next_btn, 92);
    lv_obj_set_height(bk_ui->klok_main_next_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->klok_main_next_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_next_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_next_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_next_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_next_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_next_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_next_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_next_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_next_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_next_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_next_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_next_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_next_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_next_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_next_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_next_btn, klok_main_next_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_play_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_play_btn_label = lv_label_create(bk_ui->klok_main_play_btn);
    lv_label_set_text_static(bk_ui->klok_main_play_btn_label, "播放");
    lv_label_set_long_mode(bk_ui->klok_main_play_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_play_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_play_btn, 816);
    lv_obj_set_y(bk_ui->klok_main_play_btn, 14);
    lv_obj_set_width(bk_ui->klok_main_play_btn, 92);
    lv_obj_set_height(bk_ui->klok_main_play_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->klok_main_play_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_play_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_play_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_play_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_play_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_play_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_play_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_play_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_play_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_play_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_play_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_play_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_play_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_play_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_play_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_play_btn, klok_main_play_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_full_btn = lv_btn_create(bk_ui->klok_main_bot_actions);
    bk_ui->klok_main_full_btn_label = lv_label_create(bk_ui->klok_main_full_btn);
    lv_label_set_text_static(bk_ui->klok_main_full_btn_label, "全屏播放");
    lv_label_set_long_mode(bk_ui->klok_main_full_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->klok_main_full_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->klok_main_full_btn, 926);
    lv_obj_set_y(bk_ui->klok_main_full_btn, 14);
    lv_obj_set_width(bk_ui->klok_main_full_btn, 138);
    lv_obj_set_height(bk_ui->klok_main_full_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->klok_main_full_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_full_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_full_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_full_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_full_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_full_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_full_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_full_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_full_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_full_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_full_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_full_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_full_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_full_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_full_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_full_btn, klok_main_full_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_volume_bar = lv_slider_create(bk_ui->klok_main_bot_actions);
    lv_slider_set_range(bk_ui->klok_main_volume_bar, 0, 100);
    lv_slider_set_value(bk_ui->klok_main_volume_bar, klok_player_get_volume(), LV_ANIM_OFF);
    lv_slider_set_mode(bk_ui->klok_main_volume_bar, LV_SLIDER_MODE_NORMAL);
    lv_obj_set_x(bk_ui->klok_main_volume_bar, 290);
    lv_obj_set_y(bk_ui->klok_main_volume_bar, 30);
    lv_obj_set_width(bk_ui->klok_main_volume_bar, 108);
    lv_obj_set_height(bk_ui->klok_main_volume_bar, 10);
    lv_obj_remove_flag(bk_ui->klok_main_volume_bar, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_volume_bar, 136, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_volume_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_volume_bar, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_volume_bar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_volume_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_volume_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(bk_ui->klok_main_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_volume_bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_volume_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_volume_bar, LV_BORDER_SIDE_FULL, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_volume_bar, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_volume_bar, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_volume_bar, LV_GRAD_DIR_NONE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_volume_bar, lv_color_hex(0xffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_volume_bar, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_volume_bar, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_volume_bar, LV_BORDER_SIDE_FULL, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_volume_bar, 7, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_volume_bar, false, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->klok_main_volume_bar, klok_main_volume_bar_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->klok_main_top_status = lv_obj_create(bk_ui->klok_main_km_bg);
    lv_obj_set_x(bk_ui->klok_main_top_status, 650);
    lv_obj_set_y(bk_ui->klok_main_top_status, 26);
    lv_obj_set_width(bk_ui->klok_main_top_status, 605);
    lv_obj_set_height(bk_ui->klok_main_top_status, 44);
    lv_obj_remove_flag(bk_ui->klok_main_top_status, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_top_status, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_top_status, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_top_status, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_top_status, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_top_status, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_top_status, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_top_status, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_top_status, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_top_status, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_top_status, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_top_status, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_top_status, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_top_status, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_top_status, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_top_status, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_top_status, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_top_status, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_vip_box = lv_obj_create(bk_ui->klok_main_top_status);
    lv_obj_set_x(bk_ui->klok_main_vip_box, 0);
    lv_obj_set_y(bk_ui->klok_main_vip_box, 0);
    lv_obj_set_width(bk_ui->klok_main_vip_box, 255);
    lv_obj_set_height(bk_ui->klok_main_vip_box, 42);
    lv_obj_remove_flag(bk_ui->klok_main_vip_box, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_vip_box, lv_color_hex(0x111b34), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_vip_box, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_vip_box, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_vip_box, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_vip_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_vip_box, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_vip_box, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_vip_box, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_vip_box, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_vip_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_vip_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_vip_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_vip_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_vip_box, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_vip_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_vip_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_vip_box, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_vip_box, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_vip_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_vip_box, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_vip_txt = lv_label_create(bk_ui->klok_main_vip_box);
    lv_label_set_text(bk_ui->klok_main_vip_txt, "VIP 剩余");
    lv_label_set_long_mode(bk_ui->klok_main_vip_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_vip_txt, 18);
    lv_obj_set_y(bk_ui->klok_main_vip_txt, 10);
    lv_obj_set_width(bk_ui->klok_main_vip_txt, 138);
    lv_obj_set_height(bk_ui->klok_main_vip_txt, 22);
    lv_obj_remove_flag(bk_ui->klok_main_vip_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_vip_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_vip_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_vip_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_vip_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_vip_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_vip_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_vip_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_vip_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_vip_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_vip_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_vip_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_vip_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_vip_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_day_txt = lv_label_create(bk_ui->klok_main_vip_box);
    lv_label_set_text(bk_ui->klok_main_day_txt, "332天");
    lv_label_set_long_mode(bk_ui->klok_main_day_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_day_txt, 158);
    lv_obj_set_y(bk_ui->klok_main_day_txt, 6);
    lv_obj_set_width(bk_ui->klok_main_day_txt, 82);
    lv_obj_set_height(bk_ui->klok_main_day_txt, 30);
    lv_obj_remove_flag(bk_ui->klok_main_day_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_day_txt, lv_color_hex(0xffd17a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_day_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_day_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_day_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_day_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_day_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_day_txt, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_day_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_day_txt, lv_color_hex(0x9b6422), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_day_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_day_txt, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_day_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_day_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_day_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_day_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_mix_box = lv_obj_create(bk_ui->klok_main_top_status);
    lv_obj_set_x(bk_ui->klok_main_mix_box, 285);
    lv_obj_set_y(bk_ui->klok_main_mix_box, 0);
    lv_obj_set_width(bk_ui->klok_main_mix_box, 130);
    lv_obj_set_height(bk_ui->klok_main_mix_box, 42);
    lv_obj_remove_flag(bk_ui->klok_main_mix_box, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_mix_box, lv_color_hex(0x12374b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_mix_box, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_mix_box, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_mix_box, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_mix_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_mix_box, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_mix_box, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_mix_box, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_mix_box, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_mix_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_mix_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_mix_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_mix_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_mix_box, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_mix_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_mix_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_mix_box, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_mix_box, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_mix_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_mix_box, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_mix_txt = lv_label_create(bk_ui->klok_main_mix_box);
    lv_label_set_text(bk_ui->klok_main_mix_txt, "调音台");
    lv_label_set_long_mode(bk_ui->klok_main_mix_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_mix_txt, 14);
    lv_obj_set_y(bk_ui->klok_main_mix_txt, 10);
    lv_obj_set_width(bk_ui->klok_main_mix_txt, 70);
    lv_obj_set_height(bk_ui->klok_main_mix_txt, 22);
    lv_obj_remove_flag(bk_ui->klok_main_mix_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_mix_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_mix_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_mix_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_mix_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_mix_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_mix_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_mix_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_mix_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_mix_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_mix_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_mix_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_mix_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_mix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_mix_icon = lv_image_create(bk_ui->klok_main_mix_box);
    lv_image_set_src(bk_ui->klok_main_mix_icon, &nav_eq_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->klok_main_mix_icon, 50, 50);
    lv_image_set_rotation(bk_ui->klok_main_mix_icon, 0);
    lv_obj_set_x(bk_ui->klok_main_mix_icon, 87);
    lv_obj_set_y(bk_ui->klok_main_mix_icon, 3);
    lv_obj_set_width(bk_ui->klok_main_mix_icon, 40);
    lv_obj_set_height(bk_ui->klok_main_mix_icon, 40);
    lv_obj_remove_flag(bk_ui->klok_main_mix_icon, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_mix_icon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_mix_icon, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_mix_icon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_mix_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_mix_icon, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_mix_icon, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_mix_icon, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_mix_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->klok_main_mix_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->klok_main_mix_icon, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->klok_main_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_queue_box = lv_obj_create(bk_ui->klok_main_top_status);
    lv_obj_set_x(bk_ui->klok_main_queue_box, 445);
    lv_obj_set_y(bk_ui->klok_main_queue_box, 0);
    lv_obj_set_width(bk_ui->klok_main_queue_box, 158);
    lv_obj_set_height(bk_ui->klok_main_queue_box, 42);
    lv_obj_remove_flag(bk_ui->klok_main_queue_box, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_queue_box, lv_color_hex(0x173641), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_queue_box, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_queue_box, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_queue_box, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_queue_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_queue_box, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_queue_box, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_queue_box, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_queue_box, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_queue_box, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_queue_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->klok_main_queue_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_queue_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_queue_box, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_queue_box, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_queue_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_queue_box, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_queue_box, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_queue_box, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_queue_box, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->klok_main_queue_txt = lv_label_create(bk_ui->klok_main_queue_box);
    lv_label_set_text(bk_ui->klok_main_queue_txt, "已点");
    lv_label_set_long_mode(bk_ui->klok_main_queue_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_queue_txt, 12);
    lv_obj_set_y(bk_ui->klok_main_queue_txt, 10);
    lv_obj_set_width(bk_ui->klok_main_queue_txt, 92);
    lv_obj_set_height(bk_ui->klok_main_queue_txt, 22);
    lv_obj_remove_flag(bk_ui->klok_main_queue_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_queue_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_queue_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_queue_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_queue_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_queue_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_queue_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_queue_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_queue_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_queue_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_queue_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_queue_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_queue_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->klok_main_queue_num = lv_label_create(bk_ui->klok_main_queue_box);
    lv_label_set_text(bk_ui->klok_main_queue_num, "0");
    lv_label_set_long_mode(bk_ui->klok_main_queue_num, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->klok_main_queue_num, 112);
    lv_obj_set_y(bk_ui->klok_main_queue_num, 7);
    lv_obj_set_width(bk_ui->klok_main_queue_num, 36);
    lv_obj_set_height(bk_ui->klok_main_queue_num, 28);
    lv_obj_remove_flag(bk_ui->klok_main_queue_num, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->klok_main_queue_num, lv_color_hex(0xff8c00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->klok_main_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->klok_main_queue_num, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->klok_main_queue_num, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->klok_main_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->klok_main_queue_num, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->klok_main_queue_num, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->klok_main_queue_num, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->klok_main_queue_num, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->klok_main_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->klok_main_queue_num, &lv_font_Alibaba_PuHuiTi_2_0_95_ExtraBold_95_ExtraBold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->klok_main_queue_num, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->klok_main_queue_num, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->klok_main_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->klok_main_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->klok_main);
}

/*
 * @brief: destroy page klok_main
 */
void destroy_page_klok_main(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->klok_main != NULL) {
        lv_obj_del(bk_ui->klok_main);
        bk_ui->klok_main = NULL;
    }
}