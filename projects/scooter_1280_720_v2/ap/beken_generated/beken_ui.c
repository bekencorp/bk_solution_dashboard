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
/*
 * @file: beken_ui.c
 * @brief: beken ui implementation file
 * This file contains the implementation of the Beken UI system.
 * Customers can modify the UI implementation in beken_ui.c without
 * touching the main application code.
 *
 * This file is intentionally kept thin: it only drives navigation between the
 * pages (the home nav menu state machine, page load/free, physical-key hooks
 * and init/teardown). Each page's own business logic lives in its module:
 *   - home page  -> home/home_ui.c   (home_ui_*)
 *   - dashcam    -> dashcam/dashcam_ui.c (dashcam_ui_*)
 *   - ota update -> ota/ota_ui.c     (ota_ui_*)
 */

#include "beken_ui.h"
#include <stdbool.h>
#include <stdint.h>
#include "components/log.h"
#include <os/os.h>
#include "lv_vendor.h"
#include "boot_bg_preload.h"
#include "dashcam_ui.h"
#include "dashcam_config.h"
#include "ota_ui.h"
#include "home_ui.h"

bk_lv_ui_t bk_lv_tool_ui = {0};

/*
 * Wrap the background bitmap preloaded during the boot animation
 * (boot_bg_preload) onto a page's background image object, with no decoding on
 * the UI-init path. Shared by all pages so they reuse the single decoded
 * bitmap. Returns true when the preloaded bitmap was installed.
 */
bool beken_ui_install_preloaded_bg(lv_obj_t *bg_img)
{
    const lv_image_dsc_t *bg = boot_bg_preload_get(3000);

    if (bg == NULL || bg_img == NULL || !lv_obj_is_valid(bg_img))
    {
        return false;
    }

    lv_image_set_src(bg_img, bg);
    BK_LOGI("ui_bg", "using preloaded RGB565 bitmap (%dx%d)\n",
            (int)bg->header.w, (int)bg->header.h);
    return true;
}

/* ---------- Home nav panel: Home / Dashcam / OTA ---------- */
typedef enum
{
    HOME_MENU_HOME = 0,
    HOME_MENU_DASHCAM,
    HOME_MENU_OTA,
    HOME_MENU_COUNT,
} home_menu_item_t;

static int32_t s_home_menu_selected = HOME_MENU_HOME;
static int32_t s_home_menu_active = HOME_MENU_HOME;
static home_menu_item_t s_home_menu_next_candidate = HOME_MENU_DASHCAM;
static bool s_home_menu_armed = false;

static void beken_ui_start_dashcam_video(void);
static void beken_ui_stop_dashcam_video(void);
static void home_menu_return_home(void);
static void beken_ui_log_heap(const char *tag);

