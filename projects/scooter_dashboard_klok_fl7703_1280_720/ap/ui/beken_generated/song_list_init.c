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
#include "bk_posix.h"
#include "klok_lvgl_preview.h"
#include "klok_mv_render.h"
#include "klok_player_adapter.h"
#include "os/mem.h"
#include "os/os.h"
#include "lv_vendor.h"
#include "video_play_engine_api.h"
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

#define KLOK_SONG_LIST_MAX_ITEMS 64
#define KLOK_SONG_PATH_MAX 256

typedef struct {
    char file_path[KLOK_SONG_PATH_MAX];
} klok_song_play_request_t;

static volatile bool s_song_video_transition_pending = false;

#define KLOK_SONG_VIDEO_SWITCH_STACK_SIZE (8U * 1024U)

static void klok_song_list_start_video_worker(void *user_data)
{
    klok_song_play_request_t *request = (klok_song_play_request_t *)user_data;
    if (request == NULL) {
        rtos_delete_thread(NULL);
        return;
    }

    /*
     * Do not deinitialize LVGL's VG-Lite instance from inside lv_task_handler.
     * Running the ownership transfer on a worker lets the current LVGL callback
     * return first; the display mutex then serializes all LVGL object/timer
     * access while the frame decoder is replaced by the Flexa decoder.
     */
    if (request->file_path[0] != '\0' || !klok_player_is_started()) {
        klok_player_acquire_ktv_audio_focus();
    }
    lv_vendor_disp_lock();
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    int prepared = klok_player_begin_output_switch(
        KLOK_PLAYER_OUTPUT_FLEXA_DIRECT);
    if (prepared >= 0) {
        /*
         * The old video decoder is now quiesced and destroyed, while the
         * engine/parser/audio pipeline remains live. Flexa can take VG-Lite
         * before the target decoder handle is created.
         */
        if (klok_mv_render_enter_locked() == BK_OK) {
            if (prepared == 0) {
                (void)klok_player_complete_output_switch(
                    request->file_path[0] != '\0'
                        ? request->file_path
                        : NULL);
            } else if (request->file_path[0] != '\0') {
                (void)klok_player_play_file(request->file_path);
            } else if (!klok_player_is_started()) {
                (void)klok_player_play_default();
            } else if (klok_player_is_paused()) {
                (void)klok_player_resume();
            }
        } else if (prepared == 0) {
            klok_player_cancel_output_switch();
        }
    }

    s_song_video_transition_pending = false;
#else
    if (klok_mv_render_enter_locked() == BK_OK) {
        if (request->file_path[0] != '\0') {
            (void)klok_player_play_file(request->file_path);
        } else if (!klok_player_is_started()) {
            (void)klok_player_play_default();
        } else if (klok_player_is_paused()) {
            (void)klok_player_resume();
        }
    }
#endif
    os_free(request);
    lv_vendor_disp_unlock();
    rtos_delete_thread(NULL);
}

static void klok_song_list_start_video_async(void *user_data)
{
    beken_thread_t worker = NULL;
    if (rtos_create_thread(&worker,
                           BEKEN_DEFAULT_WORKER_PRIORITY,
                           "klok_video_switch",
                           (beken_thread_function_t)klok_song_list_start_video_worker,
                           KLOK_SONG_VIDEO_SWITCH_STACK_SIZE,
                           (beken_thread_arg_t)user_data) != BK_OK) {
        s_song_video_transition_pending = false;
        os_free(user_data);
    }
}

static bool klok_song_list_has_media_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL)
    {
        return false;
    }

    dot++;
    char ext[8] = {0};
    size_t i = 0;
    while (dot[i] != '\0' && i < sizeof(ext) - 1U)
    {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        ext[i] = c;
        i++;
    }

    return strcmp(ext, "mp4") == 0 ||
           strcmp(ext, "avi") == 0 ||
           strcmp(ext, "mkv") == 0 ||
           strcmp(ext, "mov") == 0;
}

static void klok_song_list_free_path_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DELETE)
    {
        char *path = (char *)lv_event_get_user_data(e);
        if (path != NULL)
        {
            os_free(path);
        }
    }
}

static void klok_song_list_item_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const char *path = (const char *)lv_event_get_user_data(e);
    if (path == NULL || path[0] == '\0')
    {
        return;
    }
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (s_song_video_transition_pending || klok_player_is_switching()) {
        return;
    }
#endif

    klok_song_play_request_t *request =
        (klok_song_play_request_t *)os_malloc(sizeof(*request));
    if (request == NULL) {
        return;
    }
    snprintf(request->file_path, sizeof(request->file_path), "%s", path);
    /*
     * Flexa direct mode covers the complete display and returns to the song
     * list when playback ends. Keep the song-list screen alive underneath it:
     * rebuilding mv_play here makes lv_task_handler render the new screen
     * while the output-switch worker is waiting for the display mutex.
     */
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    s_song_video_transition_pending = true;
#else
    init_page_mv_play(&bk_lv_tool_ui);
    navigate_to_screen(&bk_lv_tool_ui.mv_play,
                       LV_SCR_LOAD_ANIM_NONE,
                       0,
                       0,
                       false,
                       init_page_mv_play);
#endif

    if (lv_async_call(klok_song_list_start_video_async, request) != LV_RESULT_OK) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
        s_song_video_transition_pending = false;
#endif
        os_free(request);
    }
}

static void klok_song_list_show_message(lv_obj_t *panel, const char *text)
{
    lv_obj_t *label = lv_label_create(panel);
    lv_label_set_text(label, text);
    lv_obj_set_x(label, 32);
    lv_obj_set_y(label, 96);
    lv_obj_set_width(label, 560);
    lv_obj_set_height(label, 40);
    lv_obj_set_style_text_color(label, lv_color_hex(0x305887), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void klok_song_list_create_item(lv_obj_t *panel,
                                      const char *name,
                                      const char *path,
                                      uint32_t index)
{
    char *path_copy = os_malloc(strlen(path) + 1U);
    if (path_copy == NULL)
    {
        return;
    }
    strcpy(path_copy, path);

    const int y = 22 + (int)index * 64;
    lv_obj_t *row = lv_btn_create(panel);
    lv_obj_set_x(row, 20);
    lv_obj_set_y(row, y);
    lv_obj_set_width(row, 600);
    lv_obj_set_height(row, 56);
    lv_obj_set_style_bg_color(row, lv_color_hex((index % 2U) ? 0xf8fbff : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, lv_color_hex(0xc9d8ee), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(row,
                    LV_OBJ_FLAG_SCROLL_CHAIN_VER |
                    LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(row, klok_song_list_item_event_cb, LV_EVENT_CLICKED, path_copy);
    lv_obj_add_event_cb(row, klok_song_list_free_path_cb, LV_EVENT_DELETE, path_copy);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, name);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(title, 52);
    lv_obj_set_y(title, 14);
    lv_obj_set_width(title, 420);
    lv_obj_set_height(title, 28);
    lv_obj_set_style_text_color(title, lv_color_hex(0x24395f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *play = lv_label_create(row);
    lv_label_set_text(play, "播放");
    lv_obj_set_x(play, 512);
    lv_obj_set_y(play, 15);
    lv_obj_set_width(play, 56);
    lv_obj_set_height(play, 24);
    lv_obj_set_style_text_color(play, lv_color_hex(0x356eda), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(play, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(play, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void klok_song_list_load_from_sd(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL || bk_ui->song_list_song_panel == NULL)
    {
        return;
    }

    lv_obj_t *panel = bk_ui->song_list_song_panel;
    lv_obj_clean(panel);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    if (video_play_engine_api_prepare_storage() != AVDK_ERR_OK)
    {
        klok_song_list_show_message(panel, "未找到 SD 卡或无法读取 /sd0");
        return;
    }

    DIR *dir = opendir("/sd0");
    if (dir == NULL)
    {
        klok_song_list_show_message(panel, "未找到 SD 卡或无法读取 /sd0");
        return;
    }

    uint32_t count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && count < KLOK_SONG_LIST_MAX_ITEMS)
    {
        if (entry->d_name[0] == '.' || !klok_song_list_has_media_ext(entry->d_name))
        {
            continue;
        }

        char path[KLOK_SONG_PATH_MAX];
        int len = snprintf(path, sizeof(path), "/sd0/%s", entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(path))
        {
            continue;
        }

        klok_song_list_create_item(panel, entry->d_name, path, count);
        count++;
    }

    closedir(dir);

    if (count == 0U)
    {
        klok_song_list_show_message(panel, "SD 卡中未找到视频歌曲文件");
    }
}

void klok_song_list_refresh(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL ||
        bk_ui->song_list == NULL ||
        !lv_obj_is_valid(bk_ui->song_list) ||
        bk_ui->song_list_song_panel == NULL ||
        !lv_obj_is_valid(bk_ui->song_list_song_panel))
    {
        return;
    }

    klok_song_list_load_from_sd(bk_ui);
    lv_obj_update_layout(bk_ui->song_list);
}

static void song_list_preview_lifecycle_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOADED) {
        klok_lvgl_preview_release_locked();
    }
}

static void song_list_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        navigate_to_screen(&bk_lv_tool_ui.klok_main,
                           LV_SCR_LOAD_ANIM_NONE,
                           300,
                           0,
                           false,
                           init_page_klok_main);
    }
}

static void song_list_update_mute_button(void)
{
    if (bk_lv_tool_ui.song_list_mute_btn == NULL ||
        !lv_obj_is_valid(bk_lv_tool_ui.song_list_mute_btn)) {
        return;
    }

    bool muted = klok_player_is_muted();
    lv_obj_set_style_bg_color(
        bk_lv_tool_ui.song_list_mute_btn,
        lv_color_hex(muted ? 0xd64c75 : 0x8f3b55),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(
        bk_lv_tool_ui.song_list_mute_btn,
        muted ? 255 : 221,
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void song_list_update_play_button(void)
{
    if (bk_lv_tool_ui.song_list_play_btn_label == NULL ||
        !lv_obj_is_valid(bk_lv_tool_ui.song_list_play_btn_label)) {
        return;
    }

    bool playing = klok_player_is_started() && !klok_player_is_paused();
    lv_label_set_text(bk_lv_tool_ui.song_list_play_btn_label,
                      playing ? "暂停" : "播放");
    lv_obj_set_style_text_font(
        bk_lv_tool_ui.song_list_play_btn_label,
        playing ? &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17
                : &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18,
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void song_list_open_fullscreen(void)
{
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    if (s_song_video_transition_pending || klok_player_is_switching()) {
        return;
    }
#endif

    klok_song_play_request_t *request =
        (klok_song_play_request_t *)os_malloc(sizeof(*request));
    if (request == NULL) {
        return;
    }
    request->file_path[0] = '\0';
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    s_song_video_transition_pending = true;
#else
    init_page_mv_play(&bk_lv_tool_ui);
    navigate_to_screen(&bk_lv_tool_ui.mv_play,
                       LV_SCR_LOAD_ANIM_MOVE_LEFT,
                       250,
                       0,
                       false,
                       init_page_mv_play);
#endif
    if (lv_async_call(klok_song_list_start_video_async, request) != LV_RESULT_OK) {
#if KLOK_VIDEO_FLEXA_DIRECT_MODE
        s_song_video_transition_pending = false;
#endif
        os_free(request);
    }
}


/**
 * @brief Event callback for song_list_sl_video - handles all events
 * @param e LVGL event object
 */
void song_list_sl_video_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        song_list_open_fullscreen();
    }
}

/**
 * @brief Event callback for song_list_mute_btn - handles all events
 * @param e LVGL event object
 */
void song_list_mute_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_mute_toggle() == 0) {
            song_list_update_mute_button();
        }
    }
}

/**
 * @brief Event callback for song_list_vol_down_btn - handles all events
 * @param e LVGL event object
 */
void song_list_vol_down_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_volume_down() == 0) {
            lv_slider_set_value(bk_ui->song_list_volume_bar,
                                klok_player_get_volume(),
                                LV_ANIM_OFF);
        }
    }
}

