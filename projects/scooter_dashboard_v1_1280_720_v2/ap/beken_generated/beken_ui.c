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
#include <stddef.h>
#include <stdint.h>
#include "components/log.h"
#include <os/os.h>
#include "lv_vendor.h"
#include "lv_port_indev.h"
#include "boot_bg_preload.h"
#include "dashcam_ui.h"
#include "dashcam_config.h"
#include "dashcam_assitview.h"
#include "ota_ui.h"
#include "home_ui.h"
#include "phone_book_ui.h"
#include "music_player_ui.h"

bk_lv_ui_t bk_lv_tool_ui = {0};
static lv_timer_t *s_dashcam_boot_timer = NULL;
static const lv_image_dsc_t *s_preloaded_bg = NULL;
static bool s_preloaded_bg_waited = false;

/*
 * Wrap the background bitmap preloaded during the boot animation
 * (boot_bg_preload) onto a page's background image object, with no decoding on
 * the UI-init path. Shared by all pages so they reuse the single decoded
 * bitmap. Returns true when the preloaded bitmap was installed.
 */
bool beken_ui_install_preloaded_bg(lv_obj_t *bg_img)
{
    const lv_image_dsc_t *bg;

    if (bg_img == NULL || !lv_obj_is_valid(bg_img))
    {
        return false;
    }

    if (s_preloaded_bg == NULL)
    {
        /*
         * Only the first UI installation may wait for the boot worker. If that
         * wait times out, later page transitions probe without blocking and
         * adopt the bitmap as soon as the worker eventually completes.
         */
        s_preloaded_bg = boot_bg_preload_get(s_preloaded_bg_waited ? 0 : 3000);
        s_preloaded_bg_waited = true;
    }

    bg = s_preloaded_bg;
    if (bg == NULL)
    {
        return false;
    }

    lv_image_set_src(bg_img, bg);
    BK_LOGI("ui_bg", "using preloaded RGB565 bitmap (%dx%d)\n",
            (int)bg->header.w, (int)bg->header.h);
    return true;
}

typedef enum
{
    HOME_MENU_HOME = 0,
    HOME_MENU_DASHCAM = HOME_UI_NAV_DASHCAM,
    HOME_MENU_OTA = HOME_UI_NAV_OTA,
    HOME_MENU_PHONE_BOOK = HOME_UI_NAV_PHONE_BOOK,
    HOME_MENU_MUSIC_PLAYER = HOME_UI_NAV_MUSIC_PLAYER,
    HOME_MENU_COUNT,
} home_menu_item_t;

static int32_t s_home_menu_selected = HOME_MENU_HOME;
static int32_t s_home_menu_active = HOME_MENU_HOME;
static bool s_home_menu_armed = false;

typedef struct
{
    lv_group_t *page;
    lv_group_t *modal;
} ui_keypad_nav_t;

typedef void (*ui_page_create_cb_t)(bk_lv_ui_t *ui);
typedef void (*ui_page_lifecycle_cb_t)(void);
typedef lv_group_t *(*ui_page_group_cb_t)(void);

typedef struct
{
    size_t root_offset;
    ui_page_create_cb_t create;
    ui_page_lifecycle_cb_t enter;
    ui_page_lifecycle_cb_t leave;
    ui_page_lifecycle_cb_t unload;
    ui_page_group_cb_t get_group;
} ui_page_descriptor_t;

/*
 * The active page owns the normal navigation group. A non-empty modal group
 * temporarily overrides it (currently the incoming/active-call controls).
 */
static ui_keypad_nav_t s_keypad_nav = {0};

static void beken_ui_start_dashcam_video(void);
static void beken_ui_stop_dashcam_video(void);
static void home_menu_return_home(void);
static void beken_ui_log_heap(const char *tag);
static void home_menu_load_selected_page(void);
static void home_menu_focus_changed(int32_t item);
static void home_menu_activate(int32_t item);
static void ui_keypad_apply_binding(void);
static void ui_keypad_activate_page(int32_t active_page);
static void beken_ui_create_home(bk_lv_ui_t *ui);
static void beken_ui_create_dashcam(bk_lv_ui_t *ui);
static void beken_ui_create_ota(bk_lv_ui_t *ui);
static void beken_ui_create_phone_book(bk_lv_ui_t *ui);
static void beken_ui_create_music_player(bk_lv_ui_t *ui);