static void home_nav_entry_set_scale(lv_obj_t *obj, int32_t scale)
{
    lv_obj_set_style_transform_scale_x(obj, scale, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_scale_y(obj, scale, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void home_nav_image_set_scale(lv_obj_t *obj, int32_t scale)
{
    lv_image_set_scale(obj, (uint32_t)scale);
}

static void home_nav_image_scale_anim_cb(void *var, int32_t value)
{
    home_nav_image_set_scale((lv_obj_t *)var, value);
}

static void home_nav_image_animate_scale(lv_obj_t *obj, int32_t target_scale)
{
    int32_t current_scale;
    lv_anim_t anim;

    if (obj == NULL || !lv_obj_is_valid(obj))
    {
        return;
    }

    current_scale = lv_image_get_scale(obj);
    if (current_scale <= 0)
    {
        current_scale = 256;
    }

    lv_anim_delete(obj, home_nav_image_scale_anim_cb);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, home_nav_image_scale_anim_cb);
    lv_anim_set_values(&anim, current_scale, target_scale);
    lv_anim_set_duration(&anim, 180);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);

    lv_obj_set_style_image_opa(obj,
                               target_scale > 256 ? LV_OPA_COVER : LV_OPA_60,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void home_menu_apply_selection(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *dash = ui->home_dash_entry;
    lv_obj_t *ota = ui->home_ota_entry;
    lv_obj_t *dash_ic = ui->home_dash_ic;
    lv_obj_t *ota_ic = ui->home_ota_ic;
    const int32_t normal_scale = 256;
    const int32_t selected_scale = 410;

    if (ui->home == NULL || !lv_obj_is_valid(ui->home))
    {
        return;
    }

    if (dash != NULL && lv_obj_is_valid(dash))
    {
        lv_obj_set_style_border_width(dash, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(dash, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        home_nav_entry_set_scale(dash, normal_scale);
    }

    if (ota != NULL && lv_obj_is_valid(ota))
    {
        lv_obj_set_style_border_width(ota, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(ota, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        home_nav_entry_set_scale(ota, normal_scale);
    }

    if (dash_ic != NULL && lv_obj_is_valid(dash_ic))
    {
        lv_image_set_pivot(dash_ic, lv_obj_get_width(dash_ic) / 2, lv_obj_get_height(dash_ic) / 2);
        home_nav_image_animate_scale(dash_ic,
                                     s_home_menu_selected == HOME_MENU_DASHCAM ? selected_scale : normal_scale);
    }

    if (ota_ic != NULL && lv_obj_is_valid(ota_ic))
    {
        lv_image_set_pivot(ota_ic, lv_obj_get_width(ota_ic) / 2, lv_obj_get_height(ota_ic) / 2);
        home_nav_image_animate_scale(ota_ic,
                                     s_home_menu_selected == HOME_MENU_OTA ? selected_scale : normal_scale);
    }
}

static void home_menu_select(home_menu_item_t item)
{
    s_home_menu_selected = item;
    s_home_menu_armed = (item != HOME_MENU_HOME);
    home_menu_apply_selection();
}

/*
 * LVGL draws from the (small) AP SRAM heap, and the object trees now live there
 * too (lv_mem.c -> os_malloc). Keeping the home + dashcam + ota screens all
 * resident at once overflows that heap: the home speed-gauge redraw burst alone
 * allocates many arc draw tasks, and the dashcam page additionally needs a large
 * GC2053 720P ISP calibration buffer out of the same heap (a resident home tree
 * starved it and broke camera open). So free whichever screen is not currently
 * shown - including home - and lazily recreate it on next entry.
 */
static void beken_ui_free_heavy_page(bk_lv_ui_t *ui, int32_t page)
{
    lv_obj_t **slot = NULL;
    lv_obj_t *victim = NULL;

    if (page == HOME_MENU_DASHCAM)
    {
        slot = &ui->dashcam;
    }
    else if (page == HOME_MENU_OTA)
    {
        slot = &ui->ota_update;
    }
    else if (page == HOME_MENU_HOME)
    {
        slot = &ui->home;
    }

    if (slot == NULL || *slot == NULL)
    {
        return;
    }

    victim = *slot;
    if (!lv_obj_is_valid(victim))
    {
        *slot = NULL;
        return;
    }

    /* Never delete the screen that is currently being displayed. */
    if (victim == lv_screen_active())
    {
        return;
    }

    /* The home screen owns canvas buffers LVGL does not free and keeps static
     * handles into its tree; let home_ui drop them before the tree is deleted. */
    if (page == HOME_MENU_HOME)
    {
        home_ui_unload();
    }

    lv_obj_delete(victim);
    *slot = NULL;
}

static void home_menu_free_inactive_heavy_pages(int32_t active_page)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (active_page != HOME_MENU_DASHCAM)
    {
        beken_ui_free_heavy_page(ui, HOME_MENU_DASHCAM);
    }
    if (active_page != HOME_MENU_OTA)
    {
        beken_ui_free_heavy_page(ui, HOME_MENU_OTA);
    }
    if (active_page != HOME_MENU_HOME)
    {
        beken_ui_free_heavy_page(ui, HOME_MENU_HOME);
    }
}

static void home_menu_load_selected_page(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    int32_t old_page = s_home_menu_active;
    lv_obj_t *target = NULL;
    bool switching_page = old_page != s_home_menu_selected;

    if (switching_page)
    {
        if (old_page == HOME_MENU_HOME)
        {
            home_ui_leave();
        }
        else if (old_page == HOME_MENU_OTA)
        {
            ota_ui_leave();
        }
        else if (old_page == HOME_MENU_DASHCAM)
        {
            beken_ui_stop_dashcam_video();
        }
    }

    switch (s_home_menu_selected)
    {
    case HOME_MENU_HOME:
        if (ui->home == NULL)
        {
            init_page_home(ui);
            home_ui_install_bg();
        }
        target = ui->home;
        break;
    case HOME_MENU_DASHCAM:
        if (ui->dashcam == NULL)
        {
            init_page_dashcam(ui);
            beken_ui_install_preloaded_bg(ui->dashcam_bg_img);
        }
        target = ui->dashcam;
        break;
    case HOME_MENU_OTA:
        if (ui->ota_update == NULL)
        {
            init_page_ota_update(ui);
            beken_ui_install_preloaded_bg(ui->ota_update_bg_img);
        }
        target = ui->ota_update;
        break;
    default:
        break;
    }

    if (target == NULL || !lv_obj_is_valid(target))
    {
        return;
    }

    lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    home_menu_free_inactive_heavy_pages(s_home_menu_selected);
    beken_ui_log_heap("page-load");

    s_home_menu_active = s_home_menu_selected;
    s_home_menu_armed = false;

    if (s_home_menu_active == HOME_MENU_HOME)
    {
        home_ui_enter();
    }
    else if (s_home_menu_active == HOME_MENU_DASHCAM)
    {
        beken_ui_start_dashcam_video();
    }
    else if (s_home_menu_active == HOME_MENU_OTA)
    {
        ota_ui_enter();
    }
}

static void home_menu_open(home_menu_item_t item)
{
    home_menu_select(item);
    home_menu_load_selected_page();
}

static void home_menu_return_home(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    int32_t old_page = s_home_menu_active;

    if (old_page == HOME_MENU_OTA)
    {
        ota_ui_leave();
    }
    else if (old_page == HOME_MENU_DASHCAM)
    {
        beken_ui_stop_dashcam_video();
    }

    if (ui->home == NULL || !lv_obj_is_valid(ui->home))
    {
        init_page_home(ui);
        home_ui_install_bg();
    }

    if (ui->home != NULL && lv_obj_is_valid(ui->home))
    {
        lv_screen_load_anim(ui->home, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        home_menu_free_inactive_heavy_pages(HOME_MENU_HOME);
        s_home_menu_active = HOME_MENU_HOME;
        s_home_menu_armed = true;
        home_menu_apply_selection();
        home_ui_enter();
    }
}

static void home_menu_entry_press(home_menu_item_t item)
{
    if (s_home_menu_active != HOME_MENU_HOME)
    {
        home_menu_open(HOME_MENU_HOME);
    }
    else if (s_home_menu_selected == item && s_home_menu_armed)
    {
        home_menu_load_selected_page();
    }
    else
    {
        home_menu_select(item);
    }
}

static void home_menu_entry_press_async_cb(void *user_data)
{
    home_menu_entry_press((home_menu_item_t)(uintptr_t)user_data);
}

static void home_menu_select_delta(int32_t delta)
{
    s_home_menu_selected += delta;
    if (s_home_menu_selected <= HOME_MENU_HOME)
    {
        s_home_menu_selected = HOME_MENU_OTA;
    }
    else if (s_home_menu_selected >= HOME_MENU_COUNT)
    {
        s_home_menu_selected = HOME_MENU_DASHCAM;
    }

    s_home_menu_armed = true;
    home_menu_apply_selection();
}

void beken_ui_menu_prev(void)
{
    lv_vendor_disp_lock();
    if (s_home_menu_active == HOME_MENU_HOME)
    {
        home_menu_select_delta(-1);
    }
    else
    {
        home_menu_return_home();
    }
    lv_vendor_disp_unlock();
}

void beken_ui_menu_next(void)
{
    lv_vendor_disp_lock();
    if (s_home_menu_active == HOME_MENU_HOME)
    {
        home_menu_select_delta(1);
    }
    else
    {
        home_menu_return_home();
    }
    lv_vendor_disp_unlock();
}

void beken_ui_menu_enter(void)
{
    lv_vendor_disp_lock();
    if (s_home_menu_active != HOME_MENU_HOME)
    {
        home_menu_return_home();
    }
    else if (s_home_menu_selected == HOME_MENU_HOME)
    {
        home_menu_select(HOME_MENU_DASHCAM);
    }
    else
    {
        home_menu_load_selected_page();
    }
    lv_vendor_disp_unlock();
}

void beken_ui_nav_to_dashcam(void)
{
    lv_async_call(home_menu_entry_press_async_cb, (void *)(uintptr_t)HOME_MENU_DASHCAM);
}

void beken_ui_nav_to_ota_update(void)
{
    lv_async_call(home_menu_entry_press_async_cb, (void *)(uintptr_t)HOME_MENU_OTA);
}

/*
 * Heap probe for the OOM (LV_ASSERT_MALLOC) investigation. Logs the free and
 * lowest-ever-free bytes of the three heaps LVGL can draw from. Compare the
 * numbers across repeated toggles: a steadily shrinking "free" => leak; a flat
 * "free" that already sits near the "min" => chronically on the edge.
 */
static void beken_ui_log_heap(const char *tag)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);

    BK_LOGI("ui_heap",
            "%s sram=%u(min %u) psram=%u hsram=%u | lvgl free=%u biggest=%u used%%=%u frag%%=%u\n",
            tag,
            (unsigned)rtos_get_free_heap_size(),
            (unsigned)rtos_get_minimum_free_heap_size(),
            (unsigned)rtos_get_psram_free_heap_size(),
            (unsigned)rtos_get_hsram_free_heap_size(),
            (unsigned)mon.free_size,
            (unsigned)mon.free_biggest_size,
            (unsigned)mon.used_pct,
            (unsigned)mon.frag_pct);
}

static void home_menu_toggle_async_cb(void *user_data)
{
    (void)user_data;

    beken_ui_log_heap("toggle-in");

    /* On the dashcam page, a single press drives list navigation when the list
     * is focused (req6 #3); otherwise fall through to the home-menu behavior. */
    if (dashcam_ui_handle_key_single())
    {
        beken_ui_log_heap("toggle-out");
        return;
    }

    if (s_home_menu_active != HOME_MENU_HOME)
    {
        home_menu_return_home();
    }
    else
    {
        home_menu_item_t selected = (home_menu_item_t)s_home_menu_selected;

        if (selected != HOME_MENU_DASHCAM && selected != HOME_MENU_OTA)
        {
            selected = s_home_menu_next_candidate;
        }

        selected = selected == HOME_MENU_DASHCAM ? HOME_MENU_OTA : HOME_MENU_DASHCAM;
        s_home_menu_next_candidate = selected;
        home_menu_select(selected);
    }

    beken_ui_log_heap("toggle-out");
}

static void home_menu_double_async_cb(void *user_data)
{
    (void)user_data;

    /* Double press toggles dashcam list focus (req6 #3); ignored elsewhere. */
    (void)dashcam_ui_handle_key_double();
}

static void home_menu_enter_async_cb(void *user_data)
{
    (void)user_data;

    /* On the dashcam page with the list focused, a long press plays the
     * selected clip (req6 #3) instead of returning to the home menu. */
    if (dashcam_ui_handle_key_long())
    {
        return;
    }

    if (s_home_menu_active != HOME_MENU_HOME)
    {
        home_menu_return_home();
        return;
    }

    if (s_home_menu_selected != HOME_MENU_DASHCAM && s_home_menu_selected != HOME_MENU_OTA)
    {
        home_menu_select(s_home_menu_next_candidate);
    }

    home_menu_load_selected_page();
}

static void beken_ui_start_dashcam_video(void)
{
    /* Dashcam camera/record/playback logic lives in dashcam_ui / dashcam_app
     * to keep this generated file thin (req5 #6). */
    dashcam_ui_enter();
}

static void beken_ui_stop_dashcam_video(void)
{
    dashcam_ui_leave();
}

/**
 * @brief Get the configured screen width
 * @return Screen width in pixels
 */
int beken_get_screen_width(void)
{
    return SCREEN_WIDTH;
}

/**
 * @brief Get the configured screen height
 * @return Screen height in pixels
 */
int beken_get_screen_height(void)
{
    return SCREEN_HEIGHT;
}

/*
 * Kick off continuous dashcam recording (req5 §9.1: record from power-on,
 * across pages). The dashcam logic lives in dashcam_ui / dashcam_app to keep
 * this generated file thin (req5 #6).
 *
 * The start is delayed by DASHCAM_BOOT_RECORD_DELAY_MS via a one-shot LVGL timer
 * (not lv_async, which would fire on the very next loop, concurrent with the
 * home speed-gauge's first redraw burst). Letting the home page render and free
 * its transient boot buffers first keeps the camera/ISP/H264 bring-up from
 * starving the AP SRAM heap and asserting in lv_draw_add_task (req5 §9.1 OOM).
 */
static void beken_ui_dashcam_boot_timer_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    dashcam_ui_boot_start();
}

static void beken_ui_schedule_dashcam_boot(void)
{
    lv_timer_t *timer = lv_timer_create(beken_ui_dashcam_boot_timer_cb,
                                        DASHCAM_BOOT_RECORD_DELAY_MS, NULL);
    if (timer == NULL)
    {
        /* Could not allocate a timer; start now rather than never recording. */
        dashcam_ui_boot_start();
    }
}

/**
 * @brief Initialize the Beken UI system
 */
void beken_ui_init(void)
{
    init_page_home(&bk_lv_tool_ui);
    home_ui_install_bg();
    lv_screen_load(bk_lv_tool_ui.home);
    s_home_menu_selected = HOME_MENU_DASHCAM;
    s_home_menu_active = HOME_MENU_HOME;
    s_home_menu_next_candidate = HOME_MENU_DASHCAM;
    s_home_menu_armed = true;
    home_menu_apply_selection();
    home_ui_register_bt_callbacks();

    /* Start the speed-gauge sweep and hazard double-flash. */
    home_ui_enter();

    /* Begin recording shortly after boot, independent of the visible page. */
    beken_ui_schedule_dashcam_boot();
}

/*
 * Pre-cast hook (strong override of the weak default in display_ui_cast_hooks.c).
 * Called with lv_vendor_disp_lock held and before lv_vendor_stop, so deleting the
 * timers here is safe and prevents them firing against a torn-down UI.
 */
void beken_ui_before_cast_lvgl_teardown(void)
{
    home_ui_leave();
    ota_ui_leave();
    /*
     * Keep the dashcam recorder + camera (MP flexa -> H264) running WHILE casting:
     * only leave the standby pages and pause the LVGL segment-rotation tick (the
     * LVGL task is about to stop, so that timer must not survive). The cast path
     * (network JPEG -> GPU -> DPU) uses different hardware than the recorder, so
     * recording keeps appending to the current segment in the background during
     * casting. Mirrors the assist-view teardown.
     */
    dashcam_ui_suspend_keep_recording();
}

/*
 * Pre-assist-view hook. Like beken_ui_before_cast_lvgl_teardown() it leaves the
 * standby pages and pauses the LVGL segment tick before lv_vendor_stop(), BUT
 * it does NOT stop the dashcam recorder/camera: the assist view keeps recording
 * (MP flexa -> H264) alive and adds a second MP flexa -> GPU bond for the
 * full-screen live view, so recording is uninterrupted while assisting.
 */
void beken_ui_before_assist_lvgl_teardown(void)
{
    home_ui_leave();
    ota_ui_leave();
    dashcam_ui_suspend_keep_recording();
}

/*
 * Glue required by the shared solution components.
 *
 * display_ui_cast_hooks.c calls beken_ui_kick_after_display_resume() to restore
 * the standby UI after a cast / display-resume cycle. Reload the home page.
 */
void beken_ui_kick_after_display_resume(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (ui->home != NULL && lv_obj_is_valid(ui->home))
    {
        lv_screen_load(ui->home);
        lv_obj_invalidate(ui->home);
    }
    else
    {
        init_page_home(ui);
        home_ui_install_bg();
        lv_screen_load(ui->home);
    }

    /* Timers were deleted in beken_ui_before_cast_lvgl_teardown; recreate them. */
    s_home_menu_selected = HOME_MENU_DASHCAM;
    s_home_menu_active = HOME_MENU_HOME;
    s_home_menu_next_candidate = HOME_MENU_DASHCAM;
    s_home_menu_armed = true;
    home_menu_apply_selection();
    home_ui_enter();

    /*
     * Recording is kept alive across casting now (suspend_keep_recording), so
     * normally we only need to re-arm the paused segment-rotation tick.
     * schedule_dashcam_boot() stays as an idempotent safety net: it is a no-op
     * unless the recorder is IDLE (e.g. a cast started before the initial boot),
     * in which case it (re)starts recording.
     */
    beken_ui_schedule_dashcam_boot();
    dashcam_ui_resume_keep_recording();
}

/* key_app_service.c calls pet_page_toggle() for the GPIO_50 short press. */
void pet_page_toggle(void)
{
    lv_async_call(home_menu_toggle_async_cb, NULL);
}

/* key_app_service.c calls pet_page_double() for the GPIO_50 double press. */
void pet_page_double(void)
{
    lv_async_call(home_menu_double_async_cb, NULL);
}

/* key_app_service.c calls pet_page_enter() for the GPIO_50 long press. */
void pet_page_enter(void)
{
    lv_async_call(home_menu_enter_async_cb, NULL);
}
