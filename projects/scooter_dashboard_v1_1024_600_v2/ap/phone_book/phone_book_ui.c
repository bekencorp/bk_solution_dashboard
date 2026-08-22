#include "phone_book_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "beken_ui.h"
#include "home_ui.h"
#include "hfp_hf_demo.h"
#include "components/log.h"
#include "lvgl.h"

#if CONFIG_PBAP_CONTACTS
#include "pbap_contacts.h"
#endif

#define TAG "phone_book_ui"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/* Row geometry (mirrors the designer's static row layout). */
#define PB_LIST_WIDTH   592
#define PB_ROW_HEIGHT   92
#define PB_NAME_WIDTH   430
#define PB_NAME_HEIGHT  42
#define PB_NUM_WIDTH    220

/* Upper bound on rows created at once. LVGL objects are allocated from the small
 * AP SRAM heap (~5 objects/row), so cap the count to avoid exhausting it on a
 * very large phonebook; beyond this the list is truncated (logged). */
#define PB_MAX_ROWS     100

/* Which area the physical key currently drives. */
typedef enum {
    PB_FOCUS_NONE = 0,   /* whole page: single press returns home */
    PB_FOCUS_CONTACTS,   /* contacts list focused: single press scrolls it */
    PB_FOCUS_RECENTS,    /* recents list focused: single press scrolls it */
} pb_focus_t;

static pb_focus_t s_focus;
static int        s_contact_sel;
static int        s_recent_sel;
static int        s_contact_rows;   /* real (selectable) contact rows */
static int        s_recent_rows;    /* real (selectable) recent rows */

/*
 * Every selectable row belongs to one keypad group. UP/DOWN move inside the
 * active list, LEFT/RIGHT switch directly between Contacts and Recents, and
 * ENTER dials the focused row.
 */
static lv_group_t *s_phone_book_group = NULL;

static void pb_refresh_focus(void);

static bool phone_book_bt_connected(void)
{
#if CONFIG_PBAP_CONTACTS
    return pbap_contacts_is_connected() != 0;
#else
    return false;
#endif
}

static const char *phone_book_peer_name(void)
{
#if CONFIG_PBAP_CONTACTS
    return pbap_contacts_peer_name();
#else
    return "";
#endif
}

/* Refresh the top-left Bluetooth status (icon tint + label) from the live
 * connection state and the resolved remote device name. */
static void phone_book_update_header(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    bool        connected = phone_book_bt_connected();
    lv_color_t  accent = connected ? lv_color_hex(0x22c55e)  /* green */
                                   : lv_color_hex(0x9a9a9a); /* gray  */

    if (ui->phone_book_bt_ic != NULL && lv_obj_is_valid(ui->phone_book_bt_ic))
    {
        lv_image_set_src(ui->phone_book_bt_ic,
                         connected ? &nav_ic_bt_26x26_RGB565A8_NONE
                                   : &bt_gray_sm_22x23_RGB565A8_NONE);
    }

    if (ui->phone_book_status_lbl != NULL && lv_obj_is_valid(ui->phone_book_status_lbl))
    {
        lv_obj_t   *lbl = ui->phone_book_status_lbl;
        const char *name = phone_book_peer_name();
        lv_font_t  *cn = home_ui_get_cn_font();
        char        buf[96];

        /* Device names may be CJK; use the loaded TTF when available. The TTF
         * (32px) is taller than the designer's 18px box, so let the label size to
         * its content and center it against the icon (below) instead of relying
         * on the fixed y/height baked into the generated layout. */
        lv_obj_set_style_text_font(lbl, cn ? cn : &lv_font_montserrat_regular_16,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(lbl, 420);
        lv_obj_set_height(lbl, LV_SIZE_CONTENT);

        if (!connected)
        {
            snprintf(buf, sizeof(buf), "No device");
        }
        else if (name[0] != '\0')
        {
            snprintf(buf, sizeof(buf), "Connected: %s", name);
        }
        else
        {
            snprintf(buf, sizeof(buf), "Connected");
        }
        lv_label_set_text(lbl, buf);

        /* Vertically center the text on the icon regardless of font height. */
        if (ui->phone_book_bt_ic != NULL && lv_obj_is_valid(ui->phone_book_bt_ic))
        {
            lv_obj_align_to(lbl, ui->phone_book_bt_ic,
                            LV_ALIGN_OUT_RIGHT_MID, 8, 0);
        }
    }
}

/* Create a common row container inside a flex-column list. */
static lv_obj_t *phone_book_new_row(lv_obj_t *list)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, PB_LIST_WIDTH, PB_ROW_HEIGHT);
    lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, lv_color_hex(0x262626), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    return row;
}