/*
 * One descriptor owns every page-specific operation. Adding a page should only
 * require one enum value and one row here; transition code stays unchanged.
 */
static const ui_page_descriptor_t s_ui_pages[HOME_MENU_COUNT] = {
    [HOME_MENU_HOME] = {
        offsetof(bk_lv_ui_t, home),
        beken_ui_create_home,
        home_ui_enter,
        home_ui_leave,
        home_ui_unload,
        home_ui_get_group,
    },
    [HOME_MENU_DASHCAM] = {
        offsetof(bk_lv_ui_t, dashcam),
        beken_ui_create_dashcam,
        beken_ui_start_dashcam_video,
        beken_ui_stop_dashcam_video,
        NULL,
        dashcam_ui_get_group,
    },
    [HOME_MENU_OTA] = {
        offsetof(bk_lv_ui_t, ota_update),
        beken_ui_create_ota,
        ota_ui_enter,
        ota_ui_leave,
        NULL,
        NULL,
    },
    [HOME_MENU_PHONE_BOOK] = {
        offsetof(bk_lv_ui_t, phone_book),
        beken_ui_create_phone_book,
        phone_book_ui_enter,
        phone_book_ui_leave,
        NULL,
        phone_book_ui_get_group,
    },
    [HOME_MENU_MUSIC_PLAYER] = {
        offsetof(bk_lv_ui_t, music_player),
        beken_ui_create_music_player,
        music_player_ui_enter,
        music_player_ui_leave,
        NULL,
        music_player_ui_get_group,
    },
};

static void home_menu_focus_changed(int32_t item)
{
    s_home_menu_selected = item;
    s_home_menu_armed = true;
}

static void home_menu_activate(int32_t item)
{
    s_home_menu_selected = item;
    home_menu_load_selected_page();
}

static bool ui_keypad_group_available(lv_group_t *group)
{
    return group != NULL && lv_group_get_obj_count(group) > 0;
}

static lv_group_t *ui_keypad_group_for_page(int32_t active_page)
{
    const ui_page_descriptor_t *page;

    if (active_page < HOME_MENU_HOME || active_page >= HOME_MENU_COUNT)
    {
        return NULL;
    }

    page = &s_ui_pages[active_page];
    return page->get_group != NULL ? page->get_group() : NULL;
}

/*
 * Bind the KEYPAD to the effective owner. A non-empty modal group has priority;
 * otherwise the active page group owns the keys. Group-less pages such as OTA
 * explicitly detach the indev. lv_port_keypad_set_group(NULL) is not used
 * because the port interprets NULL as "restore the default group".
 */
static void ui_keypad_apply_binding(void)
{
    lv_indev_t *keypad = lv_port_keypad_get_indev();
    lv_group_t *target;

    if (keypad == NULL)
    {
        return;
    }

    target = ui_keypad_group_available(s_keypad_nav.modal)
                 ? s_keypad_nav.modal
                 : s_keypad_nav.page;

    if (lv_indev_get_group(keypad) == target)
    {
        return;
    }

    if (target != NULL)
    {
        lv_port_keypad_set_group(target);
    }
    else
    {
        lv_indev_set_group(keypad, NULL);
    }
}

/*
 * Update the normal keypad owner after a page transition.
 */
static void ui_keypad_activate_page(int32_t active_page)
{
    s_keypad_nav.page = ui_keypad_group_for_page(active_page);
    ui_keypad_apply_binding();
}

/*
 * Install or clear a temporary modal keypad owner. Empty modal groups do not
 * override the page group, which protects page navigation when modal widgets
 * are destroyed together with the HOME object tree.
 */
void beken_ui_keypad_set_modal_group(lv_group_t *group)
{
    s_keypad_nav.modal = group;
    ui_keypad_apply_binding();
}

