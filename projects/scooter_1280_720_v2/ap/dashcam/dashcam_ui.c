#include "dashcam_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "beken_ui.h"
#include "components/log.h"
#include "dashcam_app.h"
#include "dashcam_player.h"
#include "dashcam_storage.h"
#include "lvgl.h"
#include "dashcam_assitview.h"

#define TAG "d_ui"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DASHCAM_UI_MAX_ITEMS 10

/* Original design colors for the records list (see dashcam_init.c v2 design). */
#define DASHCAM_UI_LIST_BG          lv_color_hex(0x0A1520)
#define DASHCAM_UI_ITEM_TEXT        lv_color_hex(0xFFFFFF)
#define DASHCAM_UI_ACCENT           lv_color_hex(0x1EF2C4)

/* Playback info overlay refresh period (req6 #4). */
#define DASHCAM_UI_INFO_PERIOD_MS   500

static dashcam_file_info_t s_files[DASHCAM_UI_MAX_ITEMS];
static lv_obj_t *s_btns[DASHCAM_UI_MAX_ITEMS];
static uint32_t s_file_count = 0;
static bool s_preview_cb_bound = false;

/* req6 #3 list-navigation state. */
static bool s_list_focused = false;
static int32_t s_sel_index = 0;

/* req6 #4 playback info overlay. */
static lv_timer_t *s_info_timer = NULL;
static dashcam_player_info_t s_play_info;
static bool s_play_info_valid = false;

static void dashcam_ui_set_status(bk_lv_ui_t *ui)
{
    if (ui->dashcam_file_name != NULL && lv_obj_is_valid(ui->dashcam_file_name))
    {
        lv_label_set_text(ui->dashcam_file_name, dashcam_app_status_text());
    }
}

/* ---------- req6 #4: playback info overlay ---------- */

static void dashcam_ui_format_mmss(uint64_t ms, char *buf, size_t size)
{
    uint32_t total_s = (uint32_t)(ms / 1000u);
    snprintf(buf, size, "%02u:%02u",
             (unsigned)(total_s / 60u), (unsigned)(total_s % 60u));
}

/* Reset the overlay / progress widgets back to their live-preview look. */
static void dashcam_ui_reset_play_info(bk_lv_ui_t *ui)
{
    if (ui->dashcam_Live != NULL && lv_obj_is_valid(ui->dashcam_Live))
    {
        lv_label_set_text(ui->dashcam_Live, "LIVE");
    }
    if (ui->dashcam_ts_overlay != NULL && lv_obj_is_valid(ui->dashcam_ts_overlay))
    {
        lv_label_set_text(ui->dashcam_ts_overlay, "");
    }
    if (ui->dashcam_dur_start != NULL && lv_obj_is_valid(ui->dashcam_dur_start))
    {
        lv_label_set_text(ui->dashcam_dur_start, "00:00");
    }
    if (ui->dashcam_dur_end != NULL && lv_obj_is_valid(ui->dashcam_dur_end))
    {
        lv_label_set_text(ui->dashcam_dur_end, "00:00");
    }
    if (ui->dashcam_time_bar != NULL && lv_obj_is_valid(ui->dashcam_time_bar))
    {
        lv_bar_set_value(ui->dashcam_time_bar, 0, LV_ANIM_OFF);
    }
}

static void dashcam_ui_update_play_info(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    uint64_t position_ms;
    char start[16];
    char end[16];

    if (!dashcam_app_is_playing())
    {
        return;
    }

    /* Static info (resolution / duration) is only valid once the container has
     * been parsed; keep retrying until it lands. */
    if (!s_play_info_valid)
    {
        if (dashcam_player_get_media_info(&s_play_info) == BK_OK && s_play_info.width != 0)
        {
            s_play_info_valid = true;

            if (ui->dashcam_ts_overlay != NULL && lv_obj_is_valid(ui->dashcam_ts_overlay))
            {
                lv_label_set_text_fmt(ui->dashcam_ts_overlay, "%ux%u",
                                      (unsigned)s_play_info.width,
                                      (unsigned)s_play_info.height);
            }
            if (ui->dashcam_dur_end != NULL && lv_obj_is_valid(ui->dashcam_dur_end))
            {
                dashcam_ui_format_mmss(s_play_info.duration_ms, end, sizeof(end));
                lv_label_set_text(ui->dashcam_dur_end, end);
            }
            LOGI("play info %ux%u fps=%u dur=%llums\n",
                 (unsigned)s_play_info.width, (unsigned)s_play_info.height,
                 (unsigned)s_play_info.fps,
                 (unsigned long long)s_play_info.duration_ms);
        }
    }

    position_ms = dashcam_player_get_position_ms();

    if (ui->dashcam_dur_start != NULL && lv_obj_is_valid(ui->dashcam_dur_start))
    {
        dashcam_ui_format_mmss(position_ms, start, sizeof(start));
        lv_label_set_text(ui->dashcam_dur_start, start);
    }

    if (ui->dashcam_time_bar != NULL && lv_obj_is_valid(ui->dashcam_time_bar) &&
        s_play_info.duration_ms > 0)
    {
        int32_t pct = (int32_t)((position_ms * 100u) / s_play_info.duration_ms);
        if (pct > 100)
        {
            pct = 100;
        }
        lv_bar_set_value(ui->dashcam_time_bar, pct, LV_ANIM_OFF);
    }

    LOGD("playback pos=%llums\n", (unsigned long long)position_ms);
}

