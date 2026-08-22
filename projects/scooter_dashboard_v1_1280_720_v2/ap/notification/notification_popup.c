#include "notification_popup.h"

#include <stdbool.h>
#include <stdio.h>
#include <os/os.h>
#include "lvgl.h"

#define NOTIFICATION_TITLE_LEN       64
#define NOTIFICATION_MESSAGE_LEN     256
#define NOTIFICATION_SHOW_MS         5000
#define NOTIFICATION_POLL_MS         200

typedef struct
{
    char title[NOTIFICATION_TITLE_LEN];
    char message[NOTIFICATION_MESSAGE_LEN];
    bool pending;
} notification_pending_t;

static notification_pending_t s_notification_pending;
static beken_mutex_t s_notification_mutex = NULL;
static lv_timer_t *s_notification_timer = NULL;
static lv_obj_t *s_notification_popup = NULL;
static lv_obj_t *s_notification_title = NULL;
static lv_obj_t *s_notification_message = NULL;
static lv_font_t *s_notification_font = NULL;
static uint32_t s_notification_show_tick;

static void notification_popup_create(void)
{
    lv_obj_t *accent;

    s_notification_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_notification_popup, 760, 112);
    lv_obj_align(s_notification_popup, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_remove_flag(s_notification_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_notification_popup, lv_color_hex(0x1B2028), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_notification_popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_notification_popup, lv_color_hex(0x3A4553), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_notification_popup, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_notification_popup, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_notification_popup, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_notification_popup, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_notification_popup, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_notification_popup, 0, LV_PART_MAIN);

    accent = lv_obj_create(s_notification_popup);
    lv_obj_set_size(accent, 4, 78);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_remove_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x32A8FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(accent, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(accent, 0, LV_PART_MAIN);

    s_notification_title = lv_label_create(s_notification_popup);
    lv_obj_set_size(s_notification_title, 700, 38);
    lv_obj_set_pos(s_notification_title, 28, 8);
    lv_obj_set_style_text_color(s_notification_title, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(s_notification_title, LV_LABEL_LONG_DOT);

    s_notification_message = lv_label_create(s_notification_popup);
    lv_obj_set_size(s_notification_message, 700, 38);
    lv_obj_set_pos(s_notification_message, 28, 60);
    lv_obj_set_style_text_color(s_notification_message, lv_color_hex(0xC8D0DA), LV_PART_MAIN);
    lv_label_set_long_mode(s_notification_message, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(s_notification_popup, LV_OBJ_FLAG_HIDDEN);
}

static void notification_popup_timer_cb(lv_timer_t *timer)
{
    notification_pending_t notification = {0};
    bool has_notification = false;
    (void)timer;

    if (s_notification_mutex && rtos_lock_mutex(&s_notification_mutex) == BK_OK)
    {
        if (s_notification_pending.pending)
        {
            notification = s_notification_pending;
            s_notification_pending.pending = false;
            has_notification = true;
        }
        rtos_unlock_mutex(&s_notification_mutex);
    }

    if (s_notification_popup && !lv_obj_is_valid(s_notification_popup))
    {
        s_notification_popup = NULL;
        s_notification_title = NULL;
        s_notification_message = NULL;
    }

    if (has_notification)
    {
        if (!s_notification_popup)
        {
            notification_popup_create();
        }
        if (s_notification_font)
        {
            lv_obj_set_style_text_font(s_notification_title, s_notification_font, LV_PART_MAIN);
            lv_obj_set_style_text_font(s_notification_message, s_notification_font, LV_PART_MAIN);
        }
        lv_label_set_text(s_notification_title, notification.title);
        lv_label_set_text(s_notification_message, notification.message);
        lv_obj_move_foreground(s_notification_popup);
        lv_obj_remove_flag(s_notification_popup, LV_OBJ_FLAG_HIDDEN);
        s_notification_show_tick = lv_tick_get();
    }

    if (s_notification_popup && !lv_obj_has_flag(s_notification_popup, LV_OBJ_FLAG_HIDDEN) &&
        lv_tick_elaps(s_notification_show_tick) >= NOTIFICATION_SHOW_MS)
    {
        lv_obj_add_flag(s_notification_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

bk_err_t notification_popup_init(lv_font_t *font)
{
    bk_err_t ret;

    s_notification_font = font;
    if (!s_notification_mutex)
    {
        ret = rtos_init_mutex(&s_notification_mutex);
        if (ret != BK_OK)
        {
            return ret;
        }
    }
    if (!s_notification_timer)
    {
        s_notification_timer = lv_timer_create(notification_popup_timer_cb, NOTIFICATION_POLL_MS, NULL);
        if (!s_notification_timer)
        {
            return BK_FAIL;
        }
    }
    if (!s_notification_popup)
    {
        notification_popup_create();
    }
    return BK_OK;
}

void notification_popup_show(const char *title, const char *message)
{
    if (!s_notification_mutex || rtos_lock_mutex(&s_notification_mutex) != BK_OK)
    {
        return;
    }

    snprintf(s_notification_pending.title, sizeof(s_notification_pending.title), "%s", title ? title : "");
    snprintf(s_notification_pending.message, sizeof(s_notification_pending.message), "%s", message ? message : "");
    s_notification_pending.pending = true;
    rtos_unlock_mutex(&s_notification_mutex);
}