/* Prepare a list for (re)population: drop old rows, enable vertical scroll. */
static void phone_book_reset_list(lv_obj_t *list)
{
    lv_obj_clean(list);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
}

/* Add a dimmed placeholder row shown when a list has no entries. */
static void phone_book_add_empty(lv_obj_t *list, const char *text)
{
    lv_obj_t *lbl = lv_label_create(list);

    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_regular_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9a9a9a),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lbl, 24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(lbl, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, text);
}

/* ------------------------------------------------------------------ */
/* Key-driven focus + row selection                                   */
/* ------------------------------------------------------------------ */

/* Highlight the selected row of @list (when focused) and scroll it into view;
 * clear the highlight on all other rows. */
static void pb_apply_selection(lv_obj_t *list, int sel, bool focused)
{
    uint32_t cnt;
    uint32_t i;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    cnt = lv_obj_get_child_count(list);
    for (i = 0; i < cnt; i++)
    {
        lv_obj_t *row = lv_obj_get_child(list, i);

        if (row == NULL || !lv_obj_is_valid(row))
        {
            continue;
        }

        if (focused && (int)i == sel)
        {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(row, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_scroll_to_view(row, LV_ANIM_ON);
        }
        else
        {
            lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/* Mark a list panel as focused with a visible accent frame (the designer's
 * lists have border_width 0, so toggle the width too, not just the opacity). */
static void pb_mark_list_focus(lv_obj_t *list, bool focused)
{
    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    lv_obj_set_style_border_width(list, focused ? 2 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(list, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(list, focused ? 255 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* Clamp @*sel into [0, count) for @list, defaulting to 0 when out of range. */
static void pb_clamp_sel(lv_obj_t *list, int *sel)
{
    int cnt = (list && lv_obj_is_valid(list)) ? (int)lv_obj_get_child_count(list) : 0;

    if (*sel < 0 || *sel >= cnt)
    {
        *sel = 0;
    }
}

/* Re-apply the current focus/selection highlight to both lists. */
static void pb_refresh_focus(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    pb_mark_list_focus(ui->phone_book_contacts_list, s_focus == PB_FOCUS_CONTACTS);
    pb_mark_list_focus(ui->phone_book_recents_list, s_focus == PB_FOCUS_RECENTS);

    pb_apply_selection(ui->phone_book_contacts_list, s_contact_sel,
                       s_focus == PB_FOCUS_CONTACTS);
    pb_apply_selection(ui->phone_book_recents_list, s_recent_sel,
                       s_focus == PB_FOCUS_RECENTS);
}

static void pb_set_focus(pb_focus_t f)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    s_focus = f;

    if (f == PB_FOCUS_CONTACTS)
    {
        pb_clamp_sel(ui->phone_book_contacts_list, &s_contact_sel);
    }
    else if (f == PB_FOCUS_RECENTS)
    {
        pb_clamp_sel(ui->phone_book_recents_list, &s_recent_sel);
    }

    pb_refresh_focus();
    LOGI("focus -> %d\n", (int)f);
}

/* ------------------------------------------------------------------ */
/* Method-A navigation group                                          */
/* ------------------------------------------------------------------ */

/* Dial the selected entry of a list by index (re-snapshot: row order matches
 * snapshot order, so the index maps directly to the entry). */
static void phone_book_dial_selection(pb_focus_t focus, int sel)
{
#if CONFIG_PBAP_CONTACTS
    char number[32] = {0};

    if (focus == PB_FOCUS_CONTACTS)
    {
        static pbap_contact_info_t snap[PB_MAX_ROWS];
        int n = pbap_contacts_snapshot(snap, PB_MAX_ROWS);
        if (sel >= 0 && sel < n)
        {
            snprintf(number, sizeof(number), "%s", snap[sel].number);
        }
    }
    else if (focus == PB_FOCUS_RECENTS)
    {
        static pbap_recent_info_t snap[PB_MAX_ROWS];
        int n = pbap_recents_snapshot(snap, PB_MAX_ROWS);
        if (sel >= 0 && sel < n)
        {
            snprintf(number, sizeof(number), "%s", snap[sel].number);
        }
    }

    if (number[0] != '\0')
    {
        LOGI("dial %s\n", number);
        hfp_demo_dial(1, (uint8_t *)number);
        /* Jump back home so the outgoing-call popup is visible right away. */
        beken_ui_nav_home();
    }
    else
    {
        LOGI("dial ignored: no number for sel\n");
    }
#else
    (void)focus;
    (void)sel;
#endif
}

/* ENTER / click on a focused row: dial it. The row carries its (list, index) as
 * user_data so we can re-snapshot the number without caching it. */
static void phone_book_row_enter_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    pb_focus_t focus = (pb_focus_t)(tag >> 16);
    int sel = (int)(tag & 0xffffu);

    phone_book_dial_selection(focus, sel);
}

static bool phone_book_focus_row(pb_focus_t focus, int sel)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *list;
    lv_obj_t *row;
    int count;

    if (focus == PB_FOCUS_CONTACTS)
    {
        list = ui->phone_book_contacts_list;
        count = s_contact_rows;
    }
    else if (focus == PB_FOCUS_RECENTS)
    {
        list = ui->phone_book_recents_list;
        count = s_recent_rows;
    }
    else
    {
        return false;
    }

    if (list == NULL || !lv_obj_is_valid(list) || count <= 0)
    {
        return false;
    }

    if (sel < 0 || sel >= count)
    {
        sel = 0;
    }

    row = lv_obj_get_child(list, sel);
    if (row == NULL || !lv_obj_is_valid(row))
    {
        return false;
    }

    lv_group_focus_obj(row);
    return true;
}

static void phone_book_row_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    int *sel;
    int count;

    if (key == LV_KEY_LEFT)
    {
        phone_book_focus_row(PB_FOCUS_CONTACTS, s_contact_sel);
        return;
    }

    if (key == LV_KEY_RIGHT)
    {
        phone_book_focus_row(PB_FOCUS_RECENTS, s_recent_sel);
        return;
    }

    if (s_focus == PB_FOCUS_CONTACTS)
    {
        sel = &s_contact_sel;
        count = s_contact_rows;
    }
    else if (s_focus == PB_FOCUS_RECENTS)
    {
        sel = &s_recent_sel;
        count = s_recent_rows;
    }
    else
    {
        return;
    }

    if (count <= 0)
    {
        return;
    }

    if (key == LV_KEY_UP)
    {
        *sel = (*sel + count - 1) % count;
    }
    else if (key == LV_KEY_DOWN)
    {
        *sel = (*sel + 1) % count;
    }
    else
    {
        return;
    }

    phone_book_focus_row(s_focus, *sel);
}

/* Keep the visible selection/focus in sync with the group focus (keypad nav). */
static void phone_book_ui_group_focus_cb(lv_group_t *group)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *focused = lv_group_get_focused(group);
    lv_obj_t *parent;

    if (focused == NULL)
    {
        return;
    }

    parent = lv_obj_get_parent(focused);
    if (parent == ui->phone_book_contacts_list)
    {
        s_focus = PB_FOCUS_CONTACTS;
        s_contact_sel = (int)lv_obj_get_index(focused);
    }
    else if (parent == ui->phone_book_recents_list)
    {
        s_focus = PB_FOCUS_RECENTS;
        s_recent_sel = (int)lv_obj_get_index(focused);
    }
    else
    {
        return;
    }

    pb_refresh_focus();
}

static void phone_book_ui_group_ensure(void)
{
    if (s_phone_book_group == NULL)
    {
        s_phone_book_group = lv_group_create();
        if (s_phone_book_group != NULL)
        {
            lv_group_set_wrap(s_phone_book_group, true);
            lv_group_set_focus_cb(s_phone_book_group, phone_book_ui_group_focus_cb);
        }
    }
}

lv_group_t *phone_book_ui_get_group(void)
{
    phone_book_ui_group_ensure();
    return s_phone_book_group;
}

/* Make a freshly-created row focusable/clickable and add it to the nav group,
 * tagging it with (list, index) so ENTER can dial it. */
static void phone_book_group_add_row(lv_obj_t *row, pb_focus_t focus)
{
    uintptr_t tag;
    int idx;

    if (row == NULL || !lv_obj_is_valid(row))
    {
        return;
    }

    phone_book_ui_group_ensure();
    if (s_phone_book_group == NULL)
    {
        return;
    }

    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    idx = (int)lv_obj_get_index(row);
    tag = ((uintptr_t)focus << 16) | (uintptr_t)(idx & 0xffff);

    lv_group_add_obj(s_phone_book_group, row);
    lv_obj_add_event_cb(row, phone_book_row_enter_cb, LV_EVENT_CLICKED, (void *)tag);
    lv_obj_add_event_cb(row, phone_book_row_key_cb, LV_EVENT_KEY, NULL);
}

/* ------------------------------------------------------------------ */
/* Contacts                                                            */
/* ------------------------------------------------------------------ */

static void phone_book_add_contact_row(lv_obj_t *list, const char *name,
                                       const char *number, lv_font_t *cn)
{
    lv_obj_t *row = phone_book_new_row(list);

    lv_obj_t *av = lv_image_create(row);
    lv_image_set_src(av, &avatar_sm_42x43_RGB565A8_NONE);
    lv_obj_set_pos(av, 0, 20);
    lv_obj_set_size(av, 52, 52);

    lv_obj_t *nm = lv_label_create(row);
    /* Contact names may be Chinese; use the shared CJK TTF loaded by home_ui
     * (the designer assigns Latin-only Montserrat). */
    lv_obj_set_style_text_font(nm, cn ? cn : &lv_font_montserrat_regular_30,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(nm, 68, 8);
    lv_obj_set_size(nm, PB_NAME_WIDTH, PB_NAME_HEIGHT);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(nm, name ? name : "");

    lv_obj_t *no = lv_label_create(row);
    lv_obj_set_style_text_font(no, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(no, lv_color_hex(0x9a9a9a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(no, 68, 58);
    lv_obj_set_width(no, PB_NUM_WIDTH);
    lv_label_set_text(no, number ? number : "");

    lv_obj_t *call = lv_image_create(row);
    lv_image_set_src(call, &oncall_sm_35x37_RGB565A8_NONE);
    lv_obj_set_pos(call, 532, 26);
    lv_obj_set_size(call, 44, 44);

    phone_book_group_add_row(row, PB_FOCUS_CONTACTS);
}

static void phone_book_fill_contacts(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *list = ui->phone_book_contacts_list;
    int n = 0;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    phone_book_reset_list(list);

#if CONFIG_PBAP_CONTACTS
    static pbap_contact_info_t s_snap[PB_MAX_ROWS];
    lv_font_t *cn = home_ui_get_cn_font();
    int total = pbap_contacts_count();

    n = pbap_contacts_snapshot(s_snap, PB_MAX_ROWS);
    for (int i = 0; i < n; i++)
    {
        phone_book_add_contact_row(list, s_snap[i].name, s_snap[i].number, cn);
    }
    if (total > n)
    {
        LOGI("contacts %d, showing first %d\n", total, n);
    }
#endif

    s_contact_rows = n;
    if (n == 0)
    {
        phone_book_add_empty(list, phone_book_bt_connected() ? "No contacts"
                                                             : "No device connected");
    }

    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    LOGI("built %d contact rows\n", n);
}

/* ------------------------------------------------------------------ */
/* Recents (call history)                                             */
/* ------------------------------------------------------------------ */

#if CONFIG_PBAP_CONTACTS
/* Map a call type to its arrow icon, label text and accent color. */
static const lv_image_dsc_t *recent_type_icon(uint8_t type)
{
    switch (type)
    {
    case PBAP_CALL_RECEIVED: return &arrow_in_sm_24x25_RGB565A8_NONE;
    case PBAP_CALL_MISSED:   return &arrow_miss_sm_24x25_RGB565A8_NONE;
    case PBAP_CALL_DIALED:
    default:                 return &arrow_out_sm_24x25_RGB565A8_NONE;
    }
}

static const char *recent_type_text(uint8_t type)
{
    switch (type)
    {
    case PBAP_CALL_RECEIVED: return "Incoming";
    case PBAP_CALL_MISSED:   return "Missed";
    case PBAP_CALL_DIALED:   return "Outgoing";
    default:                 return "Call";
    }
}

static lv_color_t recent_type_color(uint8_t type)
{
    return (type == PBAP_CALL_MISSED) ? lv_color_hex(0xef4444)
                                      : lv_color_hex(0x22c55e);
}

/* Format "YYYY-MM-DD HH:MM" from a raw "YYYYMMDDThhmmss" timestamp. */
static void recent_format_time(const char *dt, char *out, size_t out_sz)
{
    out[0] = '\0';
    if (dt != NULL && strlen(dt) >= 13 && dt[8] == 'T')
    {
        snprintf(out, out_sz, "%c%c%c%c-%c%c-%c%c %c%c:%c%c",
                 dt[0], dt[1], dt[2], dt[3],
                 dt[4], dt[5], dt[6], dt[7],
                 dt[9], dt[10], dt[11], dt[12]);
    }
}

static void phone_book_add_recent_row(lv_obj_t *list, const pbap_recent_info_t *r,
                                      lv_font_t *cn)
{
    char tm[20];
    lv_obj_t *row = phone_book_new_row(list);

    lv_obj_t *ar = lv_image_create(row);
    lv_image_set_src(ar, recent_type_icon(r->type));
    lv_obj_set_pos(ar, 2, 28);
    lv_obj_set_size(ar, 30, 30);

    lv_obj_t *nm = lv_label_create(row);
    lv_obj_set_style_text_font(nm, cn ? cn : &lv_font_montserrat_regular_30,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(nm, 48, 8);
    lv_obj_set_size(nm, 280, PB_NAME_HEIGHT);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(nm, r->name);

    lv_obj_t *tp = lv_label_create(row);
    lv_obj_set_style_text_font(tp, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(tp, recent_type_color(r->type), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(tp, 48, 58);
    lv_obj_set_width(tp, 160);
    lv_label_set_text(tp, recent_type_text(r->type));

    recent_format_time(r->datetime, tm, sizeof(tm));
    lv_obj_t *tml = lv_label_create(row);
    lv_obj_set_style_text_font(tml, &lv_font_montserrat_regular_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(tml, lv_color_hex(0x9a9a9a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(tml, 336, 34);
    lv_obj_set_width(tml, 180);
    lv_label_set_text(tml, tm);

    lv_obj_t *call = lv_image_create(row);
    lv_image_set_src(call, &oncall_sm_35x37_RGB565A8_NONE);
    lv_obj_set_pos(call, 536, 24);
    lv_obj_set_size(call, 44, 44);

    phone_book_group_add_row(row, PB_FOCUS_RECENTS);
}
#endif /* CONFIG_PBAP_CONTACTS */

static void phone_book_fill_recents(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *list = ui->phone_book_recents_list;
    int n = 0;

    if (list == NULL || !lv_obj_is_valid(list))
    {
        return;
    }

    phone_book_reset_list(list);

#if CONFIG_PBAP_CONTACTS
    static pbap_recent_info_t s_snap[PB_MAX_ROWS];
    lv_font_t *cn = home_ui_get_cn_font();
    int total = pbap_recents_count();

    n = pbap_recents_snapshot(s_snap, PB_MAX_ROWS);
    for (int i = 0; i < n; i++)
    {
        phone_book_add_recent_row(list, &s_snap[i], cn);
    }
    if (total > n)
    {
        LOGI("recents %d, showing first %d\n", total, n);
    }
#endif

    s_recent_rows = n;
    if (n == 0)
    {
        phone_book_add_empty(list, phone_book_bt_connected() ? "No recent calls"
                                                             : "No device connected");
    }

    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    LOGI("built %d recent rows\n", n);
}

/* ------------------------------------------------------------------ */
/* Refresh + lifecycle                                                */
/* ------------------------------------------------------------------ */

static void phone_book_refresh(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (ui->phone_book == NULL || !lv_obj_is_valid(ui->phone_book))
    {
        return;
    }
    phone_book_update_header();
    phone_book_fill_contacts();
    phone_book_fill_recents();

    /* Rows were rebuilt (indices may now be out of range); re-apply the
     * current focus/selection so the highlight survives an async refresh. */
    pb_clamp_sel(ui->phone_book_contacts_list, &s_contact_sel);
    pb_clamp_sel(ui->phone_book_recents_list, &s_recent_sel);
    pb_refresh_focus();
}

/* ------------------------------------------------------------------ */
/* Physical-key handlers                                              */
/* ------------------------------------------------------------------ */

static bool phone_book_is_active(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    return ui->phone_book != NULL && lv_obj_is_valid(ui->phone_book) &&
           ui->phone_book == lv_screen_active();
}

bool phone_book_ui_handle_key_double(void)
{
    pb_focus_t next;

    if (!phone_book_is_active())
    {
        return false;
    }

    /* whole-page -> contacts -> recents -> whole-page */
    next = (s_focus >= PB_FOCUS_RECENTS) ? PB_FOCUS_NONE
                                         : (pb_focus_t)(s_focus + 1);
    pb_set_focus(next);
    return true;
}

bool phone_book_ui_handle_key_single(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t  *list;
    int       *sel;
    int        cnt;

    if (!phone_book_is_active() || s_focus == PB_FOCUS_NONE)
    {
        /* Let the home-menu single-press behavior (return home) run. */
        return false;
    }

    if (s_focus == PB_FOCUS_CONTACTS)
    {
        list = ui->phone_book_contacts_list;
        sel  = &s_contact_sel;
        cnt  = s_contact_rows;
    }
    else
    {
        list = ui->phone_book_recents_list;
        sel  = &s_recent_sel;
        cnt  = s_recent_rows;
    }

    if (list == NULL || !lv_obj_is_valid(list) || cnt <= 0)
    {
        return true;
    }

    *sel = (*sel + 1) % cnt;
    pb_apply_selection(list, *sel, true);
    LOGI("single -> sel=%d (focus %d)\n", *sel, (int)s_focus);
    return true;
}

#if CONFIG_PBAP_CONTACTS
static void phone_book_refresh_async_cb(void *unused)
{
    (void)unused;
    phone_book_refresh();
}

/* Runs in the PBAP worker-thread context: marshal the refresh to the LVGL
 * thread, and let phone_book_refresh() re-check that the page is still valid. */
static void phone_book_contacts_updated_cb(void *user_data)
{
    (void)user_data;
    lv_async_call(phone_book_refresh_async_cb, NULL);
}
#endif

void phone_book_ui_enter(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t *first = NULL;

    s_focus       = PB_FOCUS_NONE;
    s_contact_sel = 0;
    s_recent_sel  = 0;
    /* Clear stale row counts up front: if phone_book_refresh() bails early
     * (page/list not valid yet) it will not touch these, and we must not trust a
     * previous session's value below (that would dereference a freed list). */
    s_contact_rows = 0;
    s_recent_rows  = 0;

#if CONFIG_PBAP_CONTACTS
    pbap_contacts_set_updated_cb(phone_book_contacts_updated_cb, NULL);
#endif
    phone_book_refresh();

    /* Focus the first available row so the KEYPAD indev (bound to this group by
     * beken_ui once the page is active) drives it right away; the focus_cb sets
     * s_focus / the selection index and repaints the highlight. Validate the
     * list object (not just non-NULL) since the handle may be stale. */
    if (s_contact_rows > 0 && ui->phone_book_contacts_list != NULL &&
        lv_obj_is_valid(ui->phone_book_contacts_list))
    {
        first = lv_obj_get_child(ui->phone_book_contacts_list, 0);
    }
    else if (s_recent_rows > 0 && ui->phone_book_recents_list != NULL &&
             lv_obj_is_valid(ui->phone_book_recents_list))
    {
        first = lv_obj_get_child(ui->phone_book_recents_list, 0);
    }

    if (first != NULL && lv_obj_is_valid(first) && s_phone_book_group != NULL)
    {
        lv_group_focus_obj(first);
    }
}

void phone_book_ui_leave(void)
{
    s_focus = PB_FOCUS_NONE;

#if CONFIG_PBAP_CONTACTS
    pbap_contacts_set_updated_cb(NULL, NULL);
#endif
}