static void dashcam_ui_info_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    dashcam_ui_update_play_info();
}

static void dashcam_ui_start_info_timer(void)
{
    if (s_info_timer == NULL)
    {
        s_info_timer = lv_timer_create(dashcam_ui_info_timer_cb, DASHCAM_UI_INFO_PERIOD_MS, NULL);
        LOGD("info timer started\n");
    }
}

static void dashcam_ui_stop_info_timer(void)
{
    if (s_info_timer != NULL)
    {
        lv_timer_delete(s_info_timer);
        s_info_timer = NULL;
        LOGD("info timer stopped\n");
    }
}

/* ---------- req6 #3: list selection / focus ---------- */

static void dashcam_ui_apply_selection(void)
{
    uint32_t i;

    for (i = 0; i < s_file_count; i++)
    {
        lv_obj_t *btn = s_btns[i];

        if (btn == NULL || !lv_obj_is_valid(btn))
        {
            continue;
        }

        if (s_list_focused && (int32_t)i == s_sel_index)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
            lv_obj_scroll_to_view(btn, LV_ANIM_ON);
        }
        else
        {
            lv_obj_remove_state(btn, LV_STATE_CHECKED);
        }
    }
}

static void dashcam_ui_set_focus(bool focused)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    s_list_focused = focused;

    /* Visually mark the list panel as focused with a brighter accent border. */
    if (ui->dashcam_rec_list_panel != NULL && lv_obj_is_valid(ui->dashcam_rec_list_panel))
    {
        lv_obj_set_style_border_opa(ui->dashcam_rec_list_panel,
                                    focused ? 255 : 80,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (focused && s_file_count > 0)
    {
        if (s_sel_index < 0 || s_sel_index >= (int32_t)s_file_count)
        {
            s_sel_index = 0;
        }
    }

    dashcam_ui_apply_selection();
    LOGI("list focus=%d sel=%d count=%u\n",
         (int)focused, (int)s_sel_index, (unsigned)s_file_count);
}

/* ---------- list population (req2 colors) ---------- */

static void dashcam_ui_item_clicked_cb(lv_event_t *e)
{
    dashcam_file_info_t *info = (dashcam_file_info_t *)lv_event_get_user_data(e);

    if (info == NULL)
    {
        return;
    }

    LOGI("item clicked: %s\n", info->path);

    if (dashcam_app_play(info->path) == BK_OK)
    {
        s_play_info_valid = false;
        dashcam_ui_set_status(&bk_lv_tool_ui);
        if (bk_lv_tool_ui.dashcam_Live != NULL && lv_obj_is_valid(bk_lv_tool_ui.dashcam_Live))
        {
            lv_label_set_text(bk_lv_tool_ui.dashcam_Live, "PLAY");
        }
        dashcam_ui_start_info_timer();
    }
}

static void dashcam_ui_preview_clicked_cb(lv_event_t *e)
{
    (void)e;

    if (dashcam_app_is_playing())
    {
        LOGI("preview tap -> stop playback\n");
        dashcam_ui_stop_info_timer();
        dashcam_ui_reset_play_info(&bk_lv_tool_ui);
        dashcam_app_stop_playback();
        dashcam_ui_set_status(&bk_lv_tool_ui);
    }
}

/* Apply the original design colors to a dynamically created list button. */
static void dashcam_ui_style_item(lv_obj_t *btn)
{
    if (btn == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_opa(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, DASHCAM_UI_ITEM_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Selected/checked item highlight matches the designer accent color. */
    lv_obj_set_style_bg_color(btn, DASHCAM_UI_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn, 53, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, DASHCAM_UI_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
}

static void dashcam_ui_populate_list(bk_lv_ui_t *ui)
{
    lv_obj_t *list = ui->dashcam_rec_list;
    uint32_t i;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    /*
     * Drop the designer placeholder items and rebuild from the SD card. The
     * placeholder item_0..9 handles in bk_lv_ui_t become stale here, but they
     * are only used by the generated init and the page is recreated wholesale
     * on next entry, so they are never dereferenced after this point.
     */
    lv_obj_clean(list);
    memset(s_btns, 0, sizeof(s_btns));

    /* Keep the list background on the original (dark) design color (req6 #2). */
    lv_obj_set_style_bg_color(list, DASHCAM_UI_LIST_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(list, DASHCAM_UI_ITEM_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_file_count = 0;
    if (dashcam_storage_scan(s_files, DASHCAM_UI_MAX_ITEMS, &s_file_count) != BK_OK)
    {
        LOGW("scan records failed\n");
    }

    /*
     * The scan sorts clips newest-first (descending by name), so s_files[0] is
     * the segment the recorder is currently writing to. While recording is
     * active, hide that newest clip from the playable list: opening the MP4 the
     * muxer is still appending to would race record-write vs playback-read on
     * the same file (and the moov/index is not finalized yet). Drop index 0 by
     * shifting the rest down so the whole UI (list, key-nav, selection) treats
     * only the finalized clips as playable.
     */
    if (dashcam_app_rec_state() == DASHCAM_REC_RECORDING && s_file_count > 0)
    {
        uint32_t k;

        LOGI("skip newest (recording) clip: %s\n", s_files[0].name);
        for (k = 1; k < s_file_count; k++)
        {
            s_files[k - 1] = s_files[k];
        }
        s_file_count--;
    }

    if (s_file_count == 0)
    {
        lv_obj_t *txt = lv_list_add_text(list, "No records");
        if (txt != NULL)
        {
            lv_obj_set_style_text_color(txt, DASHCAM_UI_ITEM_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        s_sel_index = 0;
        LOGI("records list empty\n");
        return;
    }

    for (i = 0; i < s_file_count; i++)
    {
        char label[48];
        lv_obj_t *btn;

        dashcam_storage_format_label(&s_files[i], label, sizeof(label));
        btn = lv_list_add_button(list, &play_btn_trans_ARGB8888, label);
        if (btn != NULL)
        {
            dashcam_ui_style_item(btn);
            lv_obj_add_event_cb(btn, dashcam_ui_item_clicked_cb, LV_EVENT_CLICKED, &s_files[i]);
            s_btns[i] = btn;
        }
    }

    if (s_sel_index >= (int32_t)s_file_count)
    {
        s_sel_index = 0;
    }

    LOGI("records list populated: %u\n", (unsigned)s_file_count);
}

void dashcam_ui_boot_start(void)
{
    LOGD("boot_start\n");
    dashcam_app_boot_start();
    dashcam_assitview_init();
}

void dashcam_ui_shutdown(void)
{
    LOGD("shutdown\n");
    dashcam_ui_stop_info_timer();
    s_preview_cb_bound = false;
    s_list_focused = false;
    s_play_info_valid = false;
    memset(s_btns, 0, sizeof(s_btns));
    dashcam_app_shutdown();
}

/*
 * Assist-view / cast enter: tear down the dashcam LVGL page state (UI timers,
 * widget refs) like shutdown, but DO NOT call dashcam_app_shutdown() -> the
 * recorder + camera (mode=3, MP->H264) must keep running so the assist view can
 * show the same MP output through a second GPU bond.
 *
 * Playback, however, is a pure LVGL-domain path (canvas + refresh lv_timer +
 * display event cb + player thread) that cannot survive lv_vendor_stop(): its
 * lv_timer would revive on resume against a stale canvas, the player thread
 * would spin with no consumer, and its UNCODED/HSRAM buffers would starve the
 * assist GPU bond allocation. So stop playback here (player + video sink) the
 * same way detach() does, while leaving the recorder/camera untouched.
 */
void dashcam_ui_suspend_keep_recording(void)
{
    LOGD("suspend (keep recording)\n");
    dashcam_ui_stop_info_timer();
    s_preview_cb_bound = false;
    s_list_focused = false;
    s_play_info_valid = false;
    memset(s_btns, 0, sizeof(s_btns));

    /* Drop the playback path only; recorder + camera stay up (keep recording). */
    if (dashcam_app_is_playing())
    {
        dashcam_app_stop_playback();
    }

    dashcam_app_pause_segment_tick();
}

/* Assist-view leave: LVGL is back, re-arm the paused segment-rotation tick. */
void dashcam_ui_resume_keep_recording(void)
{
    LOGD("resume (keep recording)\n");
    dashcam_app_resume_segment_tick();
}

void dashcam_ui_enter(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    LOGD("enter\n");

    if (ui->dashcam == NULL || !lv_obj_is_valid(ui->dashcam) ||
        ui->dashcam_sky_area == NULL || !lv_obj_is_valid(ui->dashcam_sky_area))
    {
        LOGW("dashcam page not valid, skip enter\n");
        return;
    }

    s_list_focused = false;
    s_sel_index = 0;
    s_play_info_valid = false;

    dashcam_app_attach(ui->dashcam_sky_area);
    dashcam_ui_populate_list(ui);
    dashcam_ui_reset_play_info(ui);

    if (!s_preview_cb_bound &&
        ui->dashcam_front_vid_panel != NULL && lv_obj_is_valid(ui->dashcam_front_vid_panel))
    {
        lv_obj_add_event_cb(ui->dashcam_front_vid_panel,
                            dashcam_ui_preview_clicked_cb,
                            LV_EVENT_CLICKED,
                            NULL);
        s_preview_cb_bound = true;
    }

    dashcam_ui_set_status(ui);
}

void dashcam_ui_leave(void)
{
    LOGD("leave\n");

    /* The page (and its widgets) may be freed after this; the event callback
     * is destroyed with the panel, so just drop our bound flag. */
    dashcam_ui_stop_info_timer();
    s_preview_cb_bound = false;
    s_list_focused = false;
    s_play_info_valid = false;
    memset(s_btns, 0, sizeof(s_btns));
    dashcam_app_detach();
}

/* ---------- req6 #3: physical-key handlers ---------- */

static bool dashcam_ui_is_active(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    return ui->dashcam != NULL && lv_obj_is_valid(ui->dashcam) &&
           ui->dashcam == lv_screen_active();
}

bool dashcam_ui_handle_key_double(void)
{
    if (!dashcam_ui_is_active())
    {
        return false;
    }

    LOGI("key double, focused=%d\n", (int)s_list_focused);

    if (s_list_focused)
    {
        /* Leaving the list: if a clip is playing, stop playback. */
        if (dashcam_app_is_playing())
        {
            dashcam_ui_stop_info_timer();
            dashcam_ui_reset_play_info(&bk_lv_tool_ui);
            dashcam_app_stop_playback();
            dashcam_ui_set_status(&bk_lv_tool_ui);
        }
        dashcam_ui_set_focus(false);
    }
    else
    {
        dashcam_ui_set_focus(true);
    }

    return true;
}

bool dashcam_ui_handle_key_single(void)
{
    if (!dashcam_ui_is_active() || !s_list_focused)
    {
        return false;
    }

    if (s_file_count == 0)
    {
        LOGI("key single ignored, list empty\n");
        return true;
    }

    s_sel_index = (s_sel_index + 1) % (int32_t)s_file_count;
    dashcam_ui_apply_selection();
    LOGI("key single -> sel=%d\n", (int)s_sel_index);
    return true;
}

bool dashcam_ui_handle_key_long(void)
{
    if (!dashcam_ui_is_active() || !s_list_focused)
    {
        return false;
    }

    if (s_file_count == 0 || s_sel_index < 0 || s_sel_index >= (int32_t)s_file_count)
    {
        LOGI("key long ignored, no selection\n");
        return true;
    }

    LOGI("key long -> play sel=%d %s\n", (int)s_sel_index, s_files[s_sel_index].path);

    if (dashcam_app_play(s_files[s_sel_index].path) == BK_OK)
    {
        s_play_info_valid = false;
        dashcam_ui_set_status(&bk_lv_tool_ui);
        if (bk_lv_tool_ui.dashcam_Live != NULL && lv_obj_is_valid(bk_lv_tool_ui.dashcam_Live))
        {
            lv_label_set_text(bk_lv_tool_ui.dashcam_Live, "PLAY");
        }
        dashcam_ui_start_info_timer();
    }

    return true;
}
