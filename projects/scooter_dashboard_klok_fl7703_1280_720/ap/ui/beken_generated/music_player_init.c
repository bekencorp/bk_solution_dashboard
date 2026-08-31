#include "beken_ui.h"
#include "event_runtime.h"
#include "music_player_ui.h"

static lv_obj_t *mp_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x071126), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x281142), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, 155, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x7d6dff), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, 145, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 28, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x2ce8ff), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, 65, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 28, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 18, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *mp_text(lv_obj_t *parent, const char *text, int x, int y,
                         int w, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static lv_obj_t *mp_button(lv_obj_t *parent, const char *text, int x, int y,
                           int w, int h, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x39236d), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x087f9a), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, 205, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x62eaff), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, h / 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x27ddff), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(button, 90, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 18, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (callback != NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    return label;
}

static void mp_back_cb(lv_event_t *event)
{
    (void)event;
    music_player_ui_leave_to_home_async();
}

static void mp_bt_tab_cb(lv_event_t *event)
{
    (void)event;
    music_player_ui_select_bluetooth();
}

static void mp_usb_tab_cb(lv_event_t *event)
{
    (void)event;
    music_player_ui_select_udisk();
}

static void mp_bt_prev_cb(lv_event_t *event) { (void)event; music_player_ui_bt_prev(); }
static void mp_bt_play_cb(lv_event_t *event) { (void)event; music_player_ui_bt_toggle(); }
static void mp_bt_next_cb(lv_event_t *event) { (void)event; music_player_ui_bt_next(); }
static void mp_usb_prev_cb(lv_event_t *event) { (void)event; music_player_ui_local_prev(); }
static void mp_usb_play_cb(lv_event_t *event) { (void)event; music_player_ui_local_toggle(); }
static void mp_usb_next_cb(lv_event_t *event) { (void)event; music_player_ui_local_next(); }

static void mp_glitch_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *magenta = mp_text(parent, text, 504, 100, 520,
                                &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34,
                                0xff38bc);
    lv_obj_t *cyan = mp_text(parent, text, 510, 96, 520,
                             &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34,
                             0x25eaff);
    lv_obj_t *front = mp_text(parent, text, 507, 98, 520,
                              &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34,
                              0xffffff);
    lv_obj_set_style_text_opa(magenta, 180, LV_PART_MAIN);
    lv_obj_set_style_text_opa(cyan, 190, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(front, 5, LV_PART_MAIN);
}

void init_page_music_player(bk_lv_ui_t *ui)
{
    lv_obj_t *tint;
    lv_obj_t *lyric_tag;

    if (ui->music_player != NULL && lv_obj_is_valid(ui->music_player)) {
        destroy_page_music_player(ui);
    }
    ui->music_player = lv_obj_create(NULL);
    lv_obj_set_size(ui->music_player, 1280, 720);
    lv_obj_set_style_bg_color(ui->music_player, lv_color_hex(0x030713), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(ui->music_player, lv_color_hex(0x17082a), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(ui->music_player, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_remove_flag(ui->music_player, LV_OBJ_FLAG_SCROLLABLE);

    /* The decoded MP4 preview is moved behind this translucent color grade. */
    tint = lv_obj_create(ui->music_player);
    lv_obj_set_pos(tint, 0, 0);
    lv_obj_set_size(tint, 1280, 720);
    lv_obj_set_style_bg_color(tint, lv_color_hex(0x020817), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(tint, lv_color_hex(0x26052f), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(tint, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tint, 105, LV_PART_MAIN);
    lv_obj_set_style_border_width(tint, 0, LV_PART_MAIN);
    lv_obj_remove_flag(tint, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /*
     * Immersive lyric mode: the video is the page. Keep compatibility
     * objects hidden for the existing controller logic and render only status,
     * lyric and artist above the background.
     */
    ui->music_player_back = lv_button_create(ui->music_player);
    lv_obj_set_pos(ui->music_player_back, 20, 9);
    lv_obj_set_size(ui->music_player_back, 120, 64);
    lv_obj_set_style_bg_opa(ui->music_player_back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->music_player_back, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui->music_player_back, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(ui->music_player_back, mp_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(ui->music_player_back);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(back_label,
                               &lv_font_Alibaba_PuHuiTi_2_0_65_Medium_65_Medium_18,
                               LV_PART_MAIN);
    lv_obj_center(back_label);
    ui->music_player_bt_tab = lv_button_create(ui->music_player);
    lv_obj_add_flag(ui->music_player_bt_tab, LV_OBJ_FLAG_HIDDEN);
    ui->music_player_usb_tab = lv_button_create(ui->music_player);
    lv_obj_add_flag(ui->music_player_usb_tab, LV_OBJ_FLAG_HIDDEN);

    ui->music_player_bt_panel = lv_obj_create(ui->music_player);
    lv_obj_set_pos(ui->music_player_bt_panel, 72, 200);
    lv_obj_set_size(ui->music_player_bt_panel, 1136, 320);
    lv_obj_set_style_bg_opa(ui->music_player_bt_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->music_player_bt_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui->music_player_bt_panel, 0, LV_PART_MAIN);
    lv_obj_remove_flag(ui->music_player_bt_panel,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ui->music_player_bt_status = mp_text(ui->music_player_bt_panel,
                                         "等待手机连接 / A2DP SINK", 240, 0, 800,
                                         &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_20,
                                         0xb8f5ff);
    lv_obj_add_flag(ui->music_player_bt_status, LV_OBJ_FLAG_HIDDEN);

    lyric_tag = mp_text(ui->music_player_bt_panel, "NOW  PLAYING", 468, 2, 200,
                        &lv_font_montserrat_regular_16, 0x8ef5ff);
    lv_obj_set_style_text_letter_space(lyric_tag, 4, LV_PART_MAIN);
    lv_obj_set_style_text_align(lyric_tag, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    ui->music_player_bt_title_magenta =
        mp_text(ui->music_player_bt_panel, "未连接", 4, 25, 1120,
                &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34,
                0xff39d4);
    ui->music_player_bt_title_cyan =
        mp_text(ui->music_player_bt_panel, "未连接", 12, 21, 1120,
                &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34,
                0x28e7ff);
    ui->music_player_bt_title = mp_text(ui->music_player_bt_panel, "未连接",
                                        8, 23, 1120,
                                        &lv_font_Alibaba_PuHuiTi_2_0_75_SemiBold_75_SemiBold_34,
                                        0xfffbff);
    lv_obj_set_style_text_opa(ui->music_player_bt_title_magenta, 105, LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui->music_player_bt_title_cyan, 145, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(ui->music_player_bt_title_magenta, 2, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(ui->music_player_bt_title_cyan, 2, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(ui->music_player_bt_title, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->music_player_bt_title, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_label_set_long_mode(ui->music_player_bt_title_magenta, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_long_mode(ui->music_player_bt_title_cyan, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_long_mode(ui->music_player_bt_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(ui->music_player_bt_title_magenta, 215);
    lv_obj_set_height(ui->music_player_bt_title_cyan, 215);
    lv_obj_set_height(ui->music_player_bt_title, 215);
    lv_obj_set_style_text_align(ui->music_player_bt_title_magenta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui->music_player_bt_title_cyan, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui->music_player_bt_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui->music_player_bt_artist = mp_text(ui->music_player_bt_panel, "BLUETOOTH AUDIO",
                                         190, 254, 756,
                                         &lv_font_Alibaba_PuHuiTi_2_0_55_Regular_55_Regular_24,
                                         0xcbd7ef);
    lv_obj_set_style_text_align(ui->music_player_bt_artist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(ui->music_player_bt_artist, 2, LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui->music_player_bt_artist, 215, LV_PART_MAIN);
    ui->music_player_bt_play = lv_label_create(ui->music_player_bt_panel);
    lv_obj_add_flag(ui->music_player_bt_play, LV_OBJ_FLAG_HIDDEN);

    ui->music_player_usb_panel = lv_obj_create(ui->music_player);
    lv_obj_add_flag(ui->music_player_usb_panel, LV_OBJ_FLAG_HIDDEN);
    ui->music_player_usb_title = lv_label_create(ui->music_player_usb_panel);
    ui->music_player_usb_artist = lv_label_create(ui->music_player_usb_panel);
    ui->music_player_usb_play = lv_label_create(ui->music_player_usb_panel);
    ui->music_player_usb_list = lv_obj_create(ui->music_player_usb_panel);

    music_player_ui_enter();
}

void destroy_page_music_player(bk_lv_ui_t *ui)
{
    music_player_ui_leave();
    if (ui->music_player != NULL && lv_obj_is_valid(ui->music_player)) {
        lv_obj_delete(ui->music_player);
    }
    ui->music_player = NULL;
}