static void home_menu_select(home_menu_item_t item)
{
    s_home_menu_selected = item;
    s_home_menu_armed = (item != HOME_MENU_HOME);
    home_ui_nav_focus(item);
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
static const ui_page_descriptor_t *beken_ui_page_descriptor(int32_t page)
{
    if (page < HOME_MENU_HOME || page >= HOME_MENU_COUNT)
    {
        return NULL;
    }

    return &s_ui_pages[page];
}

static lv_obj_t **beken_ui_page_root_slot(bk_lv_ui_t *ui, int32_t page)
{
    const ui_page_descriptor_t *descriptor = beken_ui_page_descriptor(page);

    if (ui == NULL || descriptor == NULL)
    {
        return NULL;
    }

    return (lv_obj_t **)((uint8_t *)ui + descriptor->root_offset);
}

static void beken_ui_create_home(bk_lv_ui_t *ui)
{
    init_page_home(ui);
    home_ui_install_bg();
}

static void beken_ui_create_dashcam(bk_lv_ui_t *ui)
{
    init_page_dashcam(ui);
    beken_ui_install_preloaded_bg(ui->dashcam_bg_img);
}

static void beken_ui_create_ota(bk_lv_ui_t *ui)
{
    init_page_ota_update(ui);
    beken_ui_install_preloaded_bg(ui->ota_update_bg_img);
}

static void beken_ui_create_phone_book(bk_lv_ui_t *ui)
{
    init_page_phone_book(ui);
}

static void beken_ui_create_music_player(bk_lv_ui_t *ui)
{
    init_page_music_player(ui);
    beken_ui_install_preloaded_bg(ui->music_player_bg_img);
}

static lv_obj_t *beken_ui_ensure_page(bk_lv_ui_t *ui, int32_t page)
{
    const ui_page_descriptor_t *descriptor = beken_ui_page_descriptor(page);
    lv_obj_t **slot = beken_ui_page_root_slot(ui, page);

    if (descriptor == NULL || slot == NULL)
    {
        return NULL;
    }

    if (*slot != NULL && !lv_obj_is_valid(*slot))
    {
        *slot = NULL;
    }

    if (*slot == NULL && descriptor->create != NULL)
    {
        descriptor->create(ui);
    }

    if (*slot == NULL || !lv_obj_is_valid(*slot))
    {
        *slot = NULL;
        return NULL;
    }

    return *slot;
}

static void beken_ui_page_enter(int32_t page)
{
    const ui_page_descriptor_t *descriptor = beken_ui_page_descriptor(page);

    if (descriptor != NULL && descriptor->enter != NULL)
    {
        descriptor->enter();
    }
}

static void beken_ui_page_leave(int32_t page)
{
    const ui_page_descriptor_t *descriptor = beken_ui_page_descriptor(page);

    if (descriptor != NULL && descriptor->leave != NULL)
    {
        descriptor->leave();
    }
}

static void beken_ui_free_heavy_page(bk_lv_ui_t *ui, int32_t page)
{
    const ui_page_descriptor_t *descriptor = beken_ui_page_descriptor(page);
    lv_obj_t **slot = beken_ui_page_root_slot(ui, page);
    lv_obj_t *victim = NULL;

    if (descriptor == NULL || slot == NULL || *slot == NULL)
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

    if (descriptor->unload != NULL)
    {
        descriptor->unload();
    }

    lv_obj_delete(victim);
    *slot = NULL;
}

static void home_menu_free_inactive_heavy_pages(int32_t active_page)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    int32_t page;

    for (page = HOME_MENU_HOME; page < HOME_MENU_COUNT; page++)
    {
        if (page != active_page)
        {
            beken_ui_free_heavy_page(ui, page);
        }
    }
}

static bool home_menu_switch_page(int32_t target_page,
                                  home_menu_item_t selected_after,
                                  bool armed_after)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    int32_t old_page = s_home_menu_active;
    lv_obj_t *target;
    bool switching_page = old_page != target_page;

    if (switching_page)
    {
        beken_ui_page_leave(old_page);
    }

    target = beken_ui_ensure_page(ui, target_page);
    if (target == NULL)
    {
        if (switching_page)
        {
            beken_ui_page_enter(old_page);
        }
        return false;
    }

    lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    home_menu_free_inactive_heavy_pages(target_page);
    beken_ui_log_heap("page-load");

    /* Removing the HOME objects can trigger intermediate focus callbacks while
     * the navigation group is being emptied. Restore the requested selection. */
    s_home_menu_selected = selected_after;
    s_home_menu_active = target_page;
    s_home_menu_armed = armed_after;

    if (s_home_menu_active == HOME_MENU_HOME)
    {
        home_ui_nav_group_build(s_home_menu_selected,
                                home_menu_focus_changed,
                                home_menu_activate);
    }

    beken_ui_page_enter(s_home_menu_active);
    ui_keypad_activate_page(s_home_menu_active);
    return true;
}

