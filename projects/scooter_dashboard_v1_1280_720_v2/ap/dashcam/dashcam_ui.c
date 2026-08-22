#include "dashcam_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "beken_ui.h"
#include "components/log.h"
#include "dashcam_app.h"
#include "dashcam_player.h"
#include "dashcam_recorder.h"
#include "dashcam_storage.h"
#include "lvgl.h"
#include "lv_port_indev.h"
#include "dashcam_assitview.h"
#include "os/mem.h"
#include "os/os.h"

extern void beken_ui_before_assist_lvgl_teardown(void);
extern void beken_ui_kick_after_display_resume(void);

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
#define DASHCAM_UI_LOAD_PRIO        4
#define DASHCAM_UI_LOAD_STACK       4096

static dashcam_file_info_t s_files[DASHCAM_UI_MAX_ITEMS];
static lv_obj_t *s_btns[DASHCAM_UI_MAX_ITEMS];
static uint32_t s_file_count = 0;
static bool s_preview_cb_bound = false;

typedef struct
{
    uint32_t generation;
    uint32_t file_count;
    bk_err_t scan_result;
    dashcam_file_info_t files[DASHCAM_UI_MAX_ITEMS];
} dashcam_ui_load_result_t;

static beken_thread_t s_load_thread = NULL;
static beken_mutex_t s_load_state_mutex = NULL;
static bool s_load_worker_running = false;
static bool s_page_active = false;
static uint32_t s_load_generation = 0;

static bk_err_t dashcam_ui_load_state_init(void)
{
    if (s_load_state_mutex != NULL)
    {
        return BK_OK;
    }

    return rtos_init_mutex(&s_load_state_mutex);
}

static void dashcam_ui_load_state_lock(void)
{
    if (rtos_lock_mutex(&s_load_state_mutex) != BK_OK)
    {
        LOGE("lock load state mutex failed\n");
    }
}

static void dashcam_ui_load_state_unlock(void)
{
    if (rtos_unlock_mutex(&s_load_state_mutex) != BK_OK)
    {
        LOGE("unlock load state mutex failed\n");
    }
}

/* req6 #3 list-navigation state. */
static bool s_list_focused = false;
static int32_t s_sel_index = 0;

/*
 * Method-A list navigation: the records list items live in a persistent LVGL
 * group driven by the shared KEYPAD indev (lv_port_keypad_*). While the
 * dashcam page is active beken_ui binds the indev to this group, so the same
 * physical LEFT/RIGHT keys that move the home menu now move the list selection,
 * and ENTER (routed to the focused item) plays it via its existing CLICKED cb.
 * The group is created once; its member items are rebuilt on every populate.
 */
static lv_group_t *s_dashcam_group = NULL;

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

/* ---------- Method-A: records list group ---------- */

static void dashcam_ui_group_focus_cb(lv_group_t *group)
{
    lv_obj_t *focused = lv_group_get_focused(group);
    uint32_t i;

    if (focused == NULL)
    {
        return;
    }

    for (i = 0; i < s_file_count; i++)
    {
        if (s_btns[i] == focused)
        {
            s_sel_index = (int32_t)i;
            s_list_focused = true;
            dashcam_ui_apply_selection();
            return;
        }
    }
}

static void dashcam_ui_item_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if (s_dashcam_group == NULL)
    {
        return;
    }

    if (key == LV_KEY_UP)
    {
        lv_group_focus_prev(s_dashcam_group);
    }
    else if (key == LV_KEY_DOWN)
    {
        lv_group_focus_next(s_dashcam_group);
    }
}

static void dashcam_ui_group_ensure(void)
{
    if (s_dashcam_group == NULL)
    {
        s_dashcam_group = lv_group_create();
        if (s_dashcam_group != NULL)
        {
            lv_group_set_wrap(s_dashcam_group, true);
            lv_group_set_focus_cb(s_dashcam_group, dashcam_ui_group_focus_cb);
        }
    }
}

lv_group_t *dashcam_ui_get_group(void)
{
    dashcam_ui_group_ensure();
    return s_dashcam_group;
}

/* ---------- list population (req2 colors) ---------- */

/*
 * Guard playback so a half-written clip never reaches the player. Two cases are
 * rejected: (1) the clip that is still being recorded (its moov/index is not
 * finalized), and (2) any .mp4 whose container fails a quick sanity check. On
 * 1280 the dashcam page stops recording on enter, so (1) is usually moot, but it
 * is kept for parity with 1024 and to stay safe if that ordering ever changes.
 */
