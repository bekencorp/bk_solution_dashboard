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
#include "klok_mv_render.h"
#include "klok_player_adapter.h"
#include "video_play_engine_api.h"
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


/**
 * @brief Event callback for mv_play_back_btn - handles all events
 * @param e LVGL event object
 */
static bool s_mv_return_pending = false;

static void mv_play_return_to_song_list_async(void *user_data)
{
    (void)user_data;
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    int prepared = klok_player_begin_output_switch(
        KLOK_PLAYER_OUTPUT_FRAME_PREVIEW);
    if (prepared < 0) {
        s_mv_return_pending = false;
        return;
    }

    klok_mv_render_leave_locked();
    navigate_to_screen(&bk_lv_tool_ui.song_list,
                       LV_SCR_LOAD_ANIM_NONE,
                       300,
                       0,
                       false,
                       init_page_song_list);
    if (prepared == 0) {
        (void)klok_player_complete_output_switch(NULL);
    }
    s_mv_return_pending = false;
#else
    klok_mv_render_leave_locked();
    (void)video_play_engine_api_reassert_audio_format();
    navigate_to_screen(&bk_lv_tool_ui.song_list,
                       LV_SCR_LOAD_ANIM_NONE,
                       300,
                       0,
                       false,
                       init_page_song_list);
#endif
}

void mv_play_back_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
        if (s_mv_return_pending || klok_player_is_switching()) {
            return;
        }
        s_mv_return_pending = true;
#endif
        if (lv_async_call(mv_play_return_to_song_list_async, NULL) != LV_RESULT_OK) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
            s_mv_return_pending = false;
#endif
        }
    }
}

static void mv_play_set_pause_state(bool paused)
{
    if (bk_lv_tool_ui.mv_play_pause_txt == NULL ||
        !lv_obj_is_valid(bk_lv_tool_ui.mv_play_pause_txt)) {
        return;
    }

    lv_label_set_text(bk_lv_tool_ui.mv_play_pause_txt, paused ? "播放" : "暂停");
    lv_obj_set_style_text_font(
        bk_lv_tool_ui.mv_play_pause_txt,
        paused ? &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18
               : &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17,
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void mv_play_ctrl_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED &&
        klok_player_next() == 0) {
        mv_play_set_pause_state(false);
    }
}

static void mv_play_ctrl_pause_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (klok_player_pause_toggle() == 0) {
        mv_play_set_pause_state(klok_player_is_paused());
    }
}

static void mv_play_ctrl_replay_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED &&
        klok_player_replay() == 0) {
        mv_play_set_pause_state(false);
    }
}

static void mv_play_ctrl_duet_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED ||
        bk_lv_tool_ui.mv_play_duet_txt == NULL ||
        !lv_obj_is_valid(bk_lv_tool_ui.mv_play_duet_txt)) {
        return;
    }

    bool is_accompany = klok_player_is_accompany();
    int ret = is_accompany ? klok_player_vocal() : klok_player_accompany();
    if (ret == 0) {
        lv_label_set_text(bk_lv_tool_ui.mv_play_duet_txt,
                          is_accompany ? "伴唱" : "原唱");
        lv_obj_set_style_text_font(
            bk_lv_tool_ui.mv_play_duet_txt,
            is_accompany ? &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17
                         : &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18,
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void mv_play_ctrl_queue_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
        if (s_mv_return_pending || klok_player_is_switching()) {
            return;
        }
        s_mv_return_pending = true;
#endif
        if (lv_async_call(mv_play_return_to_song_list_async, NULL) != LV_RESULT_OK) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
            s_mv_return_pending = false;
#endif
        }
    }
}

static void mv_play_blend_lifecycle_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCREEN_LOAD_START) {
        klok_mv_render_prepare_locked();
    } else if (code == LV_EVENT_SCREEN_UNLOADED) {
        klok_mv_render_leave_locked();
    }
}


/*
 * @brief: init page mv_play
 */