static void home_menu_load_selected_page(void)
{
    home_menu_item_t target = (home_menu_item_t)s_home_menu_selected;

    home_menu_switch_page(target, target, false);
}

static void home_menu_open(home_menu_item_t item)
{
    home_menu_select(item);
    home_menu_load_selected_page();
}

static void home_menu_return_home(void)
{
    int32_t old_page = s_home_menu_active;
    home_menu_item_t selected;

    if (old_page == HOME_MENU_HOME)
    {
        return;
    }

    selected = old_page > HOME_MENU_HOME && old_page < HOME_MENU_COUNT
                   ? (home_menu_item_t)old_page
                   : HOME_MENU_DASHCAM;
    home_menu_switch_page(HOME_MENU_HOME, selected, true);
}

static void home_menu_select_delta(int32_t delta)
{
    int32_t item = s_home_menu_selected + delta;

    if (item <= HOME_MENU_HOME)
    {
        item = HOME_MENU_MUSIC_PLAYER;
    }
    else if (item >= HOME_MENU_COUNT)
    {
        item = HOME_MENU_DASHCAM;
    }

    /*
     * Keep the state machine and LVGL group focus synchronized so ENTER always
     * activates the item that is visibly selected.
     */
    home_menu_select((home_menu_item_t)item);
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

static void home_menu_open_async_cb(void *user_data)
{
    home_menu_open((home_menu_item_t)(uintptr_t)user_data);
}

static void beken_ui_nav_to_page(home_menu_item_t page)
{
    lv_async_call(home_menu_open_async_cb, (void *)(uintptr_t)page);
}

void beken_ui_nav_to_dashcam(void)
{
    beken_ui_nav_to_page(HOME_MENU_DASHCAM);
}

void beken_ui_nav_to_ota_update(void)
{
    beken_ui_nav_to_page(HOME_MENU_OTA);
}

void beken_ui_nav_to_phone_book(void)
{
    beken_ui_nav_to_page(HOME_MENU_PHONE_BOOK);
}

void beken_ui_nav_to_music_player(void)
{
    beken_ui_nav_to_page(HOME_MENU_MUSIC_PLAYER);
}

void beken_ui_nav_home(void)
{
    beken_ui_nav_to_page(HOME_MENU_HOME);
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

/* Runs in the LVGL task: return to the home page (page-mutual-exclusion path).
 * Wired to the physical HOME key (UP). No-op when home is already active. */
static void home_menu_return_home_async_cb(void *user_data)
{
    (void)user_data;

    if (s_home_menu_active == HOME_MENU_HOME)
    {
        return;
    }

    home_menu_return_home();
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

bool beken_ui_is_home_active(void)
{
    return s_home_menu_active == HOME_MENU_HOME &&
           !dashcam_assitview_is_active();
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
    if (s_dashcam_boot_timer == timer)
    {
        s_dashcam_boot_timer = NULL;
    }
    lv_timer_delete(timer);
    dashcam_ui_boot_start();
}

static void beken_ui_schedule_dashcam_boot(void)
{
    if (s_dashcam_boot_timer != NULL)
    {
        return;
    }

    s_dashcam_boot_timer = lv_timer_create(beken_ui_dashcam_boot_timer_cb,
                                           DASHCAM_BOOT_RECORD_DELAY_MS, NULL);
    if (s_dashcam_boot_timer == NULL)
    {
        /* Could not allocate a timer; start now rather than never recording. */
        dashcam_ui_boot_start();
    }
}

static void beken_ui_cancel_dashcam_boot(void)
{
    if (s_dashcam_boot_timer != NULL)
    {
        lv_timer_delete(s_dashcam_boot_timer);
        s_dashcam_boot_timer = NULL;
    }
}

/**
 * @brief Initialize the Beken UI system
 */
void beken_ui_init(void)
{
    lv_obj_t *home = beken_ui_ensure_page(&bk_lv_tool_ui, HOME_MENU_HOME);

    if (home == NULL)
    {
        return;
    }

    lv_screen_load(home);
    s_home_menu_selected = HOME_MENU_DASHCAM;
    s_home_menu_active = HOME_MENU_HOME;
    s_home_menu_armed = true;
    home_ui_nav_group_build(s_home_menu_selected,
                            home_menu_focus_changed,
                            home_menu_activate);
    ui_keypad_activate_page(HOME_MENU_HOME);
    home_ui_register_bt_callbacks();

    /* Start the speed-gauge sweep and hazard double-flash. */
    beken_ui_page_enter(HOME_MENU_HOME);

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
    beken_ui_cancel_dashcam_boot();
    beken_ui_page_leave(HOME_MENU_HOME);
    beken_ui_page_leave(HOME_MENU_OTA);
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
    beken_ui_cancel_dashcam_boot();
    beken_ui_page_leave(HOME_MENU_HOME);
    beken_ui_page_leave(HOME_MENU_OTA);
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
    lv_obj_t *home = beken_ui_ensure_page(ui, HOME_MENU_HOME);

    if (home == NULL)
    {
        return;
    }

    lv_screen_load(home);
    lv_obj_invalidate(home);

    /* Timers were deleted in beken_ui_before_cast_lvgl_teardown; recreate them. */
    s_home_menu_selected = HOME_MENU_DASHCAM;
    s_home_menu_active = HOME_MENU_HOME;
    s_home_menu_armed = true;
    home_ui_nav_group_build(s_home_menu_selected,
                            home_menu_focus_changed,
                            home_menu_activate);
    ui_keypad_activate_page(HOME_MENU_HOME);
    beken_ui_page_enter(HOME_MENU_HOME);

    /*
     * Assist View never stops recording or its RTOS rotation tick, so restoring
     * HOME must not schedule or resume the recorder again. Keep the safety path
     * for other display-resume users such as casting.
     */
    if (!dashcam_assitview_is_active())
    {
        beken_ui_schedule_dashcam_boot();
        dashcam_ui_resume_keep_recording();
    }
}

/*
 * Middle key = ENTER (confirm). Send one keypad ENTER so LVGL fires the focused
 * object's handler in whatever group currently owns the keys: on HOME it enters
 * the focused menu entry (home_menu_entry_enter_cb), on dashcam it plays the
 * focused clip, on phone_book it dials the focused row, and on the call modal it
 * answers/hangs up. Group-less pages (OTA) simply ignore it.
 */
void beken_ui_key_enter(void)
{
    if (dashcam_assitview_is_active())
    {
        return;
    }

    lv_port_keypad_send_key(LV_KEY_ENTER);
}

void beken_ui_key_open_assist_view(void)
{
    if (s_home_menu_active != HOME_MENU_HOME)
    {
        return;
    }

    dashcam_assitview_start();
}

/* Return HOME is a page-global command, independent of focused objects. */
void beken_ui_key_home(void)
{
    if (dashcam_assitview_is_active())
    {
        dashcam_assitview_stop();
        return;
    }

    /* While a dashcam clip is playing, a MIDDLE double-press stops playback and
     * returns to the dashcam list page instead of jumping to HOME. */
    if (dashcam_ui_handle_key_home())
    {
        return;
    }

    lv_async_call(home_menu_return_home_async_cb, NULL);
}

static void beken_ui_send_direction_key(uint32_t key)
{
    if (dashcam_assitview_is_active())
    {
        return;
    }

    lv_port_keypad_send_key(key);
}

void beken_ui_key_up(void)
{
    beken_ui_send_direction_key(LV_KEY_UP);
}

void beken_ui_key_down(void)
{
    beken_ui_send_direction_key(LV_KEY_DOWN);
}

/* On the phone_book and music_player pages LEFT/RIGHT switch between the two
 * navigable zones (contacts/recents, now-playing/playlist); elsewhere they cycle
 * the focus (PREV/NEXT) through the active group. */
static bool beken_ui_page_uses_lr_zones(void)
{
    return s_home_menu_active == HOME_MENU_PHONE_BOOK ||
           s_home_menu_active == HOME_MENU_MUSIC_PLAYER;
}

void beken_ui_key_left(void)
{
    beken_ui_send_direction_key(beken_ui_page_uses_lr_zones()
                                    ? LV_KEY_LEFT
                                    : LV_KEY_PREV);
}

void beken_ui_key_right(void)
{
    beken_ui_send_direction_key(beken_ui_page_uses_lr_zones()
                                    ? LV_KEY_RIGHT
                                    : LV_KEY_NEXT);
}
