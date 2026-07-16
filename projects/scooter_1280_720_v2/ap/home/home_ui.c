/*
 * Home page logic, split out of the generated beken_ui.c.
 *
 * Contains the home page's own behavior only (speed gauge, hazard blink, the
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

/* ---------- Speed gauge: needle sweeps 0->60->0, the speed number tracks it ----------
 * Mirrors scooter_1280_720: drive lv_scale's line needle + the numeric label from a
 * single timer. Scale range is 0..60.
 */
#define SPEED_NEEDLE_LENGTH    170
#define SPEED_SCALE_MAX        60
#define SPEED_ANIM_PERIOD_MS   50
#define SPEED_ANIM_STEP        1
static int32_t s_speed_value = 0;
static int32_t s_speed_dir = 1;
static lv_timer_t *s_speed_timer = NULL;
static lv_timer_t *s_music_timer = NULL;

#define HOME_MUSIC_DEFAULT_TITLE       "Electric Dreams"
#define HOME_MUSIC_DEFAULT_ARTIST      "Neon Rider"
#define HOME_MUSIC_DEFAULT_TAG         "NOW PLAYING"
#define HOME_MUSIC_DIALING_TAG         "NOW DAILING"
#define HOME_MUSIC_TIMER_PERIOD_MS     200
#define HOME_MUSIC_BEAT_MIN_HEIGHT     6
#define HOME_MUSIC_BEAT_MAX_HEIGHT     26
#define HOME_MUSIC_PANEL_X             435
#define HOME_MUSIC_PANEL_Y             10
#define HOME_MUSIC_BEAT_CANVAS_X       (HOME_MUSIC_PANEL_X + 20)
#define HOME_MUSIC_BEAT_CANVAS_Y       (HOME_MUSIC_PANEL_Y + 70)
#define HOME_MUSIC_BEAT_CANVAS_W       310
#define HOME_MUSIC_BEAT_CANVAS_H       26
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
    uint8_t phone_active;
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
    uint8_t active;
} home_music_phone_async_t;

static home_music_state_t s_home_music = {
    .title = HOME_MUSIC_DEFAULT_TITLE,
    .artist = HOME_MUSIC_DEFAULT_ARTIST,
};
static lv_obj_t *s_home_beat_canvas = NULL;
static void *s_home_beat_canvas_buf = NULL;

static void home_music_apply(void);

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
    lv_label_set_text_fmt(ui->home_speed_val, "%" LV_PRId32, s_speed_value);
}

static void speed_gauge_timer_cb(lv_timer_t *timer)
{
    (void)timer;

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
    speed_gauge_apply();
}

/* ---------- Hazard (double-flash): blink both turn indicators together ----------
 * scooter_1280_720 blinks a turn light by toggling image opacity on a 500ms timer.
 * Hazard = left + right flashing in sync, so toggle both home_ic_left/right opa.
 */
#define HAZARD_BLINK_PERIOD_MS  500
static bool s_hazard_on = true;
static lv_timer_t *s_hazard_timer = NULL;