static bool dashcam_ui_clip_is_playable(const dashcam_file_info_t *info)
{
    const char *recording_path;
    size_t path_len;

    if (info == NULL || info->path[0] == '\0')
    {
        return false;
    }

    recording_path = dashcam_recorder_current_path();
    if (dashcam_recorder_is_running() &&
        recording_path != NULL &&
        strcmp(info->path, recording_path) == 0)
    {
        LOGW("reject active recording clip: %s\n", info->path);
        return false;
    }

    path_len = strlen(info->path);
    if (path_len >= 4U &&
        strcasecmp(info->path + path_len - 4U, ".mp4") == 0 &&
        !dashcam_storage_mp4_is_playable(info->path))
    {
        LOGW("reject invalid MP4 clip: %s\n", info->path);
        return false;
    }

    return true;
}

static void dashcam_ui_item_clicked_cb(lv_event_t *e)
{
    dashcam_file_info_t *info = (dashcam_file_info_t *)lv_event_get_user_data(e);

    if (info == NULL)
    {
        return;
    }

    LOGI("item clicked: %s\n", info->path);

    if (!dashcam_ui_clip_is_playable(info))
    {
        return;
    }

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

static void dashcam_ui_add_item_labels(lv_obj_t *btn,
                                       const char *title_text,
                                       const dashcam_file_info_t *info)
{
    lv_obj_t *title;
    lv_obj_t *text_column;
    lv_obj_t *size_label;
    char size_text[24];
    uint32_t size_mb;
    uint32_t size_mb_tenth;

    if (btn == NULL || title_text == NULL || info == NULL)
    {
        return;
    }

    text_column = lv_obj_create(btn);
    lv_obj_remove_flag(text_column, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(text_column, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_column, 1);
    lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(text_column, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(text_column, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(text_column, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(text_column, 7, LV_PART_MAIN);

    title = lv_label_create(text_column);
    lv_label_set_text(title, title_text);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, DASHCAM_UI_ITEM_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_opa(title, 255, LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    size_mb = info->size_bytes / (1024U * 1024U);
    size_mb_tenth = ((info->size_bytes % (1024U * 1024U)) * 10U) / (1024U * 1024U);
    snprintf(size_text, sizeof(size_text), "%u.%u MB",
             (unsigned)size_mb, (unsigned)size_mb_tenth);

    size_label = lv_label_create(text_column);
    lv_label_set_text(size_label, size_text);
    lv_obj_set_width(size_label, LV_PCT(100));
    lv_obj_set_style_text_color(size_label, lv_color_hex(0x8AA4B8), LV_PART_MAIN);
    lv_obj_set_style_text_opa(size_label, 255, LV_PART_MAIN);
    lv_obj_set_style_text_align(size_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

static lv_obj_t *dashcam_ui_reset_list(bk_lv_ui_t *ui)
{
    lv_obj_t *list = ui->dashcam_rec_list;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return NULL;
    }

    lv_obj_clean(list);
    memset(s_btns, 0, sizeof(s_btns));

    dashcam_ui_group_ensure();
    if (s_dashcam_group != NULL)
    {
        lv_group_remove_all_objs(s_dashcam_group);
    }

    /* Keep the list background on the original (dark) design color (req6 #2). */
    lv_obj_set_style_bg_color(list, DASHCAM_UI_LIST_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(list, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(list, DASHCAM_UI_ITEM_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);

    return list;
}

static void dashcam_ui_show_list_message(bk_lv_ui_t *ui, const char *message)
{
    lv_obj_t *list = dashcam_ui_reset_list(ui);
    lv_obj_t *text;

    if (list == NULL)
    {
        return;
    }

    text = lv_list_add_text(list, message);
    if (text != NULL)
    {
        lv_obj_set_style_text_color(text, DASHCAM_UI_ITEM_TEXT,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void dashcam_ui_populate_list(bk_lv_ui_t *ui, bk_err_t scan_result)
{
    lv_obj_t *list = dashcam_ui_reset_list(ui);
    uint32_t i;

    if (list == NULL)
    {
        return;
    }

    if (scan_result != BK_OK)
    {
        dashcam_ui_show_list_message(ui, "Load failed");
        LOGW("scan records failed: %d\n", scan_result);
        return;
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
        dashcam_ui_show_list_message(ui, "No records");
        s_sel_index = 0;
        LOGI("records list empty\n");
        return;
    }

    for (i = 0; i < s_file_count; i++)
    {
        char label[48];
        lv_obj_t *btn;

        dashcam_storage_format_label(&s_files[i], label, sizeof(label));
        btn = lv_list_add_button(list, &play_btn_trans_ARGB8888, NULL);
        if (btn != NULL)
        {
            dashcam_ui_style_item(btn);
            dashcam_ui_add_item_labels(btn, label, &s_files[i]);
            lv_obj_add_event_cb(btn, dashcam_ui_item_clicked_cb, LV_EVENT_CLICKED, &s_files[i]);
            lv_obj_add_event_cb(btn, dashcam_ui_item_key_cb, LV_EVENT_KEY, NULL);
            s_btns[i] = btn;

            if (s_dashcam_group != NULL)
            {
                lv_group_add_obj(s_dashcam_group, btn);
            }
        }
    }

    if (s_sel_index >= (int32_t)s_file_count)
    {
        s_sel_index = 0;
    }

    LOGI("records list populated: %u\n", (unsigned)s_file_count);
}

static void dashcam_ui_focus_current_item(void)
{
    if (s_dashcam_group == NULL || s_file_count == 0)
    {
        return;
    }

    if (s_sel_index < 0 || s_sel_index >= (int32_t)s_file_count)
    {
        s_sel_index = 0;
    }
    if (s_btns[s_sel_index] != NULL && lv_obj_is_valid(s_btns[s_sel_index]))
    {
        lv_group_focus_obj(s_btns[s_sel_index]);
    }
}

static void dashcam_ui_load_complete_cb(void *user_data)
{
    dashcam_ui_load_result_t *result = (dashcam_ui_load_result_t *)user_data;
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    bool result_is_current;

    if (result == NULL)
    {
        return;
    }

    dashcam_ui_load_state_lock();
    result_is_current = s_page_active && result->generation == s_load_generation;
    dashcam_ui_load_state_unlock();

    if (!result_is_current ||
        ui->dashcam == NULL || !lv_obj_is_valid(ui->dashcam))
    {
        os_free(result);
        return;
    }

    s_file_count = result->file_count;
    memcpy(s_files, result->files, sizeof(s_files));
    dashcam_ui_populate_list(ui, result->scan_result);
    dashcam_app_attach(ui->dashcam_sky_area);
    dashcam_ui_focus_current_item();
    dashcam_ui_set_status(ui);
    os_free(result);
}

static void dashcam_ui_load_failed_cb(void *user_data)
{
    uint32_t generation = (uint32_t)(uintptr_t)user_data;
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    bool result_is_current;

    dashcam_ui_load_state_lock();
    result_is_current = s_page_active && generation == s_load_generation;
    dashcam_ui_load_state_unlock();

    if (!result_is_current ||
        ui->dashcam == NULL || !lv_obj_is_valid(ui->dashcam))
    {
        return;
    }

    dashcam_ui_show_list_message(ui, "Load failed");
    dashcam_app_attach(ui->dashcam_sky_area);
    dashcam_ui_set_status(ui);
}

static void dashcam_ui_load_worker(void *arg)
{
    dashcam_ui_load_result_t *result;
    uint32_t generation;
    bool page_active;

    (void)arg;
    result = os_malloc(sizeof(*result));
    if (result != NULL)
    {
        memset(result, 0, sizeof(*result));
    }

    // dashcam_app_record_stop();
    if (result != NULL)
    {
        result->scan_result = dashcam_storage_scan(result->files,
                                                   DASHCAM_UI_MAX_ITEMS,
                                                   &result->file_count);
    }

    dashcam_ui_load_state_lock();
    s_load_worker_running = false;
    page_active = s_page_active;
    generation = s_load_generation;
    dashcam_ui_load_state_unlock();

    if (page_active && result != NULL)
    {
        result->generation = generation;
        if (lv_async_call(dashcam_ui_load_complete_cb, result) != LV_RESULT_OK)
        {
            os_free(result);
        }
    }
    else if (page_active)
    {
        (void)lv_async_call(dashcam_ui_load_failed_cb,
                            (void *)(uintptr_t)generation);
    }
    else
    {
        if (result != NULL)
        {
            os_free(result);
        }
        if (!page_active)
        {
            (void)dashcam_app_record_start();
        }
    }

    s_load_thread = NULL;
    rtos_delete_thread(NULL);
}

static void dashcam_ui_start_async_load(void)
{
    bk_err_t ret;

    dashcam_ui_load_state_lock();
    if (!s_page_active || s_load_worker_running)
    {
        dashcam_ui_load_state_unlock();
        return;
    }
    s_load_worker_running = true;

    ret = rtos_create_thread(&s_load_thread,
                             DASHCAM_UI_LOAD_PRIO,
                             "dcam_load",
                             dashcam_ui_load_worker,
                             DASHCAM_UI_LOAD_STACK,
                             NULL);
    if (ret != BK_OK)
    {
        s_load_thread = NULL;
        s_load_worker_running = false;
        dashcam_ui_load_state_unlock();
        dashcam_ui_show_list_message(&bk_lv_tool_ui, "Load failed");
        LOGE("create load worker failed: %d\n", ret);
        return;
    }
    dashcam_ui_load_state_unlock();
}

void dashcam_ui_boot_start(void)
{
    static const dashcam_assitview_hooks_t assist_hooks =
    {
        .before_lvgl_teardown = beken_ui_before_assist_lvgl_teardown,
        .after_display_resume = beken_ui_kick_after_display_resume,
    };

    LOGD("boot_start\n");
    dashcam_assitview_register_hooks(&assist_hooks);
    if (dashcam_ui_load_state_init() != BK_OK)
    {
        LOGE("init load state mutex failed\n");
    }
    dashcam_app_boot_start();
    dashcam_assitview_init();
}

void dashcam_ui_shutdown(void)
{
    LOGD("shutdown\n");
    dashcam_ui_load_state_lock();
    s_page_active = false;
    s_load_generation++;
    dashcam_ui_load_state_unlock();
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
    dashcam_ui_load_state_lock();
    s_page_active = false;
    s_load_generation++;
    dashcam_ui_load_state_unlock();
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

    if (dashcam_ui_load_state_init() != BK_OK)
    {
        dashcam_ui_show_list_message(ui, "Load failed");
        LOGE("load state mutex unavailable\n");
        return;
    }

    s_list_focused = false;
    s_sel_index = 0;
    s_play_info_valid = false;
    s_file_count = 0;
    dashcam_ui_load_state_lock();
    s_page_active = true;
    s_load_generation++;
    dashcam_ui_load_state_unlock();

    /* Render first, then stop/finalize recording and scan SD on the worker. */
    dashcam_ui_show_list_message(ui, "Loading...");
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
    dashcam_ui_start_async_load();
}

void dashcam_ui_leave(void)
{
    bk_err_t ret;
    bool restart_recording;

    LOGD("leave\n");
    dashcam_ui_load_state_lock();
    s_page_active = false;
    s_load_generation++;
    restart_recording = !s_load_worker_running;
    dashcam_ui_load_state_unlock();

    /* The page (and its widgets) may be freed after this; the event callback
     * is destroyed with the panel, so just drop our bound flag. */
    dashcam_ui_stop_info_timer();
    s_preview_cb_bound = false;
    s_list_focused = false;
    s_play_info_valid = false;
    memset(s_btns, 0, sizeof(s_btns));
    dashcam_app_detach();

    /* If loading is still running it owns SDIO and restarts recording when the
     * scan returns. Otherwise recording can resume immediately. */
    if (restart_recording)
    {
        ret = dashcam_app_record_start();
        if (ret != BK_OK)
        {
            LOGW("restart recording after page leave failed: %d\n", ret);
        }
    }
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

bool dashcam_ui_handle_key_home(void)
{
    if (!dashcam_app_is_playing())
    {
        return false;
    }

    /*
     * A clip is playing (direct-DPU playback stops the LVGL task). Double-press
     * of the MIDDLE key routes here via beken_ui_key_home: stop playback so the
     * dashcam list page comes back, instead of jumping all the way to HOME. This
     * handler runs on the application key-event thread, so it only tears down the
     * player/display handoff; LVGL is restored inside dashcam_app_stop_playback.
     */
    LOGI("home key: stop playback, return to dashcam list\n");
    dashcam_app_stop_playback();
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

    /* Advance the selection through the group so it stays in sync with the
     * LEFT/RIGHT keypad navigation (focus_cb writes back s_sel_index). */
    if (s_dashcam_group != NULL)
    {
        lv_group_focus_next(s_dashcam_group);
    }
    else
    {
        s_sel_index = (s_sel_index + 1) % (int32_t)s_file_count;
        dashcam_ui_apply_selection();
    }
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

    /*
     * Prefer routing "confirm" through the group: send ENTER to the focused
     * item so LVGL fires its CLICKED cb (dashcam_ui_item_clicked_cb) and plays
     * it - the same path a touch tap takes. Fall back to a direct play if the
     * group / focus is somehow unavailable so long-press never becomes a no-op.
     */
    if (s_dashcam_group != NULL && lv_group_get_focused(s_dashcam_group) != NULL)
    {
        LOGI("key long -> ENTER focused sel=%d\n", (int)s_sel_index);
        lv_port_keypad_send_key(LV_KEY_ENTER);
        return true;
    }

    LOGI("key long -> play sel=%d %s\n", (int)s_sel_index, s_files[s_sel_index].path);

    if (!dashcam_ui_clip_is_playable(&s_files[s_sel_index]))
    {
        return true;
    }

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