/**
 * @brief Event callback for song_list_vol_up_btn - handles all events
 * @param e LVGL event object
 */
void song_list_vol_up_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_volume_up() == 0) {
            lv_slider_set_value(bk_ui->song_list_volume_bar,
                                klok_player_get_volume(),
                                LV_ANIM_OFF);
        }
    }
}

/**
 * @brief Event callback for song_list_vocal_btn - handles all events
 * @param e LVGL event object
 */
void song_list_vocal_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        bool is_accompany = klok_player_is_accompany();
        int ret = is_accompany ? klok_player_vocal()
                               : klok_player_accompany();
        if (ret == 0) {
            lv_label_set_text(bk_ui->song_list_vocal_btn_label,
                              is_accompany ? "伴唱" : "原唱");
            lv_obj_set_style_text_font(
                bk_ui->song_list_vocal_btn_label,
                is_accompany
                    ? &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17
                    : &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18,
                LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/**
 * @brief Event callback for song_list_replay_btn - handles all events
 * @param e LVGL event object
 */
void song_list_replay_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_replay() == 0) {
            song_list_update_play_button();
        }
    }
}

/**
 * @brief Event callback for song_list_next_btn - handles all events
 * @param e LVGL event object
 */
void song_list_next_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_next() == 0) {
            song_list_update_play_button();
        }
    }
}

/**
 * @brief Event callback for song_list_play_btn - handles all events
 * @param e LVGL event object
 */
void song_list_play_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        if (klok_player_pause_toggle() == 0) {
            song_list_update_play_button();
        }
    }
}

/**
 * @brief Event callback for song_list_full_btn - handles all events
 * @param e LVGL event object
 */
void song_list_full_btn_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        song_list_open_fullscreen();
    }
}

/**
 * @brief Event callback for song_list_volume_bar - handles all events
 * @param e LVGL event object
 */
void song_list_volume_bar_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *bk_ui = &bk_lv_tool_ui;   
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        (void)klok_player_set_volume(
            (uint8_t)lv_slider_get_value(bk_ui->song_list_volume_bar));
        song_list_update_mute_button();
        lv_obj_set_style_bg_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffd6f1), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(bk_ui->song_list_volume_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
}


/*
 * @brief: init page song_list
 */