static void hazard_blink_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_opa_t opa = s_hazard_on ? LV_OPA_COVER : LV_OPA_TRANSP;

    if (!ui->home || !lv_obj_is_valid(ui->home))
    {
        return;
    }

    if (ui->home_ic_left && lv_obj_is_valid(ui->home_ic_left))
    {
        lv_obj_set_style_image_opa(ui->home_ic_left, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui->home_ic_right && lv_obj_is_valid(ui->home_ic_right))
    {
        lv_obj_set_style_image_opa(ui->home_ic_right, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void hazard_blink_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_hazard_on = !s_hazard_on;
    hazard_blink_apply();
}

static bool home_music_obj_valid(lv_obj_t *obj)
{
    return obj != NULL && lv_obj_is_valid(obj);
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
 * canvas bars match the semi-transparent home_beat* objects in home_init.c. */
static uint16_t home_music_blend_rgb565(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t bg_r = 0x12, bg_g = 0x09, bg_b = 0x1f;
    uint8_t or = (uint8_t)(((uint16_t)r * a + (uint16_t)bg_r * (255U - a)) / 255U);
    uint8_t og = (uint8_t)(((uint16_t)g * a + (uint16_t)bg_g * (255U - a)) / 255U);
    uint8_t ob = (uint8_t)(((uint16_t)b * a + (uint16_t)bg_b * (255U - a)) / 255U);

    return home_music_pack_rgb565(or, og, ob);
}

static void home_music_hide_beat_objects(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *bars[] = {
        ui->home_beat01, ui->home_beat02, ui->home_beat03, ui->home_beat04, ui->home_beat05,
        ui->home_beat06, ui->home_beat07, ui->home_beat08, ui->home_beat09, ui->home_beat10,
        ui->home_beat11, ui->home_beat12, ui->home_beat13, ui->home_beat14, ui->home_beat15,
        ui->home_beat16, ui->home_beat17, ui->home_beat18, ui->home_beat19, ui->home_beat20,
    };

    if (home_music_obj_valid(ui->home_beat_panel))
    {
        lv_obj_add_flag(ui->home_beat_panel, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i)
    {
        if (home_music_obj_valid(bars[i]))
        {
            lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
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
        s_home_beat_canvas_buf = os_malloc(buf_size);
        if (s_home_beat_canvas_buf == NULL)
        {
            return false;
        }
    }

    s_home_beat_canvas = lv_canvas_create(ui->home_bottom_bar);
    if (s_home_beat_canvas == NULL)
    {
        os_free(s_home_beat_canvas_buf);
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
    home_music_hide_beat_objects();
    return true;
}

static void home_music_apply_beat(void)
{
    static const uint8_t pattern[] = {4, 14, 7, 20, 10, 24, 8, 17, 9, 22, 6, 18, 12, 23, 8, 16, 10, 21, 7, 18};
    const uint16_t bg = home_music_pack_rgb565(0x12, 0x09, 0x1f);
    /* Mirror the 6-color palette + per-bar opacity of home_beat01..home_beat20. */
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

    if (s_home_music.phone_active)
    {
        title = s_home_music.phone_number[0] ? s_home_music.phone_number : "UNKNOWN NUMBER";
        artist = "PHONE CALL";
        home_music_set_label(ui->home_music_tag, HOME_MUSIC_DIALING_TAG);
    }
    else
    {
        home_music_set_label(ui->home_music_tag, HOME_MUSIC_DEFAULT_TAG);
    }

    home_music_set_label(ui->home_song_title, title);
    home_music_set_label(ui->home_song_artist, artist);

    if (!s_home_music.phone_active &&
        s_home_music.duration_ms > 0 &&
        s_home_music.position_ms < s_home_music.duration_ms)
    {
        progress = (uint32_t)(((uint64_t)s_home_music.position_ms * 100ULL) / s_home_music.duration_ms);
    }
    else if (!s_home_music.phone_active &&
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

    home_music_apply_beat();
}

static void home_music_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_home_music.playing && !s_home_music.phone_active)
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
    if (s_home_music.playing && !s_home_music.phone_active)
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

static void home_music_update_async_cb(void *user_data)
{
    home_music_update_async_t *update = (home_music_update_async_t *)user_data;
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
    s_home_music.phone_active = 0;

    home_music_sync_timer();
    home_music_apply();
    os_free(update);
}

static void home_music_phone_async_cb(void *user_data)
{
    home_music_phone_async_t *update = (home_music_phone_async_t *)user_data;

    if (update == NULL)
    {
        return;
    }

    s_home_music.phone_active = update->active ? 1 : 0;
    if (update->number[0] != '\0')
    {
        snprintf(s_home_music.phone_number, sizeof(s_home_music.phone_number), "%s", update->number);
    }
    if (s_home_music.phone_active)
    {
        s_home_music.playing = 0;
        s_home_music.progress_accum_ms = 0;
    }

    home_music_sync_timer();
    home_music_apply();
    os_free(update);
}

/*
 * Sync the locally interpolated playback position with the real value reported
 * by the phone via AVRCP PLAY_POS_CHANGED. The 1-second timer keeps advancing
 * the bar smoothly between these periodic corrections.
 */
static void home_music_position_async_cb(void *user_data)
{
    uint32_t *position_ms = (uint32_t *)user_data;

    if (position_ms == NULL)
    {
        return;
    }

    if (!s_home_music.phone_active)
    {
        s_home_music.position_ms = *position_ms;
        if (s_home_music.duration_ms > 0 &&
            s_home_music.position_ms > s_home_music.duration_ms)
        {
            s_home_music.position_ms = s_home_music.duration_ms;
        }
        s_home_music.progress_accum_ms = 0;

        home_music_sync_timer();
        home_music_apply();
    }

    os_free(position_ms);
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
    home_music_update_async_t *update;
    uint8_t has_update = 0;

    if (rsp == NULL)
    {
        return;
    }

    update = (home_music_update_async_t *)os_zalloc(sizeof(*update));
    if (update == NULL)
    {
        return;
    }

    for (uint32_t i = 0; i < rsp->attr_count; ++i)
    {
        const a2dp_sink_avrcp_attr_t *attr = &rsp->attr_array[i];

        switch (attr->attr_id)
        {
        case BK_AVRCP_MEDIA_ATTR_ID_TITLE:
            home_music_copy_avrcp_attr_text(update->title, sizeof(update->title), attr);
            update->has_title = update->title[0] != '\0';
            has_update |= update->has_title;
            break;

        case BK_AVRCP_MEDIA_ATTR_ID_ARTIST:
            home_music_copy_avrcp_attr_text(update->artist, sizeof(update->artist), attr);
            update->has_artist = update->artist[0] != '\0';
            has_update |= update->has_artist;
            break;

        case BK_AVRCP_MEDIA_ATTR_ID_PLAYING_TIME:
        {
            char duration_text[24] = {0};
            home_music_copy_avrcp_attr_text(duration_text, sizeof(duration_text), attr);
            update->duration_ms = home_music_parse_u32_text(duration_text);
            update->has_duration = 1;
            has_update = 1;
            break;
        }

        default:
            break;
        }
    }

    if (has_update)
    {
        lv_async_call(home_music_update_async_cb, update);
    }
    else
    {
        os_free(update);
    }
}

static void home_music_update(const char *title,
                              const char *artist,
                              uint32_t duration_ms,
                              uint32_t position_ms,
                              uint8_t playing)
{
    home_music_update_async_t *update = (home_music_update_async_t *)os_zalloc(sizeof(*update));

    if (update == NULL)
    {
        return;
    }

    if (title != NULL)
    {
        snprintf(update->title, sizeof(update->title), "%s", title);
        update->has_title = 1;
    }
    if (artist != NULL)
    {
        snprintf(update->artist, sizeof(update->artist), "%s", artist);
        update->has_artist = 1;
    }
    update->duration_ms = duration_ms;
    update->position_ms = position_ms;
    update->playing = playing ? 1 : 0;
    update->has_duration = 1;
    update->has_position = 1;
    update->has_playing = 1;

    lv_async_call(home_music_update_async_cb, update);
}

static void home_music_set_position(uint32_t position_ms)
{
    uint32_t *async_pos = (uint32_t *)os_zalloc(sizeof(*async_pos));

    if (async_pos == NULL)
    {
        return;
    }

    *async_pos = position_ms;
    lv_async_call(home_music_position_async_cb, async_pos);
}

static void home_phone_update(const char *number, uint8_t active)
{
    home_music_phone_async_t *update = (home_music_phone_async_t *)os_zalloc(sizeof(*update));

    if (update == NULL)
    {
        return;
    }

    if (number != NULL)
    {
        snprintf(update->number, sizeof(update->number), "%s", number);
    }
    update->active = active ? 1 : 0;

    lv_async_call(home_music_phone_async_cb, update);
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
         * (see home_music_update_async_cb). This avoids the bar jumping back to
         * 0 on the repeated TRACK_CHANGED notifications phones emit. */
        break;

    case A2DP_SINK_UI_EVT_PLAY_STATUS_CHANGED:
        if (event_data != NULL)
        {
            uint8_t play_status = *(const uint8_t *)event_data;
            home_music_update(NULL,
                              NULL,
                              s_home_music.duration_ms,
                              s_home_music.position_ms,
                              play_status == BK_AVRCP_PLAYBACK_PLAYING);
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

static void home_ui_bt_phone_update_cb(const char *number, uint8_t active, void *user_data)
{
    (void)user_data;
    home_phone_update(number, active);
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

    a2dp_sink_demo_register_ui_callback(&a2dp_callbacks);
    hfp_hf_demo_register_ui_callback(&hfp_callbacks);
}

/* Create the gauge + hazard timers (idempotent). */
void home_ui_enter(void)
{
    if (s_speed_timer)
    {
        lv_timer_delete(s_speed_timer);
        s_speed_timer = NULL;
    }
    if (s_hazard_timer)
    {
        lv_timer_delete(s_hazard_timer);
        s_hazard_timer = NULL;
    }

    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    if (!ui->home || !lv_obj_is_valid(ui->home))
    {
        return;
    }

    s_speed_value = 0;
    s_speed_dir = 1;
    if (ui->home_speed_scale_needle_0 != NULL && lv_obj_is_valid(ui->home_speed_scale_needle_0))
    {
        lv_obj_move_foreground(ui->home_speed_scale_needle_0);
    }
    speed_gauge_apply();
    s_speed_timer = lv_timer_create(speed_gauge_timer_cb, SPEED_ANIM_PERIOD_MS, NULL);

    s_hazard_on = true;
    hazard_blink_apply();
    s_hazard_timer = lv_timer_create(hazard_blink_timer_cb, HAZARD_BLINK_PERIOD_MS, NULL);

    s_home_music.progress_valid = 0;
    home_music_apply();
    home_music_sync_timer();
}

/* Delete the gauge + hazard timers. */
void home_ui_leave(void)
{
    if (s_speed_timer)
    {
        lv_timer_delete(s_speed_timer);
        s_speed_timer = NULL;
    }
    if (s_hazard_timer)
    {
        lv_timer_delete(s_hazard_timer);
        s_hazard_timer = NULL;
    }
    home_music_stop_timer();
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

    s_bg_canvas = NULL;
    if (s_bg_canvas_buf != NULL)
    {
        psram_free(s_bg_canvas_buf);
        s_bg_canvas_buf = NULL;
    }

    s_home_beat_canvas = NULL;
    if (s_home_beat_canvas_buf != NULL)
    {
        os_free(s_home_beat_canvas_buf);
        s_home_beat_canvas_buf = NULL;
    }
}