void init_page_mv_play(bk_lv_ui_t *bk_ui)
{
    klok_mv_render_leave_locked();

    if (bk_ui->mv_play != NULL && lv_obj_is_valid(bk_ui->mv_play)) {
        destroy_page_mv_play(bk_ui);
    }
    

    bk_ui->mv_play = lv_obj_create(NULL);
    lv_obj_add_event_cb(bk_ui->mv_play,
                        mv_play_blend_lifecycle_event_cb,
                        LV_EVENT_ALL,
                        NULL);
    lv_obj_set_scrollbar_mode(bk_ui->mv_play, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->mv_play, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->mv_play, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_mv_root = lv_obj_create(bk_ui->mv_play);
    lv_obj_set_x(bk_ui->mv_play_mv_root, 0);
    lv_obj_set_y(bk_ui->mv_play_mv_root, 0);
    lv_obj_set_width(bk_ui->mv_play_mv_root, 1280);
    lv_obj_set_height(bk_ui->mv_play_mv_root, 720);
    lv_obj_set_style_bg_color(bk_ui->mv_play_mv_root, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_mv_root, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_mv_root, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_mv_root, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_mv_root, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_mv_root, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_mv_root, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_mv_root, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_mv_root, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_mv_root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->mv_play_mv_root, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_mv_root, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_mv_root, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_mv_root, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_mv_root, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_mv_root, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_mv_root, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_mv_root, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_mv_root, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->mv_play_video_img = lv_image_create(bk_ui->mv_play_mv_root);
    lv_obj_set_x(bk_ui->mv_play_video_img, 0);
    lv_obj_set_y(bk_ui->mv_play_video_img, 0);
    lv_obj_set_width(bk_ui->mv_play_video_img, 1280);
    lv_obj_set_height(bk_ui->mv_play_video_img, 720);
    lv_obj_set_style_bg_color(bk_ui->mv_play_video_img, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_video_img, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_video_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_back_btn = lv_btn_create(bk_ui->mv_play_mv_root);
    bk_ui->mv_play_back_btn_label = lv_label_create(bk_ui->mv_play_back_btn);
    lv_label_set_text_static(bk_ui->mv_play_back_btn_label, "返回");
    lv_label_set_long_mode(bk_ui->mv_play_back_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_back_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_back_btn, 34);
    lv_obj_set_y(bk_ui->mv_play_back_btn, 24);
    lv_obj_set_width(bk_ui->mv_play_back_btn, 92);
    lv_obj_set_height(bk_ui->mv_play_back_btn, 34);
    lv_obj_set_style_bg_color(bk_ui->mv_play_back_btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_back_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_back_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_back_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_back_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_back_btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_back_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_back_btn, lv_color_hex(0xb9d8ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_back_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_back_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_back_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_back_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_back_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->mv_play_back_btn, mv_play_back_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->mv_play_qr_card = lv_obj_create(bk_ui->mv_play_mv_root);
    lv_obj_set_x(bk_ui->mv_play_qr_card, 1084);
    lv_obj_set_y(bk_ui->mv_play_qr_card, 28);
    lv_obj_set_width(bk_ui->mv_play_qr_card, 160);
    lv_obj_set_height(bk_ui->mv_play_qr_card, 250);
    lv_obj_set_style_bg_color(bk_ui->mv_play_qr_card, lv_color_hex(0xd7e7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_qr_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_qr_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_qr_card, lv_color_hex(0x7daeff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_qr_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_qr_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_qr_card, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_qr_card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_qr_card, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_qr_card, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_qr_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_qr_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->mv_play_qr_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_qr_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_qr_card, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_qr_card, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_qr_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_qr_card, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_qr_card, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_qr_card, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_qr_card, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

#if LV_USE_QRCODE
    bk_ui->mv_play_qr_code = lv_qrcode_create(bk_ui->mv_play_qr_card);
    lv_qrcode_set_size(bk_ui->mv_play_qr_code, 132);
    lv_qrcode_set_dark_color(bk_ui->mv_play_qr_code, lv_color_hex(0x305887));
    lv_qrcode_set_light_color(bk_ui->mv_play_qr_code, lv_color_hex(0xeef6ff));
    lv_qrcode_update(bk_ui->mv_play_qr_code, "klok://order/mv_play", strlen("klok://order/mv_play"));
#else
    bk_ui->mv_play_qr_code = lv_obj_create(bk_ui->mv_play_qr_card);
#endif
    lv_obj_set_x(bk_ui->mv_play_qr_code, 14);
    lv_obj_set_y(bk_ui->mv_play_qr_code, 12);
    lv_obj_set_width(bk_ui->mv_play_qr_code, 132);
    lv_obj_set_height(bk_ui->mv_play_qr_code, 132);
    lv_obj_set_style_bg_color(bk_ui->mv_play_qr_code, lv_color_hex(0xeef6ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_qr_code, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_qr_code, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_qr_code, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_qr_code, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_qr_code, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_qr_code, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_qr_code, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_qr_code, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_qr_code, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_qr_code, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_qr_tip = lv_label_create(bk_ui->mv_play_qr_card);
    lv_label_set_text_static(bk_ui->mv_play_qr_tip, "手机扫码点歌");
    lv_label_set_long_mode(bk_ui->mv_play_qr_tip, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_qr_tip, 10);
    lv_obj_set_y(bk_ui->mv_play_qr_tip, 152);
    lv_obj_set_width(bk_ui->mv_play_qr_tip, 140);
    lv_obj_set_height(bk_ui->mv_play_qr_tip, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_qr_tip, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_qr_tip, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_qr_tip, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_qr_tip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_qr_tip, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_qr_tip, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_qr_tip, lv_color_hex(0x5b8fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_qr_tip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_qr_tip, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_qr_tip, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_qr_tip, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_qr_tip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_qr_tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_fix_btn = lv_btn_create(bk_ui->mv_play_qr_card);
    bk_ui->mv_play_fix_btn_label = lv_label_create(bk_ui->mv_play_fix_btn);
    lv_label_set_text_static(bk_ui->mv_play_fix_btn_label, "");
    lv_label_set_long_mode(bk_ui->mv_play_fix_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_fix_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_fix_btn, 14);
    lv_obj_set_y(bk_ui->mv_play_fix_btn, 188);
    lv_obj_set_width(bk_ui->mv_play_fix_btn, 132);
    lv_obj_set_height(bk_ui->mv_play_fix_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->mv_play_fix_btn, lv_color_hex(0xd2e5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_fix_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_fix_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_fix_btn, lv_color_hex(0x9fc1f2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_fix_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_fix_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_fix_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_fix_btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_fix_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_fix_btn, lv_color_hex(0x527eb9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_fix_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_fix_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_fix_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_fix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_fix_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_fix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_fix_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_fix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_fix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_fix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_star_ic = lv_image_create(bk_ui->mv_play_qr_card);
    lv_image_set_src(bk_ui->mv_play_star_ic, &mv_star_32x32_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->mv_play_star_ic, 50, 50);
    lv_image_set_rotation(bk_ui->mv_play_star_ic, 0);
    lv_obj_set_x(bk_ui->mv_play_star_ic, 24);
    lv_obj_set_y(bk_ui->mv_play_star_ic, 193);
    lv_obj_set_width(bk_ui->mv_play_star_ic, 32);
    lv_obj_set_height(bk_ui->mv_play_star_ic, 32);
    lv_obj_set_style_bg_color(bk_ui->mv_play_star_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_star_ic, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_star_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_star_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_star_ic, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_star_ic, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_star_ic, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_star_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->mv_play_star_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->mv_play_star_ic, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->mv_play_star_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_fix_txt = lv_label_create(bk_ui->mv_play_qr_card);
    lv_label_set_text(bk_ui->mv_play_fix_txt, "开启固定");
    lv_label_set_long_mode(bk_ui->mv_play_fix_txt, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->mv_play_fix_txt, 56);
    lv_obj_set_y(bk_ui->mv_play_fix_txt, 198);
    lv_obj_set_width(bk_ui->mv_play_fix_txt, 76);
    lv_obj_set_height(bk_ui->mv_play_fix_txt, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_fix_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_fix_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_fix_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_fix_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_fix_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_fix_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_fix_txt, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_fix_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_fix_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_fix_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_fix_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_fix_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_fix_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_ctrl_next = lv_btn_create(bk_ui->mv_play_mv_root);
    bk_ui->mv_play_ctrl_next_label = lv_label_create(bk_ui->mv_play_ctrl_next);
    lv_label_set_text_static(bk_ui->mv_play_ctrl_next_label, "");
    lv_label_set_long_mode(bk_ui->mv_play_ctrl_next_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_ctrl_next_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_ctrl_next, 209);
    lv_obj_set_y(bk_ui->mv_play_ctrl_next, 24);
    lv_obj_set_width(bk_ui->mv_play_ctrl_next, 126);
    lv_obj_set_height(bk_ui->mv_play_ctrl_next, 56);
    lv_obj_set_style_bg_color(bk_ui->mv_play_ctrl_next, lv_color_hex(0x061224), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_ctrl_next, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_ctrl_next, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_ctrl_next, lv_color_hex(0x223d66), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_ctrl_next, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_ctrl_next, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_ctrl_next, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_ctrl_next, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_ctrl_next, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_ctrl_next, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_ctrl_next, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_ctrl_next, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_ctrl_next, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_ctrl_next, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_ctrl_next, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_ctrl_next, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_ctrl_next, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_ctrl_next, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_ctrl_next, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_ctrl_next, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_ctrl_pause = lv_btn_create(bk_ui->mv_play_mv_root);
    bk_ui->mv_play_ctrl_pause_label = lv_label_create(bk_ui->mv_play_ctrl_pause);
    lv_label_set_text_static(bk_ui->mv_play_ctrl_pause_label, "");
    lv_label_set_long_mode(bk_ui->mv_play_ctrl_pause_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_ctrl_pause_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_ctrl_pause, 393);
    lv_obj_set_y(bk_ui->mv_play_ctrl_pause, 24);
    lv_obj_set_width(bk_ui->mv_play_ctrl_pause, 126);
    lv_obj_set_height(bk_ui->mv_play_ctrl_pause, 56);
    lv_obj_set_style_bg_color(bk_ui->mv_play_ctrl_pause, lv_color_hex(0x061224), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_ctrl_pause, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_ctrl_pause, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_ctrl_pause, lv_color_hex(0x223d66), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_ctrl_pause, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_ctrl_pause, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_ctrl_pause, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_ctrl_pause, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_ctrl_pause, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_ctrl_pause, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_ctrl_pause, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_ctrl_pause, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_ctrl_pause, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_ctrl_pause, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_ctrl_pause, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_ctrl_pause, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_ctrl_pause, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_ctrl_pause, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_ctrl_pause, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_ctrl_pause, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_ctrl_replay = lv_btn_create(bk_ui->mv_play_mv_root);
    bk_ui->mv_play_ctrl_replay_label = lv_label_create(bk_ui->mv_play_ctrl_replay);
    lv_label_set_text_static(bk_ui->mv_play_ctrl_replay_label, "");
    lv_label_set_long_mode(bk_ui->mv_play_ctrl_replay_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_ctrl_replay_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_ctrl_replay, 577);
    lv_obj_set_y(bk_ui->mv_play_ctrl_replay, 24);
    lv_obj_set_width(bk_ui->mv_play_ctrl_replay, 126);
    lv_obj_set_height(bk_ui->mv_play_ctrl_replay, 56);
    lv_obj_set_style_bg_color(bk_ui->mv_play_ctrl_replay, lv_color_hex(0x061224), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_ctrl_replay, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_ctrl_replay, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_ctrl_replay, lv_color_hex(0x223d66), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_ctrl_replay, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_ctrl_replay, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_ctrl_replay, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_ctrl_replay, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_ctrl_replay, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_ctrl_replay, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_ctrl_replay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_ctrl_replay, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_ctrl_replay, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_ctrl_replay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_ctrl_replay, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_ctrl_replay, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_ctrl_replay, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_ctrl_replay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_ctrl_replay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_ctrl_replay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_ctrl_duet = lv_btn_create(bk_ui->mv_play_mv_root);
    bk_ui->mv_play_ctrl_duet_label = lv_label_create(bk_ui->mv_play_ctrl_duet);
    lv_label_set_text_static(bk_ui->mv_play_ctrl_duet_label, "");
    lv_label_set_long_mode(bk_ui->mv_play_ctrl_duet_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_ctrl_duet_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_ctrl_duet, 761);
    lv_obj_set_y(bk_ui->mv_play_ctrl_duet, 24);
    lv_obj_set_width(bk_ui->mv_play_ctrl_duet, 126);
    lv_obj_set_height(bk_ui->mv_play_ctrl_duet, 56);
    lv_obj_set_style_bg_color(bk_ui->mv_play_ctrl_duet, lv_color_hex(0x061224), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_ctrl_duet, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_ctrl_duet, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_ctrl_duet, lv_color_hex(0x223d66), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_ctrl_duet, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_ctrl_duet, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_ctrl_duet, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_ctrl_duet, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_ctrl_duet, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_ctrl_duet, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_ctrl_duet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_ctrl_duet, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_ctrl_duet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_ctrl_duet, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_ctrl_duet, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_ctrl_duet, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_ctrl_duet, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_ctrl_duet, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_ctrl_duet, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_ctrl_duet, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_ctrl_queue = lv_btn_create(bk_ui->mv_play_mv_root);
    bk_ui->mv_play_ctrl_queue_label = lv_label_create(bk_ui->mv_play_ctrl_queue);
    lv_label_set_text_static(bk_ui->mv_play_ctrl_queue_label, "");
    lv_label_set_long_mode(bk_ui->mv_play_ctrl_queue_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->mv_play_ctrl_queue_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->mv_play_ctrl_queue, 945);
    lv_obj_set_y(bk_ui->mv_play_ctrl_queue, 24);
    lv_obj_set_width(bk_ui->mv_play_ctrl_queue, 126);
    lv_obj_set_height(bk_ui->mv_play_ctrl_queue, 56);
    lv_obj_set_style_bg_color(bk_ui->mv_play_ctrl_queue, lv_color_hex(0x061224), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_ctrl_queue, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_ctrl_queue, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_ctrl_queue, lv_color_hex(0x223d66), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_ctrl_queue, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_ctrl_queue, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_ctrl_queue, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_ctrl_queue, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_ctrl_queue, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_ctrl_queue, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_ctrl_queue, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_ctrl_queue, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_ctrl_queue, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_ctrl_queue, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_ctrl_queue, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_ctrl_queue, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_ctrl_queue, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_ctrl_queue, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_ctrl_queue, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_ctrl_queue, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_next_ic = lv_image_create(bk_ui->mv_play_mv_root);
    lv_image_set_src(bk_ui->mv_play_next_ic, &mv_next_32x32_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->mv_play_next_ic, 50, 50);
    lv_image_set_rotation(bk_ui->mv_play_next_ic, 0);
    lv_obj_set_x(bk_ui->mv_play_next_ic, 229);
    lv_obj_set_y(bk_ui->mv_play_next_ic, 36);
    lv_obj_set_width(bk_ui->mv_play_next_ic, 32);
    lv_obj_set_height(bk_ui->mv_play_next_ic, 32);
    lv_obj_set_style_bg_color(bk_ui->mv_play_next_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_next_ic, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_next_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_next_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_next_ic, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_next_ic, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_next_ic, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_next_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->mv_play_next_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->mv_play_next_ic, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->mv_play_next_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_next_txt = lv_label_create(bk_ui->mv_play_mv_root);
    lv_label_set_text_static(bk_ui->mv_play_next_txt, "下一首");
    lv_label_set_long_mode(bk_ui->mv_play_next_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_next_txt, 264);
    lv_obj_set_y(bk_ui->mv_play_next_txt, 40);
    lv_obj_set_width(bk_ui->mv_play_next_txt, 56);
    lv_obj_set_height(bk_ui->mv_play_next_txt, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_next_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_next_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_next_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_next_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_next_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_next_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_next_txt, lv_color_hex(0xd8ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_next_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_next_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_next_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_next_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_next_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_next_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_pause_ic = lv_image_create(bk_ui->mv_play_mv_root);
    lv_image_set_src(bk_ui->mv_play_pause_ic, &mv_pause_32x32_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->mv_play_pause_ic, 50, 50);
    lv_image_set_rotation(bk_ui->mv_play_pause_ic, 0);
    lv_obj_set_x(bk_ui->mv_play_pause_ic, 407);
    lv_obj_set_y(bk_ui->mv_play_pause_ic, 36);
    lv_obj_set_width(bk_ui->mv_play_pause_ic, 32);
    lv_obj_set_height(bk_ui->mv_play_pause_ic, 32);
    lv_obj_set_style_bg_color(bk_ui->mv_play_pause_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_pause_ic, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_pause_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_pause_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_pause_ic, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_pause_ic, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_pause_ic, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_pause_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->mv_play_pause_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->mv_play_pause_ic, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->mv_play_pause_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_pause_txt = lv_label_create(bk_ui->mv_play_mv_root);
    lv_label_set_text_static(bk_ui->mv_play_pause_txt, "暂停");
    lv_label_set_long_mode(bk_ui->mv_play_pause_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_pause_txt, 445);
    lv_obj_set_y(bk_ui->mv_play_pause_txt, 40);
    lv_obj_set_width(bk_ui->mv_play_pause_txt, 44);
    lv_obj_set_height(bk_ui->mv_play_pause_txt, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_pause_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_pause_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_pause_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_pause_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_pause_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_pause_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_pause_txt, lv_color_hex(0xd8ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_pause_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_pause_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_pause_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_pause_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_pause_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_pause_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_replay_ic = lv_image_create(bk_ui->mv_play_mv_root);
    lv_image_set_src(bk_ui->mv_play_replay_ic, &mv_replay_32x32_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->mv_play_replay_ic, 50, 50);
    lv_image_set_rotation(bk_ui->mv_play_replay_ic, 0);
    lv_obj_set_x(bk_ui->mv_play_replay_ic, 588);
    lv_obj_set_y(bk_ui->mv_play_replay_ic, 36);
    lv_obj_set_width(bk_ui->mv_play_replay_ic, 32);
    lv_obj_set_height(bk_ui->mv_play_replay_ic, 32);
    lv_obj_set_style_bg_color(bk_ui->mv_play_replay_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_replay_ic, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_replay_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_replay_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_replay_ic, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_replay_ic, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_replay_ic, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_replay_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->mv_play_replay_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->mv_play_replay_ic, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->mv_play_replay_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_replay_txt = lv_label_create(bk_ui->mv_play_mv_root);
    lv_label_set_text_static(bk_ui->mv_play_replay_txt, "重唱");
    lv_label_set_long_mode(bk_ui->mv_play_replay_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_replay_txt, 625);
    lv_obj_set_y(bk_ui->mv_play_replay_txt, 40);
    lv_obj_set_width(bk_ui->mv_play_replay_txt, 44);
    lv_obj_set_height(bk_ui->mv_play_replay_txt, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_replay_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_replay_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_replay_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_replay_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_replay_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_replay_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_replay_txt, lv_color_hex(0xd8ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_replay_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_replay_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_replay_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_replay_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_replay_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_replay_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_duet_ic = lv_image_create(bk_ui->mv_play_mv_root);
    lv_image_set_src(bk_ui->mv_play_duet_ic, &mv_mic_32x32_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->mv_play_duet_ic, 50, 50);
    lv_image_set_rotation(bk_ui->mv_play_duet_ic, 0);
    lv_obj_set_x(bk_ui->mv_play_duet_ic, 778);
    lv_obj_set_y(bk_ui->mv_play_duet_ic, 36);
    lv_obj_set_width(bk_ui->mv_play_duet_ic, 32);
    lv_obj_set_height(bk_ui->mv_play_duet_ic, 32);
    lv_obj_set_style_bg_color(bk_ui->mv_play_duet_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_duet_ic, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_duet_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_duet_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_duet_ic, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_duet_ic, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_duet_ic, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_duet_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->mv_play_duet_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->mv_play_duet_ic, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->mv_play_duet_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_duet_txt = lv_label_create(bk_ui->mv_play_mv_root);
    lv_label_set_text(bk_ui->mv_play_duet_txt,
                      klok_player_is_accompany() ? "原唱" : "伴唱");
    lv_label_set_long_mode(bk_ui->mv_play_duet_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_duet_txt, 815);
    lv_obj_set_y(bk_ui->mv_play_duet_txt, 40);
    lv_obj_set_width(bk_ui->mv_play_duet_txt, 44);
    lv_obj_set_height(bk_ui->mv_play_duet_txt, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_duet_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_duet_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_duet_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_duet_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_duet_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_duet_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_duet_txt, lv_color_hex(0xd8ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_duet_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_duet_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_duet_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_duet_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_duet_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_duet_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_queue_ic = lv_image_create(bk_ui->mv_play_mv_root);
    lv_image_set_src(bk_ui->mv_play_queue_ic, &mv_list_32x32_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->mv_play_queue_ic, 50, 50);
    lv_image_set_rotation(bk_ui->mv_play_queue_ic, 0);
    lv_obj_set_x(bk_ui->mv_play_queue_ic, 959);
    lv_obj_set_y(bk_ui->mv_play_queue_ic, 36);
    lv_obj_set_width(bk_ui->mv_play_queue_ic, 32);
    lv_obj_set_height(bk_ui->mv_play_queue_ic, 32);
    lv_obj_set_style_bg_color(bk_ui->mv_play_queue_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_queue_ic, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_queue_ic, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_queue_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_queue_ic, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_queue_ic, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_queue_ic, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_queue_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->mv_play_queue_ic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->mv_play_queue_ic, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->mv_play_queue_ic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_queue_txt = lv_label_create(bk_ui->mv_play_mv_root);
    lv_label_set_text_static(bk_ui->mv_play_queue_txt, "已点");
    lv_label_set_long_mode(bk_ui->mv_play_queue_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_queue_txt, 996);
    lv_obj_set_y(bk_ui->mv_play_queue_txt, 40);
    lv_obj_set_width(bk_ui->mv_play_queue_txt, 44);
    lv_obj_set_height(bk_ui->mv_play_queue_txt, 24);
    lv_obj_set_style_bg_color(bk_ui->mv_play_queue_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_queue_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_queue_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_queue_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_queue_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_queue_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_queue_txt, lv_color_hex(0xd8ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_queue_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_queue_txt, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_queue_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_queue_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_queue_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_queue_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->mv_play_q_badge = lv_label_create(bk_ui->mv_play_mv_root);
    lv_label_set_text_static(bk_ui->mv_play_q_badge, "0");
    lv_label_set_long_mode(bk_ui->mv_play_q_badge, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->mv_play_q_badge, 1045);
    lv_obj_set_y(bk_ui->mv_play_q_badge, 15);
    lv_obj_set_width(bk_ui->mv_play_q_badge, 30);
    lv_obj_set_height(bk_ui->mv_play_q_badge, 30);
    lv_obj_set_style_bg_color(bk_ui->mv_play_q_badge, lv_color_hex(0x9c4dff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->mv_play_q_badge, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->mv_play_q_badge, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->mv_play_q_badge, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->mv_play_q_badge, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->mv_play_q_badge, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->mv_play_q_badge, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->mv_play_q_badge, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->mv_play_q_badge, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->mv_play_q_badge, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->mv_play_q_badge, &lv_font_Alibaba_PuHuiTi_2_0_95_ExtraBold_95_ExtraBold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->mv_play_q_badge, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->mv_play_q_badge, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->mv_play_q_badge, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->mv_play_q_badge, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(bk_ui->mv_play_ctrl_next,
                        mv_play_ctrl_next_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_add_event_cb(bk_ui->mv_play_ctrl_pause,
                        mv_play_ctrl_pause_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_add_event_cb(bk_ui->mv_play_ctrl_replay,
                        mv_play_ctrl_replay_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_add_event_cb(bk_ui->mv_play_ctrl_duet,
                        mv_play_ctrl_duet_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_add_event_cb(bk_ui->mv_play_ctrl_queue,
                        mv_play_ctrl_queue_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    lv_obj_update_layout(bk_ui->mv_play);
}

/*
 * @brief: destroy page mv_play
 */
void destroy_page_mv_play(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->mv_play != NULL) {
        lv_obj_del(bk_ui->mv_play);
        bk_ui->mv_play = NULL;
        bk_ui->mv_play_video_img = NULL;
    }
}