void init_page_song_list(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->song_list != NULL && lv_obj_is_valid(bk_ui->song_list)) {
        destroy_page_song_list(bk_ui);
    }
    

    bk_ui->song_list = lv_obj_create(NULL);
    lv_obj_add_event_cb(bk_ui->song_list,
                        song_list_preview_lifecycle_event_cb,
                        LV_EVENT_SCREEN_UNLOADED,
                        NULL);
    lv_obj_set_scrollbar_mode(bk_ui->song_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->song_list, 1280, 720);
    lv_obj_set_style_bg_color(bk_ui->song_list, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_sl_bg = lv_obj_create(bk_ui->song_list);
    lv_obj_set_x(bk_ui->song_list_sl_bg, 0);
    lv_obj_set_y(bk_ui->song_list_sl_bg, 0);
    lv_obj_set_width(bk_ui->song_list_sl_bg, 1280);
    lv_obj_set_height(bk_ui->song_list_sl_bg, 720);
    lv_obj_remove_flag(bk_ui->song_list_sl_bg, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_bg, lv_color_hex(0x140936), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_bg, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(bk_ui->song_list_sl_bg, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(bk_ui->song_list_sl_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor(bk_ui->song_list_sl_bg, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_bg, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_bg, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_bg, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_sl_bg, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_sl_bg, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_sl_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_bg, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_bg, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_bg, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_bg, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_bg, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_bg, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_bg, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_sl_back = lv_label_create(bk_ui->song_list_sl_bg);
    lv_label_set_text(bk_ui->song_list_sl_back, "返回点歌");
    lv_label_set_long_mode(bk_ui->song_list_sl_back, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_sl_back, 28);
    lv_obj_set_y(bk_ui->song_list_sl_back, 20);
    lv_obj_set_width(bk_ui->song_list_sl_back, 240);
    lv_obj_set_height(bk_ui->song_list_sl_back, 68);
    lv_obj_remove_flag(bk_ui->song_list_sl_back, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(bk_ui->song_list_sl_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bk_ui->song_list_sl_back,
                        song_list_back_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_back, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_back, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_back, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_sl_back, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_sl_back, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_sl_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_sl_back, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_sl_back, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_sl_back, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_sl_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_sl_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_sl_video = lv_obj_create(bk_ui->song_list_sl_bg);
    lv_obj_set_x(bk_ui->song_list_sl_video, 22);
    lv_obj_set_y(bk_ui->song_list_sl_video, 91);
    lv_obj_set_width(bk_ui->song_list_sl_video, 576);
    lv_obj_set_height(bk_ui->song_list_sl_video, 304);
    lv_obj_remove_flag(bk_ui->song_list_sl_video, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_video, lv_color_hex(0x02020a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_video, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_video, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_video, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_video, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_video, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_video, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_video, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_sl_video, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_sl_video, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_sl_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_video, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_video, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_video, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_video, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_video, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_video, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_video, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_video, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_video, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_sl_video, song_list_sl_video_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_search_bar = lv_obj_create(bk_ui->song_list_sl_bg);
    lv_obj_set_x(bk_ui->song_list_search_bar, 22);
    lv_obj_set_y(bk_ui->song_list_search_bar, 405);
    lv_obj_set_width(bk_ui->song_list_search_bar, 565);
    lv_obj_set_height(bk_ui->song_list_search_bar, 48);
    lv_obj_remove_flag(bk_ui->song_list_search_bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_search_bar, lv_color_hex(0x381c5a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_search_bar, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_search_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_search_bar, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_search_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_search_bar, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_search_bar, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_search_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_search_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_search_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_search_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_search_bar, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_search_bar, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_search_bar, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_search_bar, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_search_bar, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_search_bar, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_search_bar, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_search_bar, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_search_bar, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_sbar_icon = lv_image_create(bk_ui->song_list_search_bar);
    lv_image_set_src(bk_ui->song_list_sbar_icon, &icon_search_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_sbar_icon, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_sbar_icon, 0);
    lv_obj_set_x(bk_ui->song_list_sbar_icon, 12);
    lv_obj_set_y(bk_ui->song_list_sbar_icon, 6);
    lv_obj_set_width(bk_ui->song_list_sbar_icon, 36);
    lv_obj_set_height(bk_ui->song_list_sbar_icon, 36);
    lv_obj_remove_flag(bk_ui->song_list_sbar_icon, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_sbar_icon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sbar_icon, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sbar_icon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sbar_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sbar_icon, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sbar_icon, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_sbar_icon, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_sbar_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_sbar_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_sbar_icon, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_sbar_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_sbar_txt = lv_label_create(bk_ui->song_list_search_bar);
    lv_label_set_text(bk_ui->song_list_sbar_txt, "请输入歌名 / 歌手 / 拼音");
    lv_label_set_long_mode(bk_ui->song_list_sbar_txt, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_sbar_txt, 62);
    lv_obj_set_y(bk_ui->song_list_sbar_txt, 12);
    lv_obj_set_width(bk_ui->song_list_sbar_txt, 320);
    lv_obj_set_height(bk_ui->song_list_sbar_txt, 26);
    lv_obj_remove_flag(bk_ui->song_list_sbar_txt, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_sbar_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sbar_txt, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sbar_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sbar_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sbar_txt, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sbar_txt, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_sbar_txt, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_sbar_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_sbar_txt, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_17, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_sbar_txt, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_sbar_txt, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_sbar_txt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_sbar_txt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_panel = lv_obj_create(bk_ui->song_list_sl_bg);
    lv_obj_set_x(bk_ui->song_list_key_panel, 22);
    lv_obj_set_y(bk_ui->song_list_key_panel, 465);
    lv_obj_set_width(bk_ui->song_list_key_panel, 565);
    lv_obj_set_height(bk_ui->song_list_key_panel, 175);
    lv_obj_remove_flag(bk_ui->song_list_key_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_panel, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_panel, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_panel, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_panel, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_panel, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_key_a = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_a_label = lv_label_create(bk_ui->song_list_key_a);
    lv_label_set_text_static(bk_ui->song_list_key_a_label, "A");
    lv_label_set_long_mode(bk_ui->song_list_key_a_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_a_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_a, 2);
    lv_obj_set_y(bk_ui->song_list_key_a, 0);
    lv_obj_set_width(bk_ui->song_list_key_a, 51);
    lv_obj_set_height(bk_ui->song_list_key_a, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_a, lv_color_hex(0xb72b4d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_a, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_a, lv_color_hex(0xc04a82), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_a, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_a, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_a, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_a, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_a, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_a, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_a, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_a, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_a, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_a, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_a, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_a, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_a, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_a, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_a, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_a, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_a, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_b = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_b_label = lv_label_create(bk_ui->song_list_key_b);
    lv_label_set_text_static(bk_ui->song_list_key_b_label, "B");
    lv_label_set_long_mode(bk_ui->song_list_key_b_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_b_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_b, 58);
    lv_obj_set_y(bk_ui->song_list_key_b, 0);
    lv_obj_set_width(bk_ui->song_list_key_b, 51);
    lv_obj_set_height(bk_ui->song_list_key_b, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_b, lv_color_hex(0xb73055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_b, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_b, lv_color_hex(0xbd4a8a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_b, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_b, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_b, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_b, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_b, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_b, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_b, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_b, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_b, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_b, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_b, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_b, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_b, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_b, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_b, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_b, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_b, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_b, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_b, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_b, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_c = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_c_label = lv_label_create(bk_ui->song_list_key_c);
    lv_label_set_text_static(bk_ui->song_list_key_c_label, "C");
    lv_label_set_long_mode(bk_ui->song_list_key_c_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_c_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_c, 114);
    lv_obj_set_y(bk_ui->song_list_key_c, 0);
    lv_obj_set_width(bk_ui->song_list_key_c, 51);
    lv_obj_set_height(bk_ui->song_list_key_c, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_c, lv_color_hex(0xb73560), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_c, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_c, lv_color_hex(0xb84b91), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_c, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_c, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_c, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_c, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_c, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_c, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_c, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_c, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_c, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_c, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_c, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_c, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_c, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_c, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_c, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_c, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_c, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_d = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_d_label = lv_label_create(bk_ui->song_list_key_d);
    lv_label_set_text_static(bk_ui->song_list_key_d_label, "D");
    lv_label_set_long_mode(bk_ui->song_list_key_d_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_d_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_d, 170);
    lv_obj_set_y(bk_ui->song_list_key_d, 0);
    lv_obj_set_width(bk_ui->song_list_key_d, 51);
    lv_obj_set_height(bk_ui->song_list_key_d, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_d, lv_color_hex(0xaa3a68), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_d, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_d, lv_color_hex(0xad4b98), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_d, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_d, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_d, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_d, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_d, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_d, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_d, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_d, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_d, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_d, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_d, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_d, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_d, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_d, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_d, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_d, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_d, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_d, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_d, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_d, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_d, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_e = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_e_label = lv_label_create(bk_ui->song_list_key_e);
    lv_label_set_text_static(bk_ui->song_list_key_e_label, "E");
    lv_label_set_long_mode(bk_ui->song_list_key_e_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_e_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_e, 226);
    lv_obj_set_y(bk_ui->song_list_key_e, 0);
    lv_obj_set_width(bk_ui->song_list_key_e, 51);
    lv_obj_set_height(bk_ui->song_list_key_e, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_e, lv_color_hex(0x9f3e72), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_e, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_e, lv_color_hex(0xa04ca0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_e, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_e, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_e, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_e, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_e, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_e, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_e, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_e, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_e, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_e, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_e, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_e, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_e, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_e, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_e, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_e, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_e, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_e, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_e, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_e, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_f = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_f_label = lv_label_create(bk_ui->song_list_key_f);
    lv_label_set_text_static(bk_ui->song_list_key_f_label, "F");
    lv_label_set_long_mode(bk_ui->song_list_key_f_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_f_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_f, 282);
    lv_obj_set_y(bk_ui->song_list_key_f, 0);
    lv_obj_set_width(bk_ui->song_list_key_f, 51);
    lv_obj_set_height(bk_ui->song_list_key_f, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_f, lv_color_hex(0x914279), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_f, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_f, lv_color_hex(0x944ba4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_f, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_f, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_f, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_f, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_f, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_f, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_f, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_f, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_f, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_f, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_f, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_f, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_f, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_f, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_f, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_f, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_f, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_f, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_f, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_f, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_f, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_g = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_g_label = lv_label_create(bk_ui->song_list_key_g);
    lv_label_set_text_static(bk_ui->song_list_key_g_label, "G");
    lv_label_set_long_mode(bk_ui->song_list_key_g_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_g_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_g, 338);
    lv_obj_set_y(bk_ui->song_list_key_g, 0);
    lv_obj_set_width(bk_ui->song_list_key_g, 51);
    lv_obj_set_height(bk_ui->song_list_key_g, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_g, lv_color_hex(0x83467f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_g, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_g, lv_color_hex(0x854ca8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_g, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_g, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_g, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_g, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_g, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_g, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_g, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_g, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_g, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_g, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_g, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_g, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_g, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_g, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_g, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_g, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_g, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_g, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_g, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_g, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_g, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_h = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_h_label = lv_label_create(bk_ui->song_list_key_h);
    lv_label_set_text_static(bk_ui->song_list_key_h_label, "H");
    lv_label_set_long_mode(bk_ui->song_list_key_h_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_h_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_h, 394);
    lv_obj_set_y(bk_ui->song_list_key_h, 0);
    lv_obj_set_width(bk_ui->song_list_key_h, 51);
    lv_obj_set_height(bk_ui->song_list_key_h, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_h, lv_color_hex(0x794983), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_h, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_h, lv_color_hex(0x774caa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_h, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_h, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_h, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_h, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_h, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_h, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_h, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_h, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_h, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_h, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_h, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_h, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_h, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_h, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_h, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_h, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_h, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_i = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_i_label = lv_label_create(bk_ui->song_list_key_i);
    lv_label_set_text_static(bk_ui->song_list_key_i_label, "I");
    lv_label_set_long_mode(bk_ui->song_list_key_i_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_i_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_i, 450);
    lv_obj_set_y(bk_ui->song_list_key_i, 0);
    lv_obj_set_width(bk_ui->song_list_key_i, 51);
    lv_obj_set_height(bk_ui->song_list_key_i, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_i, lv_color_hex(0x704c87), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_i, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_i, lv_color_hex(0x6c4bac), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_i, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_i, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_i, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_i, 204, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_i, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_i, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_i, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_i, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_i, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_i, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_i, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_i, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_i, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_i, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_i, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_i, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_i, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_i, 102, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_i, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_i, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_i, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_j = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_j_label = lv_label_create(bk_ui->song_list_key_j);
    lv_label_set_text_static(bk_ui->song_list_key_j_label, "J");
    lv_label_set_long_mode(bk_ui->song_list_key_j_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_j_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_j, 506);
    lv_obj_set_y(bk_ui->song_list_key_j, 0);
    lv_obj_set_width(bk_ui->song_list_key_j, 51);
    lv_obj_set_height(bk_ui->song_list_key_j, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_j, lv_color_hex(0x684f8b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_j, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_j, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_j, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_j, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_j, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_j, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_j, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_j, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_j, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_j, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_j, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_j, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_j, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_j, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_j, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_j, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_j, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_j, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_j, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_k = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_k_label = lv_label_create(bk_ui->song_list_key_k);
    lv_label_set_text_static(bk_ui->song_list_key_k_label, "K");
    lv_label_set_long_mode(bk_ui->song_list_key_k_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_k_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_k, 2);
    lv_obj_set_y(bk_ui->song_list_key_k, 45);
    lv_obj_set_width(bk_ui->song_list_key_k, 51);
    lv_obj_set_height(bk_ui->song_list_key_k, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_k, lv_color_hex(0xa63a4d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_k, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_k, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_k, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_k, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_k, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_k, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_k, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_k, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_k, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_k, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_k, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_k, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_k, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_k, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_k, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_k, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_k, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_k, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_k, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_l = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_l_label = lv_label_create(bk_ui->song_list_key_l);
    lv_label_set_text_static(bk_ui->song_list_key_l_label, "L");
    lv_label_set_long_mode(bk_ui->song_list_key_l_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_l_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_l, 58);
    lv_obj_set_y(bk_ui->song_list_key_l, 45);
    lv_obj_set_width(bk_ui->song_list_key_l, 51);
    lv_obj_set_height(bk_ui->song_list_key_l, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_l, lv_color_hex(0xaa3a56), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_l, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_l, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_l, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_l, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_l, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_l, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_l, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_l, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_l, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_l, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_l, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_l, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_l, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_l, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_l, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_l, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_l, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_l, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_m = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_m_label = lv_label_create(bk_ui->song_list_key_m);
    lv_label_set_text_static(bk_ui->song_list_key_m_label, "M");
    lv_label_set_long_mode(bk_ui->song_list_key_m_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_m_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_m, 114);
    lv_obj_set_y(bk_ui->song_list_key_m, 45);
    lv_obj_set_width(bk_ui->song_list_key_m, 51);
    lv_obj_set_height(bk_ui->song_list_key_m, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_m, lv_color_hex(0xaa3b60), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_m, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_m, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_m, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_m, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_m, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_m, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_m, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_m, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_m, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_m, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_m, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_m, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_m, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_m, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_m, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_m, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_m, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_m, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_m, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_n = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_n_label = lv_label_create(bk_ui->song_list_key_n);
    lv_label_set_text_static(bk_ui->song_list_key_n_label, "N");
    lv_label_set_long_mode(bk_ui->song_list_key_n_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_n_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_n, 170);
    lv_obj_set_y(bk_ui->song_list_key_n, 45);
    lv_obj_set_width(bk_ui->song_list_key_n, 51);
    lv_obj_set_height(bk_ui->song_list_key_n, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_n, lv_color_hex(0xa03e68), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_n, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_n, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_n, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_n, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_n, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_n, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_n, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_n, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_n, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_n, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_n, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_n, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_n, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_n, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_n, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_n, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_n, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_n, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_n, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_o = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_o_label = lv_label_create(bk_ui->song_list_key_o);
    lv_label_set_text_static(bk_ui->song_list_key_o_label, "O");
    lv_label_set_long_mode(bk_ui->song_list_key_o_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_o_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_o, 226);
    lv_obj_set_y(bk_ui->song_list_key_o, 45);
    lv_obj_set_width(bk_ui->song_list_key_o, 51);
    lv_obj_set_height(bk_ui->song_list_key_o, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_o, lv_color_hex(0x964170), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_o, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_o, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_o, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_o, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_o, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_o, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_o, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_o, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_o, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_o, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_o, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_o, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_o, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_o, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_o, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_p = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_p_label = lv_label_create(bk_ui->song_list_key_p);
    lv_label_set_text_static(bk_ui->song_list_key_p_label, "P");
    lv_label_set_long_mode(bk_ui->song_list_key_p_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_p_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_p, 282);
    lv_obj_set_y(bk_ui->song_list_key_p, 45);
    lv_obj_set_width(bk_ui->song_list_key_p, 51);
    lv_obj_set_height(bk_ui->song_list_key_p, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_p, lv_color_hex(0x8b4478), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_p, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_p, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_p, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_p, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_p, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_p, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_p, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_p, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_p, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_p, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_p, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_p, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_p, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_p, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_p, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_q = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_q_label = lv_label_create(bk_ui->song_list_key_q);
    lv_label_set_text_static(bk_ui->song_list_key_q_label, "Q");
    lv_label_set_long_mode(bk_ui->song_list_key_q_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_q_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_q, 338);
    lv_obj_set_y(bk_ui->song_list_key_q, 45);
    lv_obj_set_width(bk_ui->song_list_key_q, 51);
    lv_obj_set_height(bk_ui->song_list_key_q, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_q, lv_color_hex(0x82467f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_q, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_q, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_q, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_q, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_q, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_q, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_q, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_q, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_q, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_q, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_q, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_q, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_q, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_q, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_q, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_q, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_q, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_q, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_q, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_r = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_r_label = lv_label_create(bk_ui->song_list_key_r);
    lv_label_set_text_static(bk_ui->song_list_key_r_label, "R");
    lv_label_set_long_mode(bk_ui->song_list_key_r_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_r_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_r, 394);
    lv_obj_set_y(bk_ui->song_list_key_r, 45);
    lv_obj_set_width(bk_ui->song_list_key_r, 51);
    lv_obj_set_height(bk_ui->song_list_key_r, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_r, lv_color_hex(0x794983), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_r, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_r, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_r, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_r, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_r, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_r, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_r, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_r, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_r, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_r, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_r, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_r, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_r, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_r, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_r, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_r, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_r, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_r, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_r, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_s = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_s_label = lv_label_create(bk_ui->song_list_key_s);
    lv_label_set_text_static(bk_ui->song_list_key_s_label, "S");
    lv_label_set_long_mode(bk_ui->song_list_key_s_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_s_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_s, 450);
    lv_obj_set_y(bk_ui->song_list_key_s, 45);
    lv_obj_set_width(bk_ui->song_list_key_s, 51);
    lv_obj_set_height(bk_ui->song_list_key_s, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_s, lv_color_hex(0x704c87), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_s, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_s, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_s, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_s, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_s, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_s, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_s, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_s, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_s, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_s, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_s, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_s, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_t = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_t_label = lv_label_create(bk_ui->song_list_key_t);
    lv_label_set_text_static(bk_ui->song_list_key_t_label, "T");
    lv_label_set_long_mode(bk_ui->song_list_key_t_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_t_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_t, 506);
    lv_obj_set_y(bk_ui->song_list_key_t, 45);
    lv_obj_set_width(bk_ui->song_list_key_t, 51);
    lv_obj_set_height(bk_ui->song_list_key_t, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_t, lv_color_hex(0x684f8b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_t, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_t, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_t, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_t, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_t, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_t, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_t, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_t, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_t, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_t, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_t, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_t, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_t, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_t, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_u = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_u_label = lv_label_create(bk_ui->song_list_key_u);
    lv_label_set_text_static(bk_ui->song_list_key_u_label, "U");
    lv_label_set_long_mode(bk_ui->song_list_key_u_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_u_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_u, 2);
    lv_obj_set_y(bk_ui->song_list_key_u, 90);
    lv_obj_set_width(bk_ui->song_list_key_u, 51);
    lv_obj_set_height(bk_ui->song_list_key_u, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_u, lv_color_hex(0x923946), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_u, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_u, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_u, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_u, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_u, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_u, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_u, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_u, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_u, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_u, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_u, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_u, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_u, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_u, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_u, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_u, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_u, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_u, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_u, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_v = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_v_label = lv_label_create(bk_ui->song_list_key_v);
    lv_label_set_text_static(bk_ui->song_list_key_v_label, "V");
    lv_label_set_long_mode(bk_ui->song_list_key_v_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_v_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_v, 58);
    lv_obj_set_y(bk_ui->song_list_key_v, 90);
    lv_obj_set_width(bk_ui->song_list_key_v, 51);
    lv_obj_set_height(bk_ui->song_list_key_v, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_v, lv_color_hex(0x973a4f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_v, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_v, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_v, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_v, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_v, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_v, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_v, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_v, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_v, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_v, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_v, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_v, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_v, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_v, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_w = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_w_label = lv_label_create(bk_ui->song_list_key_w);
    lv_label_set_text_static(bk_ui->song_list_key_w_label, "W");
    lv_label_set_long_mode(bk_ui->song_list_key_w_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_w_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_w, 114);
    lv_obj_set_y(bk_ui->song_list_key_w, 90);
    lv_obj_set_width(bk_ui->song_list_key_w, 51);
    lv_obj_set_height(bk_ui->song_list_key_w, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_w, lv_color_hex(0x9a3b58), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_w, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_w, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_w, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_w, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_w, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_w, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_w, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_w, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_w, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_w, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_w, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_w, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_w, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_w, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_w, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_w, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_w, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_w, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_w, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_x = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_x_label = lv_label_create(bk_ui->song_list_key_x);
    lv_label_set_text_static(bk_ui->song_list_key_x_label, "X");
    lv_label_set_long_mode(bk_ui->song_list_key_x_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_x_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_x, 170);
    lv_obj_set_y(bk_ui->song_list_key_x, 90);
    lv_obj_set_width(bk_ui->song_list_key_x, 51);
    lv_obj_set_height(bk_ui->song_list_key_x, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_x, lv_color_hex(0x973d62), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_x, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_x, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_x, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_x, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_x, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_x, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_x, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_x, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_x, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_x, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_x, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_x, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_x, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_x, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_x, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_x, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_x, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_x, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_x, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_y = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_y_label = lv_label_create(bk_ui->song_list_key_y);
    lv_label_set_text_static(bk_ui->song_list_key_y_label, "Y");
    lv_label_set_long_mode(bk_ui->song_list_key_y_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_y_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_y, 226);
    lv_obj_set_y(bk_ui->song_list_key_y, 90);
    lv_obj_set_width(bk_ui->song_list_key_y, 51);
    lv_obj_set_height(bk_ui->song_list_key_y, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_y, lv_color_hex(0x8e4070), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_y, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_y, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_y, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_y, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_y, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_y, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_y, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_y, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_y, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_y, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_y, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_y, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_y, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_y, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_y, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_z = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_z_label = lv_label_create(bk_ui->song_list_key_z);
    lv_label_set_text_static(bk_ui->song_list_key_z_label, "Z");
    lv_label_set_long_mode(bk_ui->song_list_key_z_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_z_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_z, 282);
    lv_obj_set_y(bk_ui->song_list_key_z, 90);
    lv_obj_set_width(bk_ui->song_list_key_z, 51);
    lv_obj_set_height(bk_ui->song_list_key_z, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_z, lv_color_hex(0x854477), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_z, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_z, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_z, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_z, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_z, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_z, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_z, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_z, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_z, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_z, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_z, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_z, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_z, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_z, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_z, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_z, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_z, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_z, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_z, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_1 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_1_label = lv_label_create(bk_ui->song_list_key_1);
    lv_label_set_text_static(bk_ui->song_list_key_1_label, "1");
    lv_label_set_long_mode(bk_ui->song_list_key_1_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_1_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_1, 338);
    lv_obj_set_y(bk_ui->song_list_key_1, 90);
    lv_obj_set_width(bk_ui->song_list_key_1, 51);
    lv_obj_set_height(bk_ui->song_list_key_1, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_1, lv_color_hex(0x7c467e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_1, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_1, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_1, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_1, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_1, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_2 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_2_label = lv_label_create(bk_ui->song_list_key_2);
    lv_label_set_text_static(bk_ui->song_list_key_2_label, "2");
    lv_label_set_long_mode(bk_ui->song_list_key_2_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_2_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_2, 394);
    lv_obj_set_y(bk_ui->song_list_key_2, 90);
    lv_obj_set_width(bk_ui->song_list_key_2, 51);
    lv_obj_set_height(bk_ui->song_list_key_2, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_2, lv_color_hex(0x734984), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_2, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_2, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_2, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_2, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_2, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_3 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_3_label = lv_label_create(bk_ui->song_list_key_3);
    lv_label_set_text_static(bk_ui->song_list_key_3_label, "3");
    lv_label_set_long_mode(bk_ui->song_list_key_3_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_3_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_3, 450);
    lv_obj_set_y(bk_ui->song_list_key_3, 90);
    lv_obj_set_width(bk_ui->song_list_key_3, 51);
    lv_obj_set_height(bk_ui->song_list_key_3, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_3, lv_color_hex(0x6b4c88), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_3, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_3, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_3, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_3, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_3, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_4 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_4_label = lv_label_create(bk_ui->song_list_key_4);
    lv_label_set_text_static(bk_ui->song_list_key_4_label, "4");
    lv_label_set_long_mode(bk_ui->song_list_key_4_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_4_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_4, 506);
    lv_obj_set_y(bk_ui->song_list_key_4, 90);
    lv_obj_set_width(bk_ui->song_list_key_4, 51);
    lv_obj_set_height(bk_ui->song_list_key_4, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_4, lv_color_hex(0x644f8b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_4, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_4, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_4, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_4, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_4, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_4, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_5 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_5_label = lv_label_create(bk_ui->song_list_key_5);
    lv_label_set_text_static(bk_ui->song_list_key_5_label, "5");
    lv_label_set_long_mode(bk_ui->song_list_key_5_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_5_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_5, 2);
    lv_obj_set_y(bk_ui->song_list_key_5, 135);
    lv_obj_set_width(bk_ui->song_list_key_5, 51);
    lv_obj_set_height(bk_ui->song_list_key_5, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_5, lv_color_hex(0x8c353f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_5, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_5, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_5, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_5, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_5, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_5, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_6 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_6_label = lv_label_create(bk_ui->song_list_key_6);
    lv_label_set_text_static(bk_ui->song_list_key_6_label, "6");
    lv_label_set_long_mode(bk_ui->song_list_key_6_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_6_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_6, 58);
    lv_obj_set_y(bk_ui->song_list_key_6, 135);
    lv_obj_set_width(bk_ui->song_list_key_6, 51);
    lv_obj_set_height(bk_ui->song_list_key_6, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_6, lv_color_hex(0x923847), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_6, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_6, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_6, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_6, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_6, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_6, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_7 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_7_label = lv_label_create(bk_ui->song_list_key_7);
    lv_label_set_text_static(bk_ui->song_list_key_7_label, "7");
    lv_label_set_long_mode(bk_ui->song_list_key_7_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_7_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_7, 114);
    lv_obj_set_y(bk_ui->song_list_key_7, 135);
    lv_obj_set_width(bk_ui->song_list_key_7, 51);
    lv_obj_set_height(bk_ui->song_list_key_7, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_7, lv_color_hex(0x963b50), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_7, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_7, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_7, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_7, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_7, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_7, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_7, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_7, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_7, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_8 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_8_label = lv_label_create(bk_ui->song_list_key_8);
    lv_label_set_text_static(bk_ui->song_list_key_8_label, "8");
    lv_label_set_long_mode(bk_ui->song_list_key_8_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_8_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_8, 170);
    lv_obj_set_y(bk_ui->song_list_key_8, 135);
    lv_obj_set_width(bk_ui->song_list_key_8, 51);
    lv_obj_set_height(bk_ui->song_list_key_8, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_8, lv_color_hex(0x933e5c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_8, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_8, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_8, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_8, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_8, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_8, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_8, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_8, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_9 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_9_label = lv_label_create(bk_ui->song_list_key_9);
    lv_label_set_text_static(bk_ui->song_list_key_9_label, "9");
    lv_label_set_long_mode(bk_ui->song_list_key_9_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_9_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_9, 226);
    lv_obj_set_y(bk_ui->song_list_key_9, 135);
    lv_obj_set_width(bk_ui->song_list_key_9, 51);
    lv_obj_set_height(bk_ui->song_list_key_9, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_9, lv_color_hex(0x8a426a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_9, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_9, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_9, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_9, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_9, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_9, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_9, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_9, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_9, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_9, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_0 = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_0_label = lv_label_create(bk_ui->song_list_key_0);
    lv_label_set_text_static(bk_ui->song_list_key_0_label, "0");
    lv_label_set_long_mode(bk_ui->song_list_key_0_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_0_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_0, 282);
    lv_obj_set_y(bk_ui->song_list_key_0, 135);
    lv_obj_set_width(bk_ui->song_list_key_0, 51);
    lv_obj_set_height(bk_ui->song_list_key_0, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_0, lv_color_hex(0x814577), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_0, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_0, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_0, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_0, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_0, 63, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_0, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_0, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_0, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_0, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_0, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_0, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_0, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_0, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_0, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_0, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_clear = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_clear_label = lv_label_create(bk_ui->song_list_key_clear);
    lv_label_set_text_static(bk_ui->song_list_key_clear_label, "清空");
    lv_label_set_long_mode(bk_ui->song_list_key_clear_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_clear_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_clear, 338);
    lv_obj_set_y(bk_ui->song_list_key_clear, 135);
    lv_obj_set_width(bk_ui->song_list_key_clear, 108);
    lv_obj_set_height(bk_ui->song_list_key_clear, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_clear, lv_color_hex(0xff352e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_clear, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_clear, lv_color_hex(0xff704d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_clear, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_clear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_clear, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_clear, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_clear, lv_color_hex(0xffb0a0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_clear, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_clear, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_clear, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_clear, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_clear, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_clear, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_clear, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_clear, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_clear, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_clear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_clear, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_clear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_clear, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_clear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_clear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_clear, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_key_del = lv_btn_create(bk_ui->song_list_key_panel);
    bk_ui->song_list_key_del_label = lv_label_create(bk_ui->song_list_key_del);
    lv_label_set_text_static(bk_ui->song_list_key_del_label, "删除");
    lv_label_set_long_mode(bk_ui->song_list_key_del_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_key_del_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_key_del, 452);
    lv_obj_set_y(bk_ui->song_list_key_del, 135);
    lv_obj_set_width(bk_ui->song_list_key_del, 108);
    lv_obj_set_height(bk_ui->song_list_key_del, 39);
    lv_obj_set_style_bg_color(bk_ui->song_list_key_del, lv_color_hex(0xe7f0dd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_key_del, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->song_list_key_del, lv_color_hex(0xf2f7ea), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_key_del, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(bk_ui->song_list_key_del, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(bk_ui->song_list_key_del, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_opa(bk_ui->song_list_key_del, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_key_del, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_key_del, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_key_del, 153, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_key_del, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_key_del, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_key_del, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_key_del, lv_color_hex(0x697071), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_key_del, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_key_del, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_key_del, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_key_del, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_key_del, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_key_del, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_key_del, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_key_del, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_key_del, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_key_del, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_song_panel = lv_obj_create(bk_ui->song_list_sl_bg);
    lv_obj_set_x(bk_ui->song_list_song_panel, 615);
    lv_obj_set_y(bk_ui->song_list_song_panel, 92);
    lv_obj_set_width(bk_ui->song_list_song_panel, 640);
    lv_obj_set_height(bk_ui->song_list_song_panel, 548);
    lv_obj_remove_flag(bk_ui->song_list_song_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_song_panel, lv_color_hex(0xedf4ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_song_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_song_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_song_panel, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_song_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_song_panel, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_song_panel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_song_panel, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_song_panel, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_song_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_song_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_song_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_song_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_song_panel, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_song_panel, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_song_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_song_panel, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_song_panel, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_song_panel, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_song_panel, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_row1 = lv_label_create(bk_ui->song_list_song_panel);
    lv_label_set_text(bk_ui->song_list_row1, "后来 - 刘若英");
    lv_label_set_long_mode(bk_ui->song_list_row1, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_row1, 32);
    lv_obj_set_y(bk_ui->song_list_row1, 86);
    lv_obj_set_width(bk_ui->song_list_row1, 330);
    lv_obj_set_height(bk_ui->song_list_row1, 36);
    lv_obj_remove_flag(bk_ui->song_list_row1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_row1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_row1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_row1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_row1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_row1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_row1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_row1, lv_color_hex(0x65708a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_row1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_row1, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_row1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_row1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_row1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_row1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_row2 = lv_label_create(bk_ui->song_list_song_panel);
    lv_label_set_text(bk_ui->song_list_row2, "朋友 - 周华健");
    lv_label_set_long_mode(bk_ui->song_list_row2, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_row2, 32);
    lv_obj_set_y(bk_ui->song_list_row2, 151);
    lv_obj_set_width(bk_ui->song_list_row2, 330);
    lv_obj_set_height(bk_ui->song_list_row2, 36);
    lv_obj_remove_flag(bk_ui->song_list_row2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_row2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_row2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_row2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_row2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_row2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_row2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_row2, lv_color_hex(0x65708a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_row2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_row2, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_row2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_row2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_row2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_row2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_row3 = lv_label_create(bk_ui->song_list_song_panel);
    lv_label_set_text(bk_ui->song_list_row3, "小幸运 - 田馥甄");
    lv_label_set_long_mode(bk_ui->song_list_row3, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_row3, 32);
    lv_obj_set_y(bk_ui->song_list_row3, 216);
    lv_obj_set_width(bk_ui->song_list_row3, 330);
    lv_obj_set_height(bk_ui->song_list_row3, 36);
    lv_obj_remove_flag(bk_ui->song_list_row3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_row3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_row3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_row3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_row3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_row3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_row3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_row3, lv_color_hex(0x65708a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_row3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_row3, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_row3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_row3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_row3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_row3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_row4 = lv_label_create(bk_ui->song_list_song_panel);
    lv_label_set_text(bk_ui->song_list_row4, "告白气球 - 周杰伦");
    lv_label_set_long_mode(bk_ui->song_list_row4, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_row4, 32);
    lv_obj_set_y(bk_ui->song_list_row4, 281);
    lv_obj_set_width(bk_ui->song_list_row4, 330);
    lv_obj_set_height(bk_ui->song_list_row4, 36);
    lv_obj_remove_flag(bk_ui->song_list_row4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_row4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_row4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_row4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_row4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_row4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_row4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_row4, lv_color_hex(0x65708a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_row4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_row4, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_row4, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_row4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_row4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_row4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_row5 = lv_label_create(bk_ui->song_list_song_panel);
    lv_label_set_text(bk_ui->song_list_row5, "月亮代表我的心");
    lv_label_set_long_mode(bk_ui->song_list_row5, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_row5, 32);
    lv_obj_set_y(bk_ui->song_list_row5, 346);
    lv_obj_set_width(bk_ui->song_list_row5, 330);
    lv_obj_set_height(bk_ui->song_list_row5, 36);
    lv_obj_remove_flag(bk_ui->song_list_row5, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_row5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_row5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_row5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_row5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_row5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_row5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_row5, lv_color_hex(0x65708a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_row5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_row5, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_row5, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_row5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_row5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_row5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_row6 = lv_label_create(bk_ui->song_list_song_panel);
    lv_label_set_text(bk_ui->song_list_row6, "十年 - 陈奕迅");
    lv_label_set_long_mode(bk_ui->song_list_row6, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_x(bk_ui->song_list_row6, 32);
    lv_obj_set_y(bk_ui->song_list_row6, 411);
    lv_obj_set_width(bk_ui->song_list_row6, 330);
    lv_obj_set_height(bk_ui->song_list_row6, 36);
    lv_obj_remove_flag(bk_ui->song_list_row6, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_row6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_row6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_row6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_row6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_row6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_row6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_row6, lv_color_hex(0x65708a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_row6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_row6, &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_row6, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_row6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_row6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_row6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mic1 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_mic1, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mic1, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mic1, 0);
    lv_obj_set_x(bk_ui->song_list_mic1, 430);
    lv_obj_set_y(bk_ui->song_list_mic1, 82);
    lv_obj_set_width(bk_ui->song_list_mic1, 36);
    lv_obj_set_height(bk_ui->song_list_mic1, 36);
    lv_obj_remove_flag(bk_ui->song_list_mic1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_mic1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mic1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mic1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mic1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mic1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mic1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mic1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mic1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mic1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mic1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mic1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_heart1 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_heart1, &icon_heart_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_heart1, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_heart1, 0);
    lv_obj_set_x(bk_ui->song_list_heart1, 500);
    lv_obj_set_y(bk_ui->song_list_heart1, 82);
    lv_obj_set_width(bk_ui->song_list_heart1, 36);
    lv_obj_set_height(bk_ui->song_list_heart1, 36);
    lv_obj_remove_flag(bk_ui->song_list_heart1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_heart1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_heart1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_heart1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_heart1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_heart1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_heart1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_heart1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_heart1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_heart1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_heart1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_heart1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_plus1 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_plus1, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_plus1, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_plus1, 0);
    lv_obj_set_x(bk_ui->song_list_plus1, 570);
    lv_obj_set_y(bk_ui->song_list_plus1, 82);
    lv_obj_set_width(bk_ui->song_list_plus1, 36);
    lv_obj_set_height(bk_ui->song_list_plus1, 36);
    lv_obj_remove_flag(bk_ui->song_list_plus1, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_plus1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_plus1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_plus1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_plus1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_plus1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_plus1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_plus1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_plus1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_plus1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_plus1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_plus1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mic2 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_mic2, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mic2, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mic2, 0);
    lv_obj_set_x(bk_ui->song_list_mic2, 430);
    lv_obj_set_y(bk_ui->song_list_mic2, 147);
    lv_obj_set_width(bk_ui->song_list_mic2, 36);
    lv_obj_set_height(bk_ui->song_list_mic2, 36);
    lv_obj_remove_flag(bk_ui->song_list_mic2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_mic2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mic2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mic2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mic2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mic2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mic2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mic2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mic2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mic2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mic2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mic2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_heart2 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_heart2, &icon_heart_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_heart2, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_heart2, 0);
    lv_obj_set_x(bk_ui->song_list_heart2, 500);
    lv_obj_set_y(bk_ui->song_list_heart2, 147);
    lv_obj_set_width(bk_ui->song_list_heart2, 36);
    lv_obj_set_height(bk_ui->song_list_heart2, 36);
    lv_obj_remove_flag(bk_ui->song_list_heart2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_heart2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_heart2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_heart2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_heart2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_heart2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_heart2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_heart2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_heart2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_heart2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_heart2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_heart2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_plus2 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_plus2, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_plus2, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_plus2, 0);
    lv_obj_set_x(bk_ui->song_list_plus2, 570);
    lv_obj_set_y(bk_ui->song_list_plus2, 147);
    lv_obj_set_width(bk_ui->song_list_plus2, 36);
    lv_obj_set_height(bk_ui->song_list_plus2, 36);
    lv_obj_remove_flag(bk_ui->song_list_plus2, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_plus2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_plus2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_plus2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_plus2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_plus2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_plus2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_plus2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_plus2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_plus2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_plus2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_plus2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mic3 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_mic3, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mic3, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mic3, 0);
    lv_obj_set_x(bk_ui->song_list_mic3, 430);
    lv_obj_set_y(bk_ui->song_list_mic3, 212);
    lv_obj_set_width(bk_ui->song_list_mic3, 36);
    lv_obj_set_height(bk_ui->song_list_mic3, 36);
    lv_obj_remove_flag(bk_ui->song_list_mic3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_mic3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mic3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mic3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mic3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mic3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mic3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mic3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mic3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mic3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mic3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mic3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_heart3 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_heart3, &icon_heart_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_heart3, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_heart3, 0);
    lv_obj_set_x(bk_ui->song_list_heart3, 500);
    lv_obj_set_y(bk_ui->song_list_heart3, 212);
    lv_obj_set_width(bk_ui->song_list_heart3, 36);
    lv_obj_set_height(bk_ui->song_list_heart3, 36);
    lv_obj_remove_flag(bk_ui->song_list_heart3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_heart3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_heart3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_heart3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_heart3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_heart3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_heart3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_heart3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_heart3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_heart3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_heart3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_heart3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_plus3 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_plus3, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_plus3, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_plus3, 0);
    lv_obj_set_x(bk_ui->song_list_plus3, 570);
    lv_obj_set_y(bk_ui->song_list_plus3, 212);
    lv_obj_set_width(bk_ui->song_list_plus3, 36);
    lv_obj_set_height(bk_ui->song_list_plus3, 36);
    lv_obj_remove_flag(bk_ui->song_list_plus3, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_plus3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_plus3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_plus3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_plus3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_plus3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_plus3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_plus3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_plus3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_plus3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_plus3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_plus3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mic4 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_mic4, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mic4, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mic4, 0);
    lv_obj_set_x(bk_ui->song_list_mic4, 430);
    lv_obj_set_y(bk_ui->song_list_mic4, 277);
    lv_obj_set_width(bk_ui->song_list_mic4, 36);
    lv_obj_set_height(bk_ui->song_list_mic4, 36);
    lv_obj_remove_flag(bk_ui->song_list_mic4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_mic4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mic4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mic4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mic4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mic4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mic4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mic4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mic4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mic4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mic4, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mic4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_heart4 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_heart4, &icon_heart_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_heart4, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_heart4, 0);
    lv_obj_set_x(bk_ui->song_list_heart4, 500);
    lv_obj_set_y(bk_ui->song_list_heart4, 277);
    lv_obj_set_width(bk_ui->song_list_heart4, 36);
    lv_obj_set_height(bk_ui->song_list_heart4, 36);
    lv_obj_remove_flag(bk_ui->song_list_heart4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_heart4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_heart4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_heart4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_heart4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_heart4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_heart4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_heart4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_heart4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_heart4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_heart4, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_heart4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_plus4 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_plus4, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_plus4, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_plus4, 0);
    lv_obj_set_x(bk_ui->song_list_plus4, 570);
    lv_obj_set_y(bk_ui->song_list_plus4, 277);
    lv_obj_set_width(bk_ui->song_list_plus4, 36);
    lv_obj_set_height(bk_ui->song_list_plus4, 36);
    lv_obj_remove_flag(bk_ui->song_list_plus4, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_plus4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_plus4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_plus4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_plus4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_plus4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_plus4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_plus4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_plus4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_plus4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_plus4, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_plus4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mic5 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_mic5, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mic5, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mic5, 0);
    lv_obj_set_x(bk_ui->song_list_mic5, 430);
    lv_obj_set_y(bk_ui->song_list_mic5, 342);
    lv_obj_set_width(bk_ui->song_list_mic5, 36);
    lv_obj_set_height(bk_ui->song_list_mic5, 36);
    lv_obj_remove_flag(bk_ui->song_list_mic5, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_mic5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mic5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mic5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mic5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mic5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mic5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mic5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mic5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mic5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mic5, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mic5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_heart5 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_heart5, &icon_heart_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_heart5, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_heart5, 0);
    lv_obj_set_x(bk_ui->song_list_heart5, 500);
    lv_obj_set_y(bk_ui->song_list_heart5, 342);
    lv_obj_set_width(bk_ui->song_list_heart5, 36);
    lv_obj_set_height(bk_ui->song_list_heart5, 36);
    lv_obj_remove_flag(bk_ui->song_list_heart5, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_heart5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_heart5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_heart5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_heart5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_heart5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_heart5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_heart5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_heart5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_heart5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_heart5, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_heart5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_plus5 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_plus5, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_plus5, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_plus5, 0);
    lv_obj_set_x(bk_ui->song_list_plus5, 570);
    lv_obj_set_y(bk_ui->song_list_plus5, 342);
    lv_obj_set_width(bk_ui->song_list_plus5, 36);
    lv_obj_set_height(bk_ui->song_list_plus5, 36);
    lv_obj_remove_flag(bk_ui->song_list_plus5, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_plus5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_plus5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_plus5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_plus5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_plus5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_plus5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_plus5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_plus5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_plus5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_plus5, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_plus5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mic6 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_mic6, &icon_mic_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mic6, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mic6, 0);
    lv_obj_set_x(bk_ui->song_list_mic6, 430);
    lv_obj_set_y(bk_ui->song_list_mic6, 407);
    lv_obj_set_width(bk_ui->song_list_mic6, 36);
    lv_obj_set_height(bk_ui->song_list_mic6, 36);
    lv_obj_remove_flag(bk_ui->song_list_mic6, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_mic6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mic6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mic6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mic6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mic6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mic6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mic6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mic6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mic6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mic6, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mic6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_heart6 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_heart6, &icon_heart_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_heart6, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_heart6, 0);
    lv_obj_set_x(bk_ui->song_list_heart6, 500);
    lv_obj_set_y(bk_ui->song_list_heart6, 407);
    lv_obj_set_width(bk_ui->song_list_heart6, 36);
    lv_obj_set_height(bk_ui->song_list_heart6, 36);
    lv_obj_remove_flag(bk_ui->song_list_heart6, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_heart6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_heart6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_heart6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_heart6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_heart6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_heart6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_heart6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_heart6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_heart6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_heart6, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_heart6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_plus6 = lv_image_create(bk_ui->song_list_song_panel);
    lv_image_set_src(bk_ui->song_list_plus6, &icon_plus_36_36x36_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_plus6, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_plus6, 0);
    lv_obj_set_x(bk_ui->song_list_plus6, 570);
    lv_obj_set_y(bk_ui->song_list_plus6, 407);
    lv_obj_set_width(bk_ui->song_list_plus6, 36);
    lv_obj_set_height(bk_ui->song_list_plus6, 36);
    lv_obj_remove_flag(bk_ui->song_list_plus6, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_plus6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_plus6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_plus6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_plus6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_plus6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_plus6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_plus6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_plus6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_plus6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_plus6, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_plus6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_tab_all = lv_btn_create(bk_ui->song_list_song_panel);
    bk_ui->song_list_tab_all_label = lv_label_create(bk_ui->song_list_tab_all);
    lv_label_set_text_static(bk_ui->song_list_tab_all_label, "全部");
    lv_label_set_long_mode(bk_ui->song_list_tab_all_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_tab_all_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_tab_all, 32);
    lv_obj_set_y(bk_ui->song_list_tab_all, 24);
    lv_obj_set_width(bk_ui->song_list_tab_all, 110);
    lv_obj_set_height(bk_ui->song_list_tab_all, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_tab_all, lv_color_hex(0x356eda), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_tab_all, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_tab_all, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_tab_all, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_tab_all, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_tab_all, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_tab_all, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_tab_all, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_tab_all, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_tab_all, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_tab_all, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_tab_all, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_tab_all, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_tab_all, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_tab_all, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_tab_all, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_tab_all, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_tab_all, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_tab_all, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_tab_all, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_tab_cn = lv_btn_create(bk_ui->song_list_song_panel);
    bk_ui->song_list_tab_cn_label = lv_label_create(bk_ui->song_list_tab_cn);
    lv_label_set_text_static(bk_ui->song_list_tab_cn_label, "国语");
    lv_label_set_long_mode(bk_ui->song_list_tab_cn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_tab_cn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_tab_cn, 160);
    lv_obj_set_y(bk_ui->song_list_tab_cn, 24);
    lv_obj_set_width(bk_ui->song_list_tab_cn, 110);
    lv_obj_set_height(bk_ui->song_list_tab_cn, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_tab_cn, lv_color_hex(0xe2e8f5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_tab_cn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_tab_cn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_tab_cn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_tab_cn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_tab_cn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_tab_cn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_tab_cn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_tab_cn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_tab_cn, lv_color_hex(0x7c859c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_tab_cn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_tab_cn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_tab_cn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_tab_cn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_tab_cn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_tab_cn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_tab_cn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_tab_cn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_tab_cn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_tab_cn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_tab_en = lv_btn_create(bk_ui->song_list_song_panel);
    bk_ui->song_list_tab_en_label = lv_label_create(bk_ui->song_list_tab_en);
    lv_label_set_text_static(bk_ui->song_list_tab_en_label, "粤语");
    lv_label_set_long_mode(bk_ui->song_list_tab_en_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_tab_en_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_tab_en, 288);
    lv_obj_set_y(bk_ui->song_list_tab_en, 24);
    lv_obj_set_width(bk_ui->song_list_tab_en, 110);
    lv_obj_set_height(bk_ui->song_list_tab_en, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_tab_en, lv_color_hex(0xe2e8f5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_tab_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_tab_en, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_tab_en, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_tab_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_tab_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_tab_en, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_tab_en, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_tab_en, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_tab_en, lv_color_hex(0x7c859c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_tab_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_tab_en, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_tab_en, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_tab_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_tab_en, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_tab_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_tab_en, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_tab_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_tab_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_tab_en, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_tab_other = lv_btn_create(bk_ui->song_list_song_panel);
    bk_ui->song_list_tab_other_label = lv_label_create(bk_ui->song_list_tab_other);
    lv_label_set_text_static(bk_ui->song_list_tab_other_label, "其它");
    lv_label_set_long_mode(bk_ui->song_list_tab_other_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_tab_other_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_tab_other, 416);
    lv_obj_set_y(bk_ui->song_list_tab_other, 24);
    lv_obj_set_width(bk_ui->song_list_tab_other, 110);
    lv_obj_set_height(bk_ui->song_list_tab_other, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_tab_other, lv_color_hex(0xe2e8f5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_tab_other, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_tab_other, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_tab_other, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_tab_other, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_tab_other, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_tab_other, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_tab_other, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_tab_other, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_tab_other, lv_color_hex(0x7c859c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_tab_other, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_tab_other, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_tab_other, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_tab_other, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_tab_other, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_tab_other, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_tab_other, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_tab_other, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_tab_other, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_tab_other, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_sl_bottom = lv_obj_create(bk_ui->song_list_sl_bg);
    lv_obj_set_x(bk_ui->song_list_sl_bottom, 0);
    lv_obj_set_y(bk_ui->song_list_sl_bottom, 650);
    lv_obj_set_width(bk_ui->song_list_sl_bottom, 1280);
    lv_obj_set_height(bk_ui->song_list_sl_bottom, 70);
    lv_obj_remove_flag(bk_ui->song_list_sl_bottom, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_bottom, lv_color_hex(0x46133f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_bottom, 232, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_bottom, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_bottom, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_bottom, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_bottom, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_bottom, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_bottom, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_sl_bottom, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_sl_bottom, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_sl_bottom, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_sl_bottom, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_sl_bottom, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_sl_bottom, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_sl_bottom, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_sl_bottom, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_sl_bottom, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_sl_bottom, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_sl_bottom, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_sl_bottom, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_bot_actions = lv_obj_create(bk_ui->song_list_sl_bottom);
    lv_obj_set_x(bk_ui->song_list_bot_actions, 0);
    lv_obj_set_y(bk_ui->song_list_bot_actions, 0);
    lv_obj_set_width(bk_ui->song_list_bot_actions, 1280);
    lv_obj_set_height(bk_ui->song_list_bot_actions, 70);
    lv_obj_remove_flag(bk_ui->song_list_bot_actions, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_bot_actions, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_bot_actions, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_bot_actions, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_bot_actions, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_bot_actions, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_bot_actions, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_bot_actions, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_bot_actions, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_bot_actions, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_bot_actions, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_bot_actions, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_bot_actions, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_bot_actions, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_bot_actions, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_bot_actions, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_bot_actions, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_bot_actions, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_bot_actions, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_mute_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_mute_btn_label = lv_label_create(bk_ui->song_list_mute_btn);
    lv_label_set_text_static(bk_ui->song_list_mute_btn_label, "X静音");
    lv_label_set_long_mode(bk_ui->song_list_mute_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_mute_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_mute_btn, 28);
    lv_obj_set_y(bk_ui->song_list_mute_btn, 14);
    lv_obj_set_width(bk_ui->song_list_mute_btn, 96);
    lv_obj_set_height(bk_ui->song_list_mute_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->song_list_mute_btn, lv_color_hex(0x8f3b55), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mute_btn, 221, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mute_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mute_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mute_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mute_btn, 61, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mute_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mute_btn, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mute_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_mute_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_mute_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_mute_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_mute_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mute_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mute_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mute_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_mute_btn, song_list_mute_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_vol_down_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_vol_down_btn_label = lv_label_create(bk_ui->song_list_vol_down_btn);
    lv_label_set_text_static(bk_ui->song_list_vol_down_btn_label, "-");
    lv_label_set_long_mode(bk_ui->song_list_vol_down_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_vol_down_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_vol_down_btn, 232);
    lv_obj_set_y(bk_ui->song_list_vol_down_btn, 15);
    lv_obj_set_width(bk_ui->song_list_vol_down_btn, 42);
    lv_obj_set_height(bk_ui->song_list_vol_down_btn, 40);
    lv_obj_set_style_bg_color(bk_ui->song_list_vol_down_btn, lv_color_hex(0xb65078), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_vol_down_btn, 238, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_vol_down_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_vol_down_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_vol_down_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_vol_down_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_vol_down_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_vol_down_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_vol_down_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_vol_down_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_vol_down_btn, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_vol_down_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_vol_down_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_vol_down_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_vol_down_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_vol_down_btn, song_list_vol_down_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_vol_up_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_vol_up_btn_label = lv_label_create(bk_ui->song_list_vol_up_btn);
    lv_label_set_text_static(bk_ui->song_list_vol_up_btn_label, "+");
    lv_label_set_long_mode(bk_ui->song_list_vol_up_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_vol_up_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_vol_up_btn, 408);
    lv_obj_set_y(bk_ui->song_list_vol_up_btn, 15);
    lv_obj_set_width(bk_ui->song_list_vol_up_btn, 42);
    lv_obj_set_height(bk_ui->song_list_vol_up_btn, 40);
    lv_obj_set_style_bg_color(bk_ui->song_list_vol_up_btn, lv_color_hex(0xd34d90), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_vol_up_btn, 240, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_vol_up_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_vol_up_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_vol_up_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_vol_up_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_vol_up_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_vol_up_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_vol_up_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_vol_up_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_vol_up_btn, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_vol_up_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_vol_up_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_vol_up_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_vol_up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_vol_up_btn, song_list_vol_up_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_vocal_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_vocal_btn_label = lv_label_create(bk_ui->song_list_vocal_btn);
    lv_label_set_text(bk_ui->song_list_vocal_btn_label,
                      klok_player_is_accompany() ? "原唱" : "伴唱");
    lv_label_set_long_mode(bk_ui->song_list_vocal_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_vocal_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_vocal_btn, 486);
    lv_obj_set_y(bk_ui->song_list_vocal_btn, 14);
    lv_obj_set_width(bk_ui->song_list_vocal_btn, 92);
    lv_obj_set_height(bk_ui->song_list_vocal_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->song_list_vocal_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_vocal_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_vocal_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_vocal_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_vocal_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_vocal_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_vocal_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_vocal_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_vocal_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_vocal_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_vocal_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(
        bk_ui->song_list_vocal_btn,
        klok_player_is_accompany()
            ? &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18
            : &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_17,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_vocal_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_vocal_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_vocal_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_vocal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_vocal_btn, song_list_vocal_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_replay_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_replay_btn_label = lv_label_create(bk_ui->song_list_replay_btn);
    lv_label_set_text_static(bk_ui->song_list_replay_btn_label, "重唱");
    lv_label_set_long_mode(bk_ui->song_list_replay_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_replay_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_replay_btn, 596);
    lv_obj_set_y(bk_ui->song_list_replay_btn, 14);
    lv_obj_set_width(bk_ui->song_list_replay_btn, 92);
    lv_obj_set_height(bk_ui->song_list_replay_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->song_list_replay_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_replay_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_replay_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_replay_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_replay_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_replay_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_replay_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_replay_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_replay_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_replay_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_replay_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_replay_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_replay_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_replay_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_replay_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_replay_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_replay_btn, song_list_replay_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_next_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_next_btn_label = lv_label_create(bk_ui->song_list_next_btn);
    lv_label_set_text_static(bk_ui->song_list_next_btn_label, "切歌");
    lv_label_set_long_mode(bk_ui->song_list_next_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_next_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_next_btn, 706);
    lv_obj_set_y(bk_ui->song_list_next_btn, 14);
    lv_obj_set_width(bk_ui->song_list_next_btn, 92);
    lv_obj_set_height(bk_ui->song_list_next_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->song_list_next_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_next_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_next_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_next_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_next_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_next_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_next_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_next_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_next_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_next_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_next_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_next_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_next_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_next_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_next_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_next_btn, song_list_next_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_play_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_play_btn_label = lv_label_create(bk_ui->song_list_play_btn);
    lv_label_set_text_static(bk_ui->song_list_play_btn_label, "播放");
    lv_label_set_long_mode(bk_ui->song_list_play_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_play_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_play_btn, 816);
    lv_obj_set_y(bk_ui->song_list_play_btn, 14);
    lv_obj_set_width(bk_ui->song_list_play_btn, 92);
    lv_obj_set_height(bk_ui->song_list_play_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->song_list_play_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_play_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_play_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_play_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_play_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_play_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_play_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_play_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_play_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_play_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_play_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_play_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_play_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_play_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_play_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_play_btn, song_list_play_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_full_btn = lv_btn_create(bk_ui->song_list_bot_actions);
    bk_ui->song_list_full_btn_label = lv_label_create(bk_ui->song_list_full_btn);
    lv_label_set_text_static(bk_ui->song_list_full_btn_label, "全屏播放");
    lv_label_set_long_mode(bk_ui->song_list_full_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_full_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_full_btn, 926);
    lv_obj_set_y(bk_ui->song_list_full_btn, 14);
    lv_obj_set_width(bk_ui->song_list_full_btn, 138);
    lv_obj_set_height(bk_ui->song_list_full_btn, 42);
    lv_obj_set_style_bg_color(bk_ui->song_list_full_btn, lv_color_hex(0x321a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_full_btn, 192, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_full_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_full_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_full_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_full_btn, 54, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_full_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_full_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_full_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_full_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_full_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_full_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_full_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_full_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_full_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_full_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_full_btn, song_list_full_btn_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_volume_bar = lv_slider_create(bk_ui->song_list_bot_actions);
    lv_slider_set_range(bk_ui->song_list_volume_bar, 0, 100);
    lv_slider_set_value(bk_ui->song_list_volume_bar, klok_player_get_volume(), LV_ANIM_OFF);
    lv_slider_set_mode(bk_ui->song_list_volume_bar, LV_SLIDER_MODE_NORMAL);
    lv_obj_set_x(bk_ui->song_list_volume_bar, 290);
    lv_obj_set_y(bk_ui->song_list_volume_bar, 30);
    lv_obj_set_width(bk_ui->song_list_volume_bar, 108);
    lv_obj_set_height(bk_ui->song_list_volume_bar, 10);
    lv_obj_remove_flag(bk_ui->song_list_volume_bar, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_volume_bar, 136, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_volume_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_volume_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_volume_bar, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_volume_bar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_volume_bar, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_volume_bar, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_volume_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(bk_ui->song_list_volume_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(bk_ui->song_list_volume_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_volume_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_volume_bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_volume_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_volume_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_volume_bar, LV_BORDER_SIDE_FULL, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_volume_bar, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_volume_bar, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_volume_bar, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_volume_bar, LV_GRAD_DIR_NONE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_volume_bar, lv_color_hex(0xffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_volume_bar, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_volume_bar, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_volume_bar, LV_BORDER_SIDE_FULL, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_volume_bar, 7, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_volume_bar, false, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bk_ui->song_list_volume_bar, song_list_volume_bar_event_cb, LV_EVENT_ALL, NULL);

    bk_ui->song_list_top_status = lv_obj_create(bk_ui->song_list_sl_bg);
    lv_obj_set_x(bk_ui->song_list_top_status, 650);
    lv_obj_set_y(bk_ui->song_list_top_status, 26);
    lv_obj_set_width(bk_ui->song_list_top_status, 605);
    lv_obj_set_height(bk_ui->song_list_top_status, 44);
    lv_obj_set_style_bg_color(bk_ui->song_list_top_status, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_top_status, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_top_status, lv_color_hex(0xd9d9d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_top_status, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_top_status, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_top_status, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_top_status, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_top_status, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_top_status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->song_list_top_status, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_top_status, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_top_status, LV_GRAD_DIR_NONE, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_top_status, lv_color_hex(0xffffff), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_top_status, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_top_status, 255, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_top_status, LV_BORDER_SIDE_FULL, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_top_status, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_top_status, false, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    bk_ui->song_list_vip_info = lv_btn_create(bk_ui->song_list_top_status);
    bk_ui->song_list_vip_info_label = lv_label_create(bk_ui->song_list_vip_info);
    lv_label_set_text_static(bk_ui->song_list_vip_info_label, "VIP剩余");
    lv_label_set_long_mode(bk_ui->song_list_vip_info_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_vip_info_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_vip_info, 0);
    lv_obj_set_y(bk_ui->song_list_vip_info, 3);
    lv_obj_set_width(bk_ui->song_list_vip_info, 160);
    lv_obj_set_height(bk_ui->song_list_vip_info, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_vip_info, lv_color_hex(0x10224f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_vip_info, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_vip_info, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_vip_info, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_vip_info, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_vip_info, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_vip_info, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_vip_info, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_vip_info, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_vip_info, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_vip_info, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_vip_info, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_vip_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_vip_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_vip_info, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_vip_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_vip_info, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_vip_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_vip_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_vip_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_vip_day = lv_btn_create(bk_ui->song_list_top_status);
    bk_ui->song_list_vip_day_label = lv_label_create(bk_ui->song_list_vip_day);
    lv_label_set_text_static(bk_ui->song_list_vip_day_label, "332天");
    lv_label_set_long_mode(bk_ui->song_list_vip_day_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_vip_day_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_vip_day, 166);
    lv_obj_set_y(bk_ui->song_list_vip_day, 7);
    lv_obj_set_width(bk_ui->song_list_vip_day, 78);
    lv_obj_set_height(bk_ui->song_list_vip_day, 30);
    lv_obj_set_style_bg_color(bk_ui->song_list_vip_day, lv_color_hex(0xffc21c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_vip_day, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_vip_day, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_vip_day, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_vip_day, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_vip_day, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_vip_day, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_vip_day, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_vip_day, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_vip_day, lv_color_hex(0x24172e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_vip_day, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_vip_day, &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_vip_day, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_vip_day, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_vip_day, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_vip_day, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_vip_day, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_vip_day, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_vip_day, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_vip_day, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mix_btn = lv_btn_create(bk_ui->song_list_top_status);
    bk_ui->song_list_mix_btn_label = lv_label_create(bk_ui->song_list_mix_btn);
    lv_label_set_text_static(bk_ui->song_list_mix_btn_label, "调音台");
    lv_label_set_long_mode(bk_ui->song_list_mix_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_mix_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_mix_btn, 285);
    lv_obj_set_y(bk_ui->song_list_mix_btn, 3);
    lv_obj_set_width(bk_ui->song_list_mix_btn, 130);
    lv_obj_set_height(bk_ui->song_list_mix_btn, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_mix_btn, lv_color_hex(0x10224f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mix_btn, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mix_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mix_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mix_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mix_btn, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mix_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mix_btn, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mix_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_mix_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_mix_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_mix_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_mix_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_mix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mix_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mix_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mix_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_mix_icon = lv_image_create(bk_ui->song_list_top_status);
    lv_image_set_src(bk_ui->song_list_mix_icon, &nav_eq_40x40_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->song_list_mix_icon, 50, 50);
    lv_image_set_rotation(bk_ui->song_list_mix_icon, 0);
    lv_obj_set_x(bk_ui->song_list_mix_icon, 373);
    lv_obj_set_y(bk_ui->song_list_mix_icon, 2);
    lv_obj_set_width(bk_ui->song_list_mix_icon, 40);
    lv_obj_set_height(bk_ui->song_list_mix_icon, 40);
    lv_obj_set_style_bg_color(bk_ui->song_list_mix_icon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_mix_icon, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_mix_icon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_mix_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_mix_icon, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_mix_icon, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_mix_icon, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_mix_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->song_list_mix_icon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->song_list_mix_icon, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->song_list_mix_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_queue_btn = lv_btn_create(bk_ui->song_list_top_status);
    bk_ui->song_list_queue_btn_label = lv_label_create(bk_ui->song_list_queue_btn);
    lv_label_set_text_static(bk_ui->song_list_queue_btn_label, "已点");
    lv_label_set_long_mode(bk_ui->song_list_queue_btn_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_queue_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_queue_btn, 445);
    lv_obj_set_y(bk_ui->song_list_queue_btn, 3);
    lv_obj_set_width(bk_ui->song_list_queue_btn, 112);
    lv_obj_set_height(bk_ui->song_list_queue_btn, 38);
    lv_obj_set_style_bg_color(bk_ui->song_list_queue_btn, lv_color_hex(0x10224f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_queue_btn, 170, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_queue_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_queue_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_queue_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_queue_btn, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_queue_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_queue_btn, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_queue_btn, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_queue_btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_queue_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_queue_btn, &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_queue_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_queue_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_queue_btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_queue_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_queue_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_queue_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_queue_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_queue_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->song_list_queue_num = lv_btn_create(bk_ui->song_list_top_status);
    bk_ui->song_list_queue_num_label = lv_label_create(bk_ui->song_list_queue_num);
    lv_label_set_text_static(bk_ui->song_list_queue_num_label, "0");
    lv_label_set_long_mode(bk_ui->song_list_queue_num_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(bk_ui->song_list_queue_num_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->song_list_queue_num, 564);
    lv_obj_set_y(bk_ui->song_list_queue_num, 7);
    lv_obj_set_width(bk_ui->song_list_queue_num, 34);
    lv_obj_set_height(bk_ui->song_list_queue_num, 30);
    lv_obj_set_style_bg_color(bk_ui->song_list_queue_num, lv_color_hex(0xffc21c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->song_list_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->song_list_queue_num, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->song_list_queue_num, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->song_list_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->song_list_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->song_list_queue_num, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->song_list_queue_num, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->song_list_queue_num, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->song_list_queue_num, lv_color_hex(0x24172e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->song_list_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->song_list_queue_num, &lv_font_Alibaba_PuHuiTi_2_0_95_ExtraBold_95_ExtraBold_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->song_list_queue_num, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->song_list_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->song_list_queue_num, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->song_list_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->song_list_queue_num, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->song_list_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->song_list_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->song_list_queue_num, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    song_list_update_mute_button();
    song_list_update_play_button();
    klok_song_list_load_from_sd(bk_ui);

    lv_obj_update_layout(bk_ui->song_list);
}

/*
 * @brief: destroy page song_list
 */
void destroy_page_song_list(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->song_list != NULL) {
        lv_obj_del(bk_ui->song_list);
        bk_ui->song_list = NULL;
    }
}