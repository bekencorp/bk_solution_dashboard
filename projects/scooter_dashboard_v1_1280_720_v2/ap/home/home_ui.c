/*
 * Home page logic, split out of the generated beken_ui.c.
 *
 * Contains the home page's own behavior only (speed gauge, the
 * music/phone panel and its Bluetooth callbacks, and the home background
 * bitmap). Navigation between pages stays in beken_ui.c, mirroring the
 * ota_ui / dashcam_ui split.
 */

#include "home_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "beken_ui.h"
#include "components/log.h"
#include <os/mem.h>
#include <os/os.h>
#include "components/bluetooth/bk_dm_avrcp_types.h"
#include "a2dp_sink_demo.h"
#include "hfp_hf_demo.h"

static lv_group_t *s_nav_group = NULL;
static home_ui_nav_callback_t s_nav_focus_cb = NULL;
static home_ui_nav_callback_t s_nav_activate_cb = NULL;

extern void home_dash_entry_event_cb(lv_event_t *e);
extern void home_ota_entry_event_cb(lv_event_t *e);
extern void home_phone_entry_event_cb(lv_event_t *e);
extern void home_music_entry_event_cb(lv_event_t *e);

static lv_obj_t *home_ui_nav_entry_for_item(int32_t item)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    switch (item)
    {
    case HOME_UI_NAV_DASHCAM:
        return ui->home_dash_entry;
    case HOME_UI_NAV_OTA:
        return ui->home_ota_entry;
    case HOME_UI_NAV_PHONE_BOOK:
        return ui->home_phone_entry;
    case HOME_UI_NAV_MUSIC_PLAYER:
        return ui->home_music_entry;
    default:
        return NULL;
    }
}

static void home_ui_nav_image_set_scale(lv_obj_t *obj, int32_t scale)
{
    lv_image_set_scale(obj, (uint32_t)scale);
}

static void home_ui_nav_image_scale_anim_cb(void *var, int32_t value)
{
    home_ui_nav_image_set_scale((lv_obj_t *)var, value);
}

static void home_ui_nav_image_animate_scale(lv_obj_t *obj, int32_t target_scale)
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

    if (current_scale == target_scale)
    {
        lv_obj_set_style_image_opa(obj,
                                   target_scale > 256 ? LV_OPA_COVER : LV_OPA_60,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }

    lv_anim_delete(obj, home_ui_nav_image_scale_anim_cb);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, home_ui_nav_image_scale_anim_cb);
    lv_anim_set_values(&anim, current_scale, target_scale);
    lv_anim_set_duration(&anim, 180);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);

    lv_obj_set_style_image_opa(obj,
                               target_scale > 256 ? LV_OPA_COVER : LV_OPA_60,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void home_ui_nav_apply_selection(int32_t selected_item)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *entries[] = {
        ui->home_dash_entry,
        ui->home_ota_entry,
        ui->home_phone_entry,
        ui->home_music_entry,
    };
    lv_obj_t *icons[] = {
        ui->home_dash_ic,
        ui->home_ota_ic,
        ui->home_phone_ic,
        ui->home_nav_music_ic,
    };
    const int32_t items[] = {
        HOME_UI_NAV_DASHCAM,
        HOME_UI_NAV_OTA,
        HOME_UI_NAV_PHONE_BOOK,
        HOME_UI_NAV_MUSIC_PLAYER,
    };
    const int32_t normal_scale = 256;
    const int32_t selected_scale = 410;
    uint32_t i;

    if (ui->home == NULL || !lv_obj_is_valid(ui->home))
    {
        return;
    }

    for (i = 0; i < 4; i++)
    {
        if (entries[i] != NULL && lv_obj_is_valid(entries[i]))
        {
            lv_obj_set_style_border_width(entries[i], 0,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(entries[i], 0,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_scale_x(entries[i], normal_scale,
                                               LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_scale_y(entries[i], normal_scale,
                                               LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        if (icons[i] != NULL && lv_obj_is_valid(icons[i]))
        {
            lv_image_set_pivot(icons[i],
                               lv_obj_get_width(icons[i]) / 2,
                               lv_obj_get_height(icons[i]) / 2);
            home_ui_nav_image_animate_scale(
                icons[i],
                selected_item == items[i] ? selected_scale : normal_scale);
        }
    }
}

static int32_t home_ui_nav_item_for_entry(lv_obj_t *entry)
{
    if (entry == bk_lv_tool_ui.home_dash_entry)
    {
        return HOME_UI_NAV_DASHCAM;
    }
    if (entry == bk_lv_tool_ui.home_ota_entry)
    {
        return HOME_UI_NAV_OTA;
    }
    if (entry == bk_lv_tool_ui.home_phone_entry)
    {
        return HOME_UI_NAV_PHONE_BOOK;
    }
    if (entry == bk_lv_tool_ui.home_music_entry)
    {
        return HOME_UI_NAV_MUSIC_PLAYER;
    }

    return 0;
}

static void home_ui_nav_group_focus_cb(lv_group_t *group)
{
    int32_t item = home_ui_nav_item_for_entry(lv_group_get_focused(group));

    if (item == 0)
    {
        return;
    }

    home_ui_nav_apply_selection(item);
    if (s_nav_focus_cb != NULL)
    {
        s_nav_focus_cb(item);
    }
}

static void home_ui_nav_entry_activate_cb(lv_event_t *e)
{
    int32_t item = (int32_t)(intptr_t)lv_event_get_user_data(e);

    if (s_nav_activate_cb != NULL)
    {
        s_nav_activate_cb(item);
    }
}

static void home_ui_nav_replace_generated_cb(lv_obj_t *entry, int32_t item)
{
    switch (item)
    {
    case HOME_UI_NAV_DASHCAM:
        lv_obj_remove_event_cb(entry, home_dash_entry_event_cb);
        break;
    case HOME_UI_NAV_OTA:
        lv_obj_remove_event_cb(entry, home_ota_entry_event_cb);
        break;
    case HOME_UI_NAV_PHONE_BOOK:
        lv_obj_remove_event_cb(entry, home_phone_entry_event_cb);
        break;
    case HOME_UI_NAV_MUSIC_PLAYER:
        lv_obj_remove_event_cb(entry, home_music_entry_event_cb);
        break;
    default:
        return;
    }

    lv_obj_remove_event_cb(entry, home_ui_nav_entry_activate_cb);
    lv_obj_add_event_cb(entry, home_ui_nav_entry_activate_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)item);
}

void home_ui_nav_group_build(int32_t selected_item,
                             home_ui_nav_callback_t focus_cb,
                             home_ui_nav_callback_t activate_cb)
{
    lv_obj_t *focus_target;
    int32_t item;

    if (bk_lv_tool_ui.home == NULL || !lv_obj_is_valid(bk_lv_tool_ui.home))
    {
        return;
    }

    s_nav_focus_cb = focus_cb;
    s_nav_activate_cb = activate_cb;

    if (s_nav_group == NULL)
    {
        s_nav_group = lv_group_create();
        if (s_nav_group == NULL)
        {
            return;
        }
        lv_group_set_wrap(s_nav_group, true);
        lv_group_set_focus_cb(s_nav_group, home_ui_nav_group_focus_cb);
    }

    lv_group_remove_all_objs(s_nav_group);

    for (item = HOME_UI_NAV_DASHCAM; item <= HOME_UI_NAV_MUSIC_PLAYER; item++)
    {
        lv_obj_t *entry = home_ui_nav_entry_for_item(item);

        if (entry != NULL && lv_obj_is_valid(entry))
        {
            lv_group_add_obj(s_nav_group, entry);
            home_ui_nav_replace_generated_cb(entry, item);
        }
    }

    focus_target = home_ui_nav_entry_for_item(selected_item);
    if (focus_target == NULL || !lv_obj_is_valid(focus_target))
    {
        selected_item = HOME_UI_NAV_DASHCAM;
        focus_target = home_ui_nav_entry_for_item(selected_item);
    }

    if (focus_target != NULL && lv_obj_is_valid(focus_target))
    {
        lv_group_focus_obj(focus_target);
    }
    else
    {
        home_ui_nav_apply_selection(selected_item);
    }
}

bool home_ui_nav_group_ready(void)
{
    return s_nav_group != NULL && lv_group_get_obj_count(s_nav_group) > 0;
}

bool home_ui_nav_focus(int32_t item)
{
    lv_obj_t *entry = home_ui_nav_entry_for_item(item);

    if (!home_ui_nav_group_ready() || entry == NULL || !lv_obj_is_valid(entry))
    {
        return false;
    }

    lv_group_focus_obj(entry);
    return true;
}

lv_group_t *home_ui_get_group(void)
{
    return s_nav_group;
}

/* Chinese track metadata (song title/artist) is rendered from a TrueType font
 * via LVGL's tiny_ttf engine, so arbitrary Chinese displays without baking a
 * CJK bitmap font into flash. On first home entry the whole .ttf is read once
 * from the SD card into a PSRAM buffer and handed to tiny_ttf via create_data;
 * after that the SD card is never touched again (no streaming, no open handle)
 * and cache-miss glyphs rasterize straight from PSRAM. The buffer and font are
 * kept for the process lifetime (create_data keeps a pointer into the buffer).
 * If loading fails, the labels keep their designer default (Latin) font. */
#define HOME_CN_TTF_PATH   "S:/simhei_new.ttf"
#define HOME_CN_TTF_SIZE   32
/* Cap the tiny_ttf glyph cache. The engine default (LV_TINY_TTF_CACHE_GLYPH_CNT
 * = 256) is an LRU by glyph COUNT; at 32px each CJK bitmap is ~1KB, so 256 of
 * them (~300KB, all in the HSRAM heap) exhausts HSRAM while music lyrics scroll
 * through many unique characters. 64 covers the on-screen set (title + artist +
 * a lyrics line) and bounds HSRAM use to roughly ~64KB. */
#define HOME_CN_TTF_GLYPH_CACHE_CNT 64
static lv_font_t *s_cn_font = NULL;
static void *s_cn_font_buf = NULL;
static uint32_t s_cn_font_buf_size = 0;

/* Read the whole TTF into a single PSRAM buffer once. All tiny_ttf fonts (any
 * size) then share this buffer, so it must outlive them (never freed). Returns
 * true when the buffer is available. */
static bool home_cn_buf_ensure(void)
{
    lv_fs_file_t f;
    uint32_t size = 0;
    uint32_t rd = 0;

    if (s_cn_font_buf != NULL)
    {
        return true;
    }

    if (lv_fs_open(&f, HOME_CN_TTF_PATH, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        BK_LOGE("home_ui", "open %s failed\n", HOME_CN_TTF_PATH);
        return false;
    }
    lv_fs_seek(&f, 0, LV_FS_SEEK_END);
    lv_fs_tell(&f, &size);
    lv_fs_seek(&f, 0, LV_FS_SEEK_SET);
    if (size == 0)
    {
        lv_fs_close(&f);
        BK_LOGE("home_ui", "%s is empty\n", HOME_CN_TTF_PATH);
        return false;
    }

    s_cn_font_buf = psram_malloc(size);
    if (s_cn_font_buf == NULL)
    {
        lv_fs_close(&f);
        BK_LOGE("home_ui", "psram_malloc(%u) for ttf failed\n", (unsigned)size);
        return false;
    }

    if (lv_fs_read(&f, s_cn_font_buf, size, &rd) != LV_FS_RES_OK || rd != size)
    {
        lv_fs_close(&f);
        psram_free(s_cn_font_buf);
        s_cn_font_buf = NULL;
        BK_LOGE("home_ui", "read %s short (%u/%u)\n", HOME_CN_TTF_PATH, (unsigned)rd, (unsigned)size);
        return false;
    }
    lv_fs_close(&f);

    s_cn_font_buf_size = size;
    return true;
}

/* Build the default-size CJK tiny_ttf font from the shared PSRAM buffer.
 * Returns NULL on any failure (file missing, OOM, short read, parse error). */
static lv_font_t *home_cn_font_load(void)
{
    lv_font_t *font;

    if (!home_cn_buf_ensure())
    {
        return NULL;
    }

    /* create_data does NOT copy: it keeps a pointer into s_cn_font_buf, so the
     * buffer must outlive the font (both are never freed). Use the _ex variant
     * to cap the glyph cache (default 256 blows the HSRAM heap, see above). */
    font = lv_tiny_ttf_create_data_ex(s_cn_font_buf, s_cn_font_buf_size,
                                      HOME_CN_TTF_SIZE,
                                      LV_FONT_KERNING_NORMAL,
                                      HOME_CN_TTF_GLYPH_CACHE_CNT);
    if (font == NULL)
    {
        BK_LOGE("home_ui", "tiny_ttf parse %s failed\n", HOME_CN_TTF_PATH);
    }
    return font;
}

/*
 * Expose the loaded CJK TTF so other pages (e.g. phone_book) can render Chinese
 * contact names with the same single PSRAM-resident font. The home page loads it
 * in home_ui_enter(), which always runs before any page can be navigated to.
 */
lv_font_t *home_ui_get_cn_font(void)
{
    return s_cn_font;
}

/*
 * Create an additional CJK font at an arbitrary pixel size, sharing the single
 * PSRAM TTF buffer (no extra copy of the font data; only a per-size glyph cache).
 * Returns NULL if the buffer is unavailable. The caller keeps the returned font
 * for the process lifetime (never freed, like s_cn_font) - create each size once.
 */
lv_font_t *home_ui_create_cn_font(uint32_t px)
{
    if (!home_cn_buf_ensure())
    {
        return NULL;
    }
    return lv_tiny_ttf_create_data_ex(s_cn_font_buf, s_cn_font_buf_size, px,
                                      LV_FONT_KERNING_NORMAL,
                                      HOME_CN_TTF_GLYPH_CACHE_CNT);
}

/*
 * The home background is a 1280x720 image. Two paths render it as a plain
 * PSRAM bitmap (a fast blit, no per-frame work):
 *
 *  1) Fast path: a worker thread (boot_bg_preload) decodes the JPEG
 *     (1:/home_bg.jpg) into an RGB565 PSRAM bitmap during the boot animation,
 *     using LVGL's software JPEG decoder. Here we just wrap that buffer as the
 *     background image - no decoding at all on the UI-init path.
 *
 *  2) Fallback: if the blob is missing, decode the JPEG (S:/home_bg.jpg) once
 *     into a PSRAM canvas. The SDK's LVGL JPEG decoder (TJPGD) is a streaming
 *     decoder and the image cache is disabled, so using the file directly as an
 *     image source would re-decode it for every render tile / refresh; decoding
 *     once into a bitmap avoids that.
 */
static lv_obj_t *s_bg_canvas = NULL;
static void *s_bg_canvas_buf = NULL;

void home_ui_install_bg(void)
{
    lv_obj_t *home = bk_lv_tool_ui.home;

    if (home == NULL)
    {
        return;
    }

    /* Preferred: instant wrap of the background preloaded during the animation. */
    if (beken_ui_install_preloaded_bg(bk_lv_tool_ui.home_bg_img))
    {
        return;
    }

    const int32_t w = 1280;
    const int32_t h = 720;
    const size_t buf_size = (size_t)w * (size_t)h * 2u + 1024u; /* RGB565 + slack */

    if (s_bg_canvas != NULL && lv_obj_is_valid(s_bg_canvas))
    {
        if (bk_lv_tool_ui.home_bg_img != NULL)
        {
            lv_obj_add_flag(bk_lv_tool_ui.home_bg_img, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    void *buf = psram_malloc(buf_size);

    if (buf == NULL)
    {
        BK_LOGE("ui_bg", "psram_malloc(%u) failed, keep slow file bg\n", (unsigned)buf_size);
        return;
    }

    s_bg_canvas_buf = buf;

    s_bg_canvas = lv_canvas_create(home);

    if (s_bg_canvas == NULL)
    {
        psram_free(buf);
        s_bg_canvas_buf = NULL;
        return;
    }

    lv_canvas_set_buffer(s_bg_canvas, buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_bg_canvas, 0, 0);
    lv_canvas_fill_bg(s_bg_canvas, lv_color_black(), LV_OPA_COVER);

    /* One-shot full-frame decode of the JPEG into the canvas buffer. */
    lv_layer_t layer;
    lv_canvas_init_layer(s_bg_canvas, &layer);

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = "S:/home_bg.jpg";

    lv_area_t coords = {0, 0, w - 1, h - 1};
    lv_draw_image(&layer, &dsc, &coords);

    lv_canvas_finish_layer(s_bg_canvas, &layer);

    /* Keep the decoded bitmap at the very back; drop the slow file image. */
    lv_obj_move_background(s_bg_canvas);

    if (bk_lv_tool_ui.home_bg_img != NULL)
    {
        lv_obj_add_flag(bk_lv_tool_ui.home_bg_img, LV_OBJ_FLAG_HIDDEN);
    }

    BK_LOGI("ui_bg", "decoded home_bg.jpg into PSRAM bitmap (%dx%d)\n", (int)w, (int)h);
}

/* ---------- Gauges: speed and system needles sweep together ----------
 * Keep the two generated scales and their numeric labels refreshed from one
 * timer, matching the 1024x600 dashboard behavior.
 */
#define SPEED_NEEDLE_LENGTH    170
#define SPEED_SCALE_MAX        60
#define SPEED_ANIM_PERIOD_MS   10
#define SPEED_ANIM_STEP        3
#define SYS_NEEDLE_LENGTH      170
#define SYS_SCALE_MAX          8000
#define SYS_ANIM_STEP          400
static int32_t s_speed_value = 0;
static int32_t s_speed_dir = 1;
static int32_t s_sys_value = 0;
static int32_t s_sys_dir = 1;
static lv_timer_t *s_speed_timer = NULL;
static lv_timer_t *s_music_timer = NULL;

#define HOME_MUSIC_DEFAULT_TITLE       "No Title"
#define HOME_MUSIC_DEFAULT_ARTIST      "No Artist"
#define HOME_MUSIC_INCOMING_TAG        "INCOMING CALL"
#define HOME_MUSIC_OUTGOING_TAG        "DIALING"
#define HOME_MUSIC_ACTIVE_TAG          "IN CALL"
#define HOME_MUSIC_TIMER_PERIOD_MS     200
#define HOME_MUSIC_BEAT_MIN_HEIGHT     8
#define HOME_MUSIC_BEAT_MAX_HEIGHT     84
#define HOME_MUSIC_PANEL_X             435
#define HOME_MUSIC_PANEL_Y             10
#define HOME_MUSIC_BEAT_CANVAS_X       (HOME_MUSIC_PANEL_X + 20)
#define HOME_MUSIC_BEAT_CANVAS_Y       (HOME_MUSIC_PANEL_Y + 12)
#define HOME_MUSIC_BEAT_CANVAS_W       310
#define HOME_MUSIC_BEAT_CANVAS_H       84
#define HOME_MUSIC_BEAT_BAR_COUNT      20
#define HOME_MUSIC_BEAT_BAR_W          8

typedef struct
{
    char title[96];
    char artist[96];
    char phone_number[40];
    uint32_t duration_ms;
    uint32_t position_ms;
    uint32_t progress_accum_ms;
    uint8_t playing;
    hfp_hf_call_state_t call_state;
    uint32_t call_start_tick;
    uint8_t call_timing;
    uint8_t beat_phase;
    uint8_t last_progress;
    uint8_t progress_valid;
} home_music_state_t;

typedef struct
{
    char title[96];
    char artist[96];
    uint32_t duration_ms;
    uint32_t position_ms;
    uint8_t playing;
    uint8_t has_title;
    uint8_t has_artist;
    uint8_t has_duration;
    uint8_t has_position;
    uint8_t has_playing;
} home_music_update_async_t;

typedef struct
{
    char number[40];
    hfp_hf_call_state_t state;
} home_music_phone_async_t;

/*
 * Bluetooth callbacks run outside the LVGL task. Keep only the newest state
 * here and let the home-page LVGL timer consume it. This avoids touching the
 * LVGL timer list from the Bluetooth task and, while lv_vendor_stop() is active,
 * prevents an unbounded list of lv_async_call() timers from accumulating.
 */
typedef struct
{
    home_music_update_async_t music;
    home_music_phone_async_t phone;
    uint32_t play_position_ms;
    uint8_t has_music;
    uint8_t has_phone;
    uint8_t has_play_position;
} home_music_pending_t;

static home_music_state_t s_home_music = {
    .title = "",
    .artist = "",
};
static home_music_pending_t s_home_music_pending;
static beken_mutex_t s_home_music_pending_mutex = NULL;
static lv_obj_t *s_home_beat_canvas = NULL;
static void *s_home_beat_canvas_buf = NULL;

static void home_music_apply(void);
static void home_music_pending_apply(void);

static void speed_gauge_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (!ui->home || !lv_obj_is_valid(ui->home) ||
        !ui->home_speed_scale || !lv_obj_is_valid(ui->home_speed_scale) ||
        !ui->home_speed_scale_needle_0 || !lv_obj_is_valid(ui->home_speed_scale_needle_0) ||
        !ui->home_speed_val || !lv_obj_is_valid(ui->home_speed_val))
    {
        return;
    }

    lv_scale_set_line_needle_value(ui->home_speed_scale, ui->home_speed_scale_needle_0,
                                   SPEED_NEEDLE_LENGTH, s_speed_value);
    if (ui->home_sys_scale && lv_obj_is_valid(ui->home_sys_scale) &&
        ui->home_sys_scale_needle_0 && lv_obj_is_valid(ui->home_sys_scale_needle_0))
    {
        lv_scale_set_line_needle_value(ui->home_sys_scale, ui->home_sys_scale_needle_0,
                                       SYS_NEEDLE_LENGTH, s_sys_value);
    }
    if (ui->home_volt_txt && lv_obj_is_valid(ui->home_volt_txt))
    {
        lv_label_set_text_fmt(ui->home_volt_txt, "%" LV_PRId32, s_sys_value / 100);
    }
    lv_label_set_text_fmt(ui->home_speed_val, "%" LV_PRId32, s_speed_value);
}

static void speed_gauge_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    home_music_pending_apply();

    s_speed_value += s_speed_dir * SPEED_ANIM_STEP;
    if (s_speed_value >= SPEED_SCALE_MAX)
    {
        s_speed_value = SPEED_SCALE_MAX;
        s_speed_dir = -1;
    }
    else if (s_speed_value <= 0)
    {
        s_speed_value = 0;
        s_speed_dir = 1;
    }

    s_sys_value += s_sys_dir * SYS_ANIM_STEP;
    if (s_sys_value >= SYS_SCALE_MAX)
    {
        s_sys_value = SYS_SCALE_MAX;
        s_sys_dir = -1;
    }
    else if (s_sys_value <= 0)
    {
        s_sys_value = 0;
        s_sys_dir = 1;
    }
    speed_gauge_apply();
}

static bool home_music_obj_valid(lv_obj_t *obj)
{
    return obj != NULL && lv_obj_is_valid(obj);
}

static bool home_call_is_active(void)
{
    return s_home_music.call_state != HFP_HF_CALL_NONE;
}

/* ------------------------------------------------------------------ */
/* Method-A call control: keypad/group over the existing call buttons  */
/* ------------------------------------------------------------------ */

/*
 * The incoming popup, outgoing popup and on-call card expose existing lv_btn
 * controls. Instead of a dedicated phone key, drive them through the shared
 * KEYPAD indev: while a call rings, dials or is active, push a small modal group
 * up to beken_ui so LEFT/RIGHT move between available actions and ENTER
 * confirms. The buttons live on HOME, so this engages while HOME is resident.
 */
static lv_group_t *s_call_group = NULL;

static void home_cp_answer_event_cb(lv_event_t *e)
{
    (void)e;
    hfp_demo_answer(1);
}

static void home_cp_hangup_event_cb(lv_event_t *e)
{
    (void)e;
    hfp_demo_answer(0);
}

static void home_call_button_add_focus_style(lv_obj_t *button)
{
    lv_obj_set_style_border_color(button, lv_color_hex(0xffffff),
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 3,
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button, lv_color_hex(0x1e7fcf),
                                   LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(button, 3,
                                   LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(button, 2,
                                 LV_PART_MAIN | LV_STATE_FOCUSED);
}

static void home_call_group_ensure(void)
{
    if (s_call_group == NULL)
    {
        s_call_group = lv_group_create();
        if (s_call_group != NULL)
        {
            lv_group_set_wrap(s_call_group, true);
        }
    }
}

/* (Re)attach the CLICKED handlers to the (possibly rebuilt) call buttons.
 * Idempotent: remove any prior binding first so a home re-enter cannot stack
 * duplicate handlers (which would answer/hang up twice). */
static void home_call_bind_buttons(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (home_music_obj_valid(ui->home_cp_answer))
    {
        home_call_button_add_focus_style(ui->home_cp_answer);
        lv_obj_remove_event_cb(ui->home_cp_answer, home_cp_answer_event_cb);
        lv_obj_add_event_cb(ui->home_cp_answer, home_cp_answer_event_cb, LV_EVENT_CLICKED, NULL);
    }
    if (home_music_obj_valid(ui->home_cp_hangup))
    {
        home_call_button_add_focus_style(ui->home_cp_hangup);
        lv_obj_remove_event_cb(ui->home_cp_hangup, home_cp_hangup_event_cb);
        lv_obj_add_event_cb(ui->home_cp_hangup, home_cp_hangup_event_cb, LV_EVENT_CLICKED, NULL);
    }
    if (home_music_obj_valid(ui->home_oc_hangup))
    {
        home_call_button_add_focus_style(ui->home_oc_hangup);
        lv_obj_remove_event_cb(ui->home_oc_hangup, home_cp_hangup_event_cb);
        lv_obj_add_event_cb(ui->home_oc_hangup, home_cp_hangup_event_cb, LV_EVENT_CLICKED, NULL);
    }
    if (home_music_obj_valid(ui->home_op_hangup))
    {
        home_call_button_add_focus_style(ui->home_op_hangup);
        lv_obj_remove_event_cb(ui->home_op_hangup, home_cp_hangup_event_cb);
        lv_obj_add_event_cb(ui->home_op_hangup, home_cp_hangup_event_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void home_call_focus_modal_button(lv_obj_t *button)
{
    if (s_call_group == NULL || !home_music_obj_valid(button))
    {
        return;
    }

    /*
     * Bind the group before focusing so LVGL associates the focus event with
     * the keypad indev and applies both FOCUSED and FOCUS_KEY states.
     */
    beken_ui_keypad_set_modal_group(s_call_group);
    lv_group_focus_obj(button);

    if (lv_group_get_focused(s_call_group) != button)
    {
        BK_LOGI("home_ui", "call modal focus failed: target=%p focused=%p\n",
                button, lv_group_get_focused(s_call_group));
        return;
    }

    lv_obj_add_state(button, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    lv_obj_invalidate(button);
}

/*
 * Rebuild the call modal group from the current call state and (un)install it as
 * the keypad modal override. Call after home_music_apply() has updated popup /
 * card visibility, and on home_ui_enter (to re-engage after a page rebuild).
 */
static void home_call_nav_sync(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    hfp_hf_call_state_t st = s_home_music.call_state;

    home_call_group_ensure();
    if (s_call_group == NULL)
    {
        return;
    }

    lv_group_remove_all_objs(s_call_group);

    if (st == HFP_HF_CALL_INCOMING &&
        home_music_obj_valid(ui->home_cp_answer) &&
        home_music_obj_valid(ui->home_cp_hangup))
    {
        lv_group_add_obj(s_call_group, ui->home_cp_answer);
        lv_group_add_obj(s_call_group, ui->home_cp_hangup);
        home_call_focus_modal_button(ui->home_cp_answer);
    }
    else if (st == HFP_HF_CALL_ACTIVE &&
             home_music_obj_valid(ui->home_oc_hangup))
    {
        lv_group_add_obj(s_call_group, ui->home_oc_hangup);
        home_call_focus_modal_button(ui->home_oc_hangup);
    }
    else if (st == HFP_HF_CALL_OUTGOING &&
             home_music_obj_valid(ui->home_op_hangup))
    {
        lv_group_add_obj(s_call_group, ui->home_op_hangup);
        home_call_focus_modal_button(ui->home_op_hangup);
    }
    else
    {
        /* No call (or dialing): drop the modal so keys return to the page. */
        beken_ui_keypad_set_modal_group(NULL);
    }
}

/*
 * Incoming-call popup title + number opacity control. The old attention blink
 * is disabled (see home_call_blink_sync); these helpers now only keep the popup
 * fully opaque and tear down any leftover timer.
 */
static lv_timer_t *s_call_blink_timer = NULL;

static void home_call_blink_set_opa(lv_opa_t opa)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (home_music_obj_valid(ui->home_cp_title))
    {
        lv_obj_set_style_opa(ui->home_cp_title, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (home_music_obj_valid(ui->home_cp_num))
    {
        lv_obj_set_style_opa(ui->home_cp_num, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void home_call_blink_stop(void)
{
    if (s_call_blink_timer != NULL)
    {
        lv_timer_delete(s_call_blink_timer);
        s_call_blink_timer = NULL;
    }
    home_call_blink_set_opa(LV_OPA_COVER);
}

static void home_call_blink_sync(void)
{
    /* Incoming-call attention blink is disabled per request: the popup title +
     * number stay steady (fully opaque) instead of pulsing. Keep the timer
     * stopped in every state. */
    home_call_blink_stop();
}

static void home_music_set_label(lv_obj_t *label, const char *text)
{
    if (home_music_obj_valid(label))
    {
        const char *new_text = (text != NULL && text[0] != '\0') ? text : "";
        const char *old_text = lv_label_get_text(label);

        if (old_text == NULL || strcmp(old_text, new_text) != 0)
        {
            lv_label_set_text(label, new_text);
        }
    }
}

static uint16_t home_music_pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

/* Alpha-blend a foreground color over the panel background (0x12091f) so the
 * canvas bars keep the same semi-transparent look on the music panel. */
static uint16_t home_music_blend_rgb565(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t bg_r = 0x12, bg_g = 0x09, bg_b = 0x1f;
    uint8_t or = (uint8_t)(((uint16_t)r * a + (uint16_t)bg_r * (255U - a)) / 255U);
    uint8_t og = (uint8_t)(((uint16_t)g * a + (uint16_t)bg_g * (255U - a)) / 255U);
    uint8_t ob = (uint8_t)(((uint16_t)b * a + (uint16_t)bg_b * (255U - a)) / 255U);

    return home_music_pack_rgb565(or, og, ob);
}

static bool home_music_beat_canvas_ready(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    const size_t buf_size = HOME_MUSIC_BEAT_CANVAS_W * HOME_MUSIC_BEAT_CANVAS_H * sizeof(uint16_t);

    if (!home_music_obj_valid(ui->home_bottom_bar))
    {
        return false;
    }

    if (s_home_beat_canvas != NULL && lv_obj_is_valid(s_home_beat_canvas))
    {
        return true;
    }

    s_home_beat_canvas = NULL;
    if (s_home_beat_canvas_buf == NULL)
    {
        /* Keep this ~51KB canvas out of the tight SRAM heap: allocate it in PSRAM
         * (plenty free), matching the background canvas. Fast enough for the
         * small per-frame redraw. */
        s_home_beat_canvas_buf = psram_malloc(buf_size);
        if (s_home_beat_canvas_buf == NULL)
        {
            return false;
        }
    }

    s_home_beat_canvas = lv_canvas_create(ui->home_bottom_bar);
    if (s_home_beat_canvas == NULL)
    {
        psram_free(s_home_beat_canvas_buf);
        s_home_beat_canvas_buf = NULL;
        return false;
    }

    lv_canvas_set_buffer(s_home_beat_canvas,
                         s_home_beat_canvas_buf,
                         HOME_MUSIC_BEAT_CANVAS_W,
                         HOME_MUSIC_BEAT_CANVAS_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_home_beat_canvas, HOME_MUSIC_BEAT_CANVAS_X, HOME_MUSIC_BEAT_CANVAS_Y);
    lv_obj_set_size(s_home_beat_canvas, HOME_MUSIC_BEAT_CANVAS_W, HOME_MUSIC_BEAT_CANVAS_H);
    lv_obj_remove_flag(s_home_beat_canvas, LV_OBJ_FLAG_SCROLLABLE);
    return true;
}

static void home_music_apply_beat(void)
{
    static const uint8_t pattern[] = {16, 52, 28, 72, 38, 84, 30, 60, 34, 78, 22, 64, 44, 80, 30, 56, 38, 72, 28, 64};
    const uint16_t bg = home_music_pack_rgb565(0x12, 0x09, 0x1f);
    /* 6-color gradient palette (blended over the panel bg) for the beat bars. */
    const uint16_t colors[] = {
        home_music_blend_rgb565(0x7c, 0x3a, 0xed, 204),
        home_music_blend_rgb565(0xa8, 0x55, 0xf7, 221),
        home_music_blend_rgb565(0x63, 0x66, 0xf1, 204),
        home_music_blend_rgb565(0xd9, 0x46, 0xef, 221),
        home_music_blend_rgb565(0x8b, 0x5c, 0xf6, 204),
        home_music_blend_rgb565(0xec, 0x48, 0x99, 221),
    };
    uint16_t *buf = (uint16_t *)s_home_beat_canvas_buf;
    uint32_t gap;

    if (!home_music_beat_canvas_ready())
    {
        return;
    }

    buf = (uint16_t *)s_home_beat_canvas_buf;
    if (buf == NULL)
    {
        return;
    }

    for (uint32_t i = 0; i < HOME_MUSIC_BEAT_CANVAS_W * HOME_MUSIC_BEAT_CANVAS_H; ++i)
    {
        buf[i] = bg;
    }

    gap = (HOME_MUSIC_BEAT_CANVAS_W - (HOME_MUSIC_BEAT_BAR_COUNT * HOME_MUSIC_BEAT_BAR_W)) /
          (HOME_MUSIC_BEAT_BAR_COUNT - 1);

    for (uint32_t i = 0; i < HOME_MUSIC_BEAT_BAR_COUNT; ++i)
    {
        uint8_t idx = (uint8_t)((i + s_home_music.beat_phase) % (sizeof(pattern) / sizeof(pattern[0])));
        int32_t height = HOME_MUSIC_BEAT_MIN_HEIGHT;
        uint32_t x = i * (HOME_MUSIC_BEAT_BAR_W + gap);
        uint32_t y_start;
        uint16_t color = colors[i % (sizeof(colors) / sizeof(colors[0]))];

        if (s_home_music.playing)
        {
            height = pattern[idx];
        }

        if (height < HOME_MUSIC_BEAT_MIN_HEIGHT)
        {
            height = HOME_MUSIC_BEAT_MIN_HEIGHT;
        }
        else if (height > HOME_MUSIC_BEAT_MAX_HEIGHT)
        {
            height = HOME_MUSIC_BEAT_MAX_HEIGHT;
        }

        y_start = HOME_MUSIC_BEAT_CANVAS_H - (uint32_t)height;
        for (uint32_t y = y_start; y < HOME_MUSIC_BEAT_CANVAS_H; ++y)
        {
            for (uint32_t bx = 0; bx < HOME_MUSIC_BEAT_BAR_W && x + bx < HOME_MUSIC_BEAT_CANVAS_W; ++bx)
            {
                buf[y * HOME_MUSIC_BEAT_CANVAS_W + x + bx] = color;
            }
        }
    }

    s_home_music.beat_phase++;
    lv_obj_invalidate(s_home_beat_canvas);
}

/* sin(angle_deg) scaled by 2^LV_TRIGO_SHIFT, with the angle reduced to [0,360)
 * in 32-bit first so large/negative accumulators don't overflow int16_t. */
static int32_t home_wave_sin(int32_t angle_deg)
{
    angle_deg %= 360;
    if (angle_deg < 0)
    {
        angle_deg += 360;
    }
    return lv_trigo_sin((int16_t)angle_deg);
}

/*
 * Call-scenario visualizer: an "electric current" waveform drawn on the same
 * canvas the music beat bars use. Instead of jumping bars, a horizontal trace
 * built from two summed sines scrolls across so it ripples like a live current /
 * scope line. Driven by the same beat_phase counter (advanced once per frame).
 */
static void home_music_apply_wave(void)
{
    const uint16_t bg = home_music_pack_rgb565(0x12, 0x09, 0x1f);
    const uint16_t core = home_music_blend_rgb565(0xd9, 0x46, 0xef, 255);
    const uint16_t glow = home_music_blend_rgb565(0x7c, 0x3a, 0xed, 150);
    const int32_t center = HOME_MUSIC_BEAT_CANVAS_H / 2;
    const int32_t amp1 = (HOME_MUSIC_BEAT_MAX_HEIGHT / 2) - 6;
    const int32_t amp2 = amp1 / 3;
    uint16_t *buf;
    int32_t phase = (int32_t)s_home_music.beat_phase;

    if (!home_music_beat_canvas_ready())
    {
        return;
    }

    buf = (uint16_t *)s_home_beat_canvas_buf;
    if (buf == NULL)
    {
        return;
    }

    for (uint32_t i = 0; i < HOME_MUSIC_BEAT_CANVAS_W * HOME_MUSIC_BEAT_CANVAS_H; ++i)
    {
        buf[i] = bg;
    }

    for (uint32_t x = 0; x < HOME_MUSIC_BEAT_CANVAS_W; ++x)
    {
        int32_t s1 = home_wave_sin((int32_t)x * 5 + phase * 26);
        int32_t s2 = home_wave_sin((int32_t)x * 11 - phase * 34);
        int32_t off = ((s1 * amp1) >> LV_TRIGO_SHIFT) + ((s2 * amp2) >> LV_TRIGO_SHIFT);
        /* Draw the trace and its vertical mirror around the center line so the
         * waveform is symmetric top/bottom. */
        int32_t ys[2] = { center + off, center - off };

        for (uint32_t k = 0; k < 2; ++k)
        {
            for (int32_t t = -1; t <= 1; ++t)
            {
                int32_t yy = ys[k] + t;
                uint16_t c = (t == 0) ? core : glow;

                if (yy < 0 || yy >= HOME_MUSIC_BEAT_CANVAS_H)
                {
                    continue;
                }
                buf[yy * HOME_MUSIC_BEAT_CANVAS_W + x] = c;
            }
        }
    }

    s_home_music.beat_phase++;
    lv_obj_invalidate(s_home_beat_canvas);
}

/*
 * Now-playing panel (song icon + title + artist): shown while music is playing,
 * hidden otherwise. The left panel is the opposite: it takes that space when no
 * music is playing and is hidden while the now-playing panel is up.
 */
static void home_np_panel_sync(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (home_music_obj_valid(ui->home_np_panel))
    {
        if (s_home_music.playing)
        {
            lv_obj_remove_flag(ui->home_np_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui->home_np_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (home_music_obj_valid(ui->home_left_panel))
    {
        if (s_home_music.playing)
        {
            lv_obj_add_flag(ui->home_left_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_remove_flag(ui->home_left_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * Call popup: shown ONLY while an incoming call is ringing. The state text goes
 * to home_cp_title and the remote number to home_cp_num. Answered calls move to
 * home_oncall_card; outgoing / other states show nothing here.
 */
static void home_call_popup_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (!home_music_obj_valid(ui->home_call_popup))
    {
        return;
    }

    if (s_home_music.call_state != HFP_HF_CALL_INCOMING)
    {
        lv_obj_add_flag(ui->home_call_popup, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    home_music_set_label(ui->home_cp_title, HOME_MUSIC_INCOMING_TAG);
    home_music_set_label(ui->home_cp_num,
                         s_home_music.phone_number[0] ? s_home_music.phone_number : "UNKNOWN");
    lv_obj_remove_flag(ui->home_call_popup, LV_OBJ_FLAG_HIDDEN);
}

/*
 * On-call card: shown ONLY once the call is answered (active). Displays the
 * remote number (or a generic status when unknown) plus the elapsed call time.
 * Hidden for every other call state, including incoming/outgoing.
 *
 * The generated home_oc_timer label is registered as a global "digital clock"
 * (shared wall-clock, see basic_callback.c). That is useless as a call timer and
 * would overwrite our text every second, so on the first active tick we detach
 * it from the global clock and drive it ourselves from a per-call tick anchor.
 */
static void home_oncall_card_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    uint32_t elapsed_s;
    char timer_text[16];

    if (!home_music_obj_valid(ui->home_oncall_card))
    {
        return;
    }

    if (s_home_music.call_state != HFP_HF_CALL_ACTIVE)
    {
        s_home_music.call_timing = 0;
        lv_obj_add_flag(ui->home_oncall_card, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!s_home_music.call_timing)
    {
        s_home_music.call_timing = 1;
        s_home_music.call_start_tick = lv_tick_get();
        if (home_music_obj_valid(ui->home_oc_timer))
        {
            lv_digital_clock_unregister(ui->home_oc_timer);
        }
    }

    elapsed_s = lv_tick_elaps(s_home_music.call_start_tick) / 1000U;
    /* Always show HH:MM:SS so the hour field is present from the start. */
    snprintf(timer_text, sizeof(timer_text), "%02lu:%02lu:%02lu",
             (unsigned long)(elapsed_s / 3600U),
             (unsigned long)((elapsed_s % 3600U) / 60U),
             (unsigned long)(elapsed_s % 60U));

    /* Answered call: show a generic status, not the remote number. */
    home_music_set_label(ui->home_oc_title, HOME_MUSIC_ACTIVE_TAG);
    home_music_set_label(ui->home_oc_timer, timer_text);
    lv_obj_remove_flag(ui->home_oncall_card, LV_OBJ_FLAG_HIDDEN);
}

/*
 * Out-call popup: shown ONLY while an outgoing call is dialing/alerting. The
 * state text goes to home_op_title and the remote name/number to home_op_num.
 * Once the call is answered it moves to home_oncall_card; every other state
 * hides this popup.
 */
static void home_outcall_popup_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (!home_music_obj_valid(ui->home_outcall_popup))
    {
        return;
    }

    if (s_home_music.call_state != HFP_HF_CALL_OUTGOING)
    {
        lv_obj_add_flag(ui->home_outcall_popup, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    home_music_set_label(ui->home_op_title, HOME_MUSIC_OUTGOING_TAG);
    home_music_set_label(ui->home_op_num,
                         s_home_music.phone_number[0] ? s_home_music.phone_number : "UNKNOWN");
    lv_obj_remove_flag(ui->home_outcall_popup, LV_OBJ_FLAG_HIDDEN);
}

static void home_music_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    uint32_t progress = 0;
    const char *title = s_home_music.title[0] ? s_home_music.title : HOME_MUSIC_DEFAULT_TITLE;
    const char *artist = s_home_music.artist[0] ? s_home_music.artist : HOME_MUSIC_DEFAULT_ARTIST;

    if (!home_music_obj_valid(ui->home))
    {
        return;
    }

    home_np_panel_sync();
    home_call_popup_apply();
    home_oncall_card_apply();
    home_outcall_popup_apply();

    home_music_set_label(ui->home_song_title, title);
    home_music_set_label(ui->home_song_artist, artist);

    if (!home_call_is_active() &&
        s_home_music.duration_ms > 0 &&
        s_home_music.position_ms < s_home_music.duration_ms)
    {
        progress = (uint32_t)(((uint64_t)s_home_music.position_ms * 100ULL) / s_home_music.duration_ms);
    }
    else if (!home_call_is_active() &&
             s_home_music.duration_ms > 0 &&
             s_home_music.position_ms >= s_home_music.duration_ms)
    {
        progress = 100;
    }

    if (home_music_obj_valid(ui->home_music_prog))
    {
        if (!s_home_music.progress_valid || s_home_music.last_progress != (uint8_t)progress)
        {
            lv_bar_set_value(ui->home_music_prog, (int32_t)progress, LV_ANIM_ON);
            s_home_music.last_progress = (uint8_t)progress;
            s_home_music.progress_valid = 1;
        }
    }

    /* Phone scenario -> electric-current waveform; music/idle -> beat bars. */
    if (home_call_is_active())
    {
        home_music_apply_wave();
    }
    else
    {
        home_music_apply_beat();
    }
}

static void home_music_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_home_music.playing && !home_call_is_active())
    {
        s_home_music.progress_accum_ms += HOME_MUSIC_TIMER_PERIOD_MS;
        while (s_home_music.progress_accum_ms >= 1000)
        {
            s_home_music.progress_accum_ms -= 1000;
            if (s_home_music.duration_ms == 0 ||
                s_home_music.position_ms + 1000 < s_home_music.duration_ms)
            {
                s_home_music.position_ms += 1000;
            }
            else
            {
                s_home_music.position_ms = s_home_music.duration_ms;
            }
        }
    }

    home_music_apply();
}

static void home_music_stop_timer(void)
{
    if (s_music_timer != NULL)
    {
        lv_timer_delete(s_music_timer);
        s_music_timer = NULL;
    }
}

static void home_music_sync_timer(void)
{
    /* Keep the periodic timer running for: music playback progress + beat bars,
     * the elapsed-time counter while a call is active, and the electric-current
     * waveform for any call state (incoming / dialing / active). */
    bool need_timer = s_home_music.playing || home_call_is_active();

    if (need_timer)
    {
        if (s_music_timer == NULL)
        {
            s_music_timer = lv_timer_create(home_music_timer_cb, HOME_MUSIC_TIMER_PERIOD_MS, NULL);
        }
    }
    else
    {
        home_music_stop_timer();
    }
}

static void home_music_update_apply(const home_music_update_async_t *update)
{
    bool track_changed = false;

    if (update == NULL)
    {
        return;
    }

    /* Some phones push live lyrics through the TITLE attribute and fire a
     * TRACK_CHANGED for every line, so the title is NOT a reliable "new song"
     * signal. Use the playing-time (duration) change instead: it stays constant
     * within a song but differs between tracks. The precise position is then
     * corrected by the periodic AVRCP PLAY_POS notifications anyway. */
    if (update->has_duration &&
        update->duration_ms != 0 &&
        s_home_music.duration_ms != 0 &&
        update->duration_ms != s_home_music.duration_ms)
    {
        track_changed = true;
    }

    if (update->has_title && update->title[0] != '\0')
    {
        snprintf(s_home_music.title, sizeof(s_home_music.title), "%s", update->title);
    }
    if (update->has_artist && update->artist[0] != '\0')
    {
        snprintf(s_home_music.artist, sizeof(s_home_music.artist), "%s", update->artist);
    }
    if (update->has_duration)
    {
        s_home_music.duration_ms = update->duration_ms;
    }
    if (update->has_position)
    {
        s_home_music.position_ms = update->position_ms;
    }
    if (update->has_playing)
    {
        s_home_music.playing = update->playing ? 1 : 0;
    }

    if (track_changed)
    {
        s_home_music.position_ms = 0;
        s_home_music.last_progress = 0;
        s_home_music.progress_valid = 0;
    }

    s_home_music.progress_accum_ms = 0;
    home_call_blink_sync();

    home_music_sync_timer();
    home_music_apply();
}

static void home_music_phone_apply(const home_music_phone_async_t *update)
{
    if (update == NULL)
    {
        return;
    }

    s_home_music.call_state = update->state;
    if (update->number[0] != '\0')
    {
        snprintf(s_home_music.phone_number, sizeof(s_home_music.phone_number), "%s", update->number);
    }
    else if (update->state == HFP_HF_CALL_NONE)
    {
        s_home_music.phone_number[0] = '\0';
    }
    if (home_call_is_active())
    {
        s_home_music.playing = 0;
        s_home_music.progress_accum_ms = 0;
    }

    home_call_blink_sync();
    home_music_sync_timer();
    home_music_apply();
    home_call_nav_sync();
}

/*
 * Sync the locally interpolated playback position with the real value reported
 * by the phone via AVRCP PLAY_POS_CHANGED. The 1-second timer keeps advancing
 * the bar smoothly between these periodic corrections.
 */
static void home_music_position_apply(uint32_t position_ms)
{
    if (!home_call_is_active())
    {
        s_home_music.position_ms = position_ms;
        if (s_home_music.duration_ms > 0 &&
            s_home_music.position_ms > s_home_music.duration_ms)
        {
            s_home_music.position_ms = s_home_music.duration_ms;
        }
        s_home_music.progress_accum_ms = 0;

        home_music_sync_timer();
        home_music_apply();
    }
}

static bk_err_t home_music_pending_init(void)
{
    if (s_home_music_pending_mutex != NULL)
    {
        return BK_OK;
    }

    return rtos_init_mutex(&s_home_music_pending_mutex);
}

static bool home_music_pending_lock(void)
{
    return s_home_music_pending_mutex != NULL &&
           rtos_lock_mutex(&s_home_music_pending_mutex) == BK_OK;
}

static void home_music_pending_unlock(void)
{
    (void)rtos_unlock_mutex(&s_home_music_pending_mutex);
}

static void home_music_pending_publish(const home_music_update_async_t *update)
{
    home_music_update_async_t *pending;

    if (update == NULL || !home_music_pending_lock())
    {
        return;
    }

    pending = &s_home_music_pending.music;
    if (update->has_title)
    {
        snprintf(pending->title, sizeof(pending->title), "%s", update->title);
        pending->has_title = 1;
    }
    if (update->has_artist)
    {
        snprintf(pending->artist, sizeof(pending->artist), "%s", update->artist);
        pending->has_artist = 1;
    }
    if (update->has_duration)
    {
        pending->duration_ms = update->duration_ms;
        pending->has_duration = 1;
    }
    if (update->has_position)
    {
        pending->position_ms = update->position_ms;
        pending->has_position = 1;
        s_home_music_pending.has_play_position = 0;
    }
    if (update->has_playing)
    {
        pending->playing = update->playing;
        pending->has_playing = 1;
    }
    s_home_music_pending.has_music = 1;

    home_music_pending_unlock();
}

static void home_music_pending_publish_position(uint32_t position_ms)
{
    if (!home_music_pending_lock())
    {
        return;
    }

    s_home_music_pending.play_position_ms = position_ms;
    s_home_music_pending.has_play_position = 1;
    home_music_pending_unlock();
}

static void home_music_pending_publish_phone(const home_music_phone_async_t *update)
{
    if (update == NULL || !home_music_pending_lock())
    {
        return;
    }

    s_home_music_pending.phone = *update;
    s_home_music_pending.has_phone = 1;
    home_music_pending_unlock();
}

/*
 * LVGL-thread consumer. When LVGL is stopped this function cannot run, but
 * producers only overwrite the fixed-size snapshot above, so memory use stays
 * bounded. The newest snapshot is applied when the home page resumes.
 */
static void home_music_pending_apply(void)
{
    home_music_pending_t pending = {0};

    if (!home_music_pending_lock())
    {
        return;
    }

    pending = s_home_music_pending;
    os_memset(&s_home_music_pending, 0, sizeof(s_home_music_pending));
    home_music_pending_unlock();

    if (pending.has_music)
    {
        home_music_update_apply(&pending.music);
    }
    if (pending.has_phone)
    {
        home_music_phone_apply(&pending.phone);
    }
    if (pending.has_play_position)
    {
        home_music_position_apply(pending.play_position_ms);
    }
}

static uint32_t home_music_parse_u32_text(const char *text)
{
    uint32_t value = 0;

    if (text == NULL)
    {
        return 0;
    }

    for (size_t i = 0; text[i] != '\0'; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            continue;
        }

        if (value > (UINT32_MAX - 9U) / 10U)
        {
            return UINT32_MAX;
        }

        value = value * 10U + (uint32_t)(text[i] - '0');
    }

    return value;
}

static size_t home_music_utf8_put(char *dst, size_t dst_size, size_t pos, uint32_t cp)
{
    if (dst == NULL || dst_size == 0 || pos >= dst_size)
    {
        return pos;
    }

    if (cp <= 0x7FU)
    {
        if (pos + 1U >= dst_size)
        {
            return pos;
        }
        dst[pos++] = (char)cp;
    }
    else if (cp <= 0x7FFU)
    {
        if (pos + 2U >= dst_size)
        {
            return pos;
        }
        dst[pos++] = (char)(0xC0U | (cp >> 6));
        dst[pos++] = (char)(0x80U | (cp & 0x3FU));
    }
    else
    {
        if (pos + 3U >= dst_size)
        {
            return pos;
        }
        dst[pos++] = (char)(0xE0U | (cp >> 12));
        dst[pos++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        dst[pos++] = (char)(0x80U | (cp & 0x3FU));
    }

    dst[pos] = '\0';
    return pos;
}

static void home_music_copy_utf16_attr_text(char *dst,
                                            size_t dst_size,
                                            const uint8_t *src,
                                            uint32_t src_len,
                                            uint8_t little_endian)
{
    size_t pos = 0;

    if (dst == NULL || dst_size == 0 || src == NULL)
    {
        return;
    }

    dst[0] = '\0';
    for (uint32_t i = 0; i + 1U < src_len; i += 2U)
    {
        uint32_t cp;

        if (little_endian)
        {
            cp = (uint32_t)src[i] | ((uint32_t)src[i + 1U] << 8);
        }
        else
        {
            cp = ((uint32_t)src[i] << 8) | (uint32_t)src[i + 1U];
        }

        if (cp == 0)
        {
            break;
        }

        if (cp >= 0xD800U && cp <= 0xDFFFU)
        {
            cp = '?';
        }

        pos = home_music_utf8_put(dst, dst_size, pos, cp);
    }
}

static void home_music_copy_avrcp_attr_text(char *dst, size_t dst_size, const a2dp_sink_avrcp_attr_t *attr)
{
    uint32_t copy_len;

    if (dst == NULL || dst_size == 0 || attr == NULL || attr->attr_text == NULL || attr->attr_length == 0)
    {
        return;
    }

    switch (attr->attr_text_charset)
    {
    case 0x03E8: /* UCS-2 */
    case 0x03F5: /* UTF-16BE */
    case 0x03F7: /* UTF-16 */
        home_music_copy_utf16_attr_text(dst, dst_size, attr->attr_text, attr->attr_length, 0);
        return;

    case 0x03F6: /* UTF-16LE */
        home_music_copy_utf16_attr_text(dst, dst_size, attr->attr_text, attr->attr_length, 1);
        return;

    default:
        break;
    }

    copy_len = attr->attr_length;
    if (copy_len >= dst_size)
    {
        copy_len = dst_size - 1;
    }

    os_memcpy(dst, attr->attr_text, copy_len);
    dst[copy_len] = '\0';
}

static void home_music_handle_avrcp_attr_rsp(const a2dp_sink_avrcp_elem_attr_msg_t *rsp)
{
    home_music_update_async_t update = {0};
    uint8_t has_update = 0;

    if (rsp == NULL)
    {
        return;
    }

    for (uint32_t i = 0; i < rsp->attr_count; ++i)
    {
        const a2dp_sink_avrcp_attr_t *attr = &rsp->attr_array[i];

        switch (attr->attr_id)
        {
        case BK_AVRCP_MEDIA_ATTR_ID_TITLE:
            home_music_copy_avrcp_attr_text(update.title, sizeof(update.title), attr);
            update.has_title = update.title[0] != '\0';
            has_update |= update.has_title;
            break;

        case BK_AVRCP_MEDIA_ATTR_ID_ARTIST:
            home_music_copy_avrcp_attr_text(update.artist, sizeof(update.artist), attr);
            update.has_artist = update.artist[0] != '\0';
            has_update |= update.has_artist;
            break;

        case BK_AVRCP_MEDIA_ATTR_ID_PLAYING_TIME:
        {
            char duration_text[24] = {0};
            home_music_copy_avrcp_attr_text(duration_text, sizeof(duration_text), attr);
            update.duration_ms = home_music_parse_u32_text(duration_text);
            update.has_duration = 1;
            has_update = 1;
            break;
        }

        default:
            break;
        }
    }

    if (has_update)
    {
        home_music_pending_publish(&update);
    }
}

static void home_music_update(const char *title,
                              const char *artist,
                              uint32_t duration_ms,
                              uint32_t position_ms,
                              uint8_t playing)
{
    home_music_update_async_t update = {0};

    if (title != NULL)
    {
        snprintf(update.title, sizeof(update.title), "%s", title);
        update.has_title = 1;
    }
    if (artist != NULL)
    {
        snprintf(update.artist, sizeof(update.artist), "%s", artist);
        update.has_artist = 1;
    }
    update.duration_ms = duration_ms;
    update.position_ms = position_ms;
    update.playing = playing ? 1 : 0;
    update.has_duration = 1;
    update.has_position = 1;
    update.has_playing = 1;

    home_music_pending_publish(&update);
}

static void home_music_set_playing(uint8_t playing)
{
    home_music_update_async_t update = {
        .playing = playing ? 1 : 0,
        .has_playing = 1,
    };

    home_music_pending_publish(&update);
}

static void home_music_set_position(uint32_t position_ms)
{
    home_music_pending_publish_position(position_ms);
}

static void home_phone_update(hfp_hf_call_state_t state, const char *number)
{
    home_music_phone_async_t update = {0};

    if (number != NULL)
    {
        snprintf(update.number, sizeof(update.number), "%s", number);
    }
    update.state = state;

    home_music_pending_publish_phone(&update);
}

static void home_ui_bt_a2dp_event_cb(a2dp_sink_ui_event_t event,
                                     const void *event_data,
                                     void *user_data)
{
    (void)user_data;

    switch (event)
    {
    case A2DP_SINK_UI_EVT_TRACK_CHANGED:
        /* Do not rewind here: the demo follows up with a track-attr request and
         * the position is only reset once the new title/artist actually arrives
         * (see home_music_update_apply). This avoids the bar jumping back to
         * 0 on the repeated TRACK_CHANGED notifications phones emit. */
        break;

    case A2DP_SINK_UI_EVT_PLAY_STATUS_CHANGED:
        if (event_data != NULL)
        {
            uint8_t play_status = *(const uint8_t *)event_data;
            home_music_set_playing(play_status == BK_AVRCP_PLAYBACK_PLAYING);
        }
        break;

    case A2DP_SINK_UI_EVT_ELEM_ATTR_RSP:
        home_music_handle_avrcp_attr_rsp((const a2dp_sink_avrcp_elem_attr_msg_t *)event_data);
        break;

    case A2DP_SINK_UI_EVT_PLAY_POS:
        if (event_data != NULL)
        {
            home_music_set_position(*(const uint32_t *)event_data);
        }
        break;

    case A2DP_SINK_UI_EVT_DISCONNECTED:
        home_music_update(NULL, NULL, 0, 0, 0);
        break;

    default:
        break;
    }
}

static void home_ui_bt_phone_update_cb(hfp_hf_call_state_t state, const char *number, void *user_data)
{
    (void)user_data;
    home_phone_update(state, number);
}

void home_ui_register_bt_callbacks(void)
{
    const a2dp_sink_ui_callback_t a2dp_callbacks = {
        .event = home_ui_bt_a2dp_event_cb,
        .user_data = NULL,
    };
    const hfp_hf_ui_callback_t hfp_callbacks = {
        .phone_update = home_ui_bt_phone_update_cb,
        .user_data = NULL,
    };

    if (home_music_pending_init() != BK_OK)
    {
        BK_LOGE("home_ui", "init Bluetooth UI state mutex failed\n");
    }

    a2dp_sink_demo_register_ui_callback(&a2dp_callbacks);
    hfp_hf_demo_register_ui_callback(&hfp_callbacks);
}

/*
 * Physical phone-control key (GPIO_5) hooks, called from key_app_service.c:
 * short press answers, double press hangs up / rejects. These issue the HFP
 * command directly (thread-safe, just posts a BT message), so no LVGL context is
 * needed here; the popup / on-call card update when the resulting call-state
 * change is reported back through home_phone_update(). On-screen call buttons are
 * intentionally not wired up: call control is handled by the physical key.
 */
void phone_key_answer(void)
{
    hfp_demo_answer(1);
}

void phone_key_hangup(void)
{
    hfp_demo_answer(0);
}

/* Create the gauge timer (idempotent). */
void home_ui_enter(void)
{
    if (s_speed_timer)
    {
        lv_timer_delete(s_speed_timer);
        s_speed_timer = NULL;
    }

    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    if (!ui->home || !lv_obj_is_valid(ui->home))
    {
        return;
    }

    s_speed_value = 0;
    s_speed_dir = 1;
    s_sys_value = 0;
    s_sys_dir = 1;
    /* The designer sets home_sys_unit to "RPM ×100", but its font
     * (pingfang_SC_16) only covers ASCII 0x20-0x7F, so U+00D7 '×' renders blank.
     * Replace it with an ASCII 'x' so the multiplier shows. Kept here so a
     * home_init.c regenerate cannot reintroduce the missing glyph. */
    if (home_music_obj_valid(ui->home_sys_unit))
    {
        lv_label_set_text(ui->home_sys_unit, "RPM x100");
    }
    if (ui->home_speed_scale_needle_0 != NULL && lv_obj_is_valid(ui->home_speed_scale_needle_0))
    {
        lv_obj_move_foreground(ui->home_speed_scale_needle_0);
    }
    if (ui->home_sys_scale_needle_0 != NULL && lv_obj_is_valid(ui->home_sys_scale_needle_0))
    {
        lv_obj_move_foreground(ui->home_sys_scale_needle_0);
    }
    speed_gauge_apply();
    s_speed_timer = lv_timer_create(speed_gauge_timer_cb, SPEED_ANIM_PERIOD_MS, NULL);

    /* If we re-entered mid-call, the page tree was rebuilt and home_oc_timer got
     * re-registered into the global digital clock (home_init.c). Detach it again
     * so our per-call elapsed counter (anchored at call_start_tick, preserved
     * across the reload) is not overwritten by the shared wall clock. */
    if (s_home_music.call_state == HFP_HF_CALL_ACTIVE &&
        home_music_obj_valid(ui->home_oc_timer))
    {
        lv_digital_clock_unregister(ui->home_oc_timer);
    }

    /* The designer assigns Latin-only Montserrat to the song title/artist, so
     * Chinese track metadata renders blank. Load a TrueType font from the SD
     * card once and apply it so arbitrary Chinese displays. Kept here (not in
     * the generated home_init.c) so a future regenerate does not clobber it. */
    if (s_cn_font == NULL)
    {
        s_cn_font = home_cn_font_load();
        if (s_cn_font == NULL)
        {
            BK_LOGE("home_ui", "load %s failed, CN glyphs stay blank\n", HOME_CN_TTF_PATH);
        }
    }
    if (s_cn_font != NULL)
    {
        if (home_music_obj_valid(ui->home_song_title))
        {
            lv_obj_set_style_text_font(ui->home_song_title, s_cn_font,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (home_music_obj_valid(ui->home_song_artist))
        {
            lv_obj_set_style_text_font(ui->home_song_artist, s_cn_font,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        /* Incoming-call caller name may be Chinese (resolved via PBAP), so it
         * needs the CJK TTF too; the generated Montserrat has no CJK glyphs. */
        if (home_music_obj_valid(ui->home_cp_num))
        {
            lv_obj_set_style_text_font(ui->home_cp_num, s_cn_font,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        /* Out-call callee name may likewise be a Chinese PBAP contact name. */
        if (home_music_obj_valid(ui->home_op_num))
        {
            lv_obj_set_style_text_font(ui->home_op_num, s_cn_font,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    s_home_music.progress_valid = 0;
    home_music_pending_apply();
    home_music_apply();
    home_music_sync_timer();
    home_call_blink_sync();

    /* Bind the call buttons on the freshly-built page and re-engage the keypad
     * call modal if we re-entered mid-call. */
    home_call_bind_buttons();
    home_call_nav_sync();
}

/* Delete the gauge timer. */
void home_ui_leave(void)
{
    if (s_speed_timer)
    {
        lv_timer_delete(s_speed_timer);
        s_speed_timer = NULL;
    }
    home_music_stop_timer();
    home_call_blink_stop();

    /* Drop any call modal so it cannot hijack the next page's keypad. The call
     * buttons belong to the home tree (freed on unload); a re-enter re-engages
     * the modal via home_ui_enter when the call is still up. */
    beken_ui_keypad_set_modal_group(NULL);
}

/*
 * The page manager is about to lv_obj_delete() the home screen. Deleting the
 * screen tree also deletes our child canvases, but the static handles to them
 * are not cleared automatically and the canvas backing buffers are user-owned
 * (LVGL never frees a lv_canvas_set_buffer() pointer). Stop the timers, null the
 * handles so nothing dangles, and free the buffers so nothing leaks. The next
 * home entry lazily recreates the screen (init_page_home + home_ui_install_bg).
 *
 * Note: the preloaded background path (beken_ui_install_preloaded_bg) wraps a
 * globally-owned blob and leaves s_bg_canvas_buf NULL, so only the fallback
 * JPEG-decode buffer (psram_malloc) is freed here.
 */
void home_ui_unload(void)
{
    home_ui_leave();

    if (s_nav_group != NULL)
    {
        lv_group_remove_all_objs(s_nav_group);
    }

    s_bg_canvas = NULL;
    if (s_bg_canvas_buf != NULL)
    {
        psram_free(s_bg_canvas_buf);
        s_bg_canvas_buf = NULL;
    }

    s_home_beat_canvas = NULL;
    if (s_home_beat_canvas_buf != NULL)
    {
        psram_free(s_home_beat_canvas_buf);
        s_home_beat_canvas_buf = NULL;
    }
}
