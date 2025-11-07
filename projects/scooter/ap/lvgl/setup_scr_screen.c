#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include "setup_scr_screen.h"

int screen_digital_clock_1_min_value = 25;
int screen_digital_clock_1_hour_value = 11;
int screen_digital_clock_1_sec_value = 50;
char screen_digital_clock_1_meridiem[] = "AM";

bool lv_speed_is_increase = true;
bool lv_turn_signal_is_green = true;
static lv_ui g_ui_screen;

static void lv_speed_time_cb(lv_timer_t *timer)
{
    static int32_t value = 0;
    char speed[3];

    if (value == 100) {
        lv_speed_is_increase = false;
    }

    if (value == 0) {
        lv_speed_is_increase = true;
    }

    if (lv_speed_is_increase == true) {
        if (value >= 0 && value <= 60) {
            value += 5;
            lv_timer_set_period(timer, 100);
        }

        if (value > 60 && value <= 80) {
            value += 2;
            lv_timer_set_period(timer, 150);
        }

        if (value > 80 && value < 100) {
            value += 1;
            lv_timer_set_period(timer, 200);
        }
    } else {
        value -= 4;
        lv_timer_set_period(timer, 180);
    }

    sprintf(speed, "%d", value);
    lv_label_set_text(g_ui_screen.screen_label_7, speed);
    lv_scale_set_line_needle_value(g_ui_screen.screen_meter_1, g_ui_screen.screen_meter_1_ndline_0, 65, value);
}

static void lv_mileage_time_cb(lv_timer_t *timer)
{
    static float trip_value = 12.4;
    static uint32_t mileage_value = 300;
    char trip[6];
    char mileage[6];

    trip_value += 0.1;
    sprintf(trip, "%.1f", trip_value);
    lv_label_set_text(g_ui_screen.screen_label_2, trip);

    if (fmod(trip_value, 1.0) < 0.1) {
        mileage_value++;
        sprintf(mileage, "%d", mileage_value);
        lv_label_set_text(g_ui_screen.screen_label_4, mileage);
    }
}

static void lv_turn_signal_timer_cb(lv_timer_t *timer)
{
    if (lv_turn_signal_is_green) {
        lv_image_set_src(g_ui_screen.screen_img_5, &_light_grey_RGB565A8_49x40);
        lv_turn_signal_is_green = false;
    } else {
        lv_image_set_src(g_ui_screen.screen_img_5, &_light_green_RGB565A8_49x40);
        lv_turn_signal_is_green = true;
    }
}

static void digital_clock_count(int * hour, int * minute, int * seconds, char * meridiem)
{
    (*seconds)++;

    if (*seconds == 60) {
        *seconds = 0;
        (*minute)++;
    }

    if (*minute == 60) {
        *minute = 0;
        if (*hour < 12) {
            (*hour)++;
        } else {
            (*hour)++;
            (*hour) = (*hour) % 12;
        }
    }

    if (*hour == 12 && *seconds == 0 && *minute == 0) {
        if ((lv_strcmp(meridiem, "PM") == 0)) {
            lv_strcpy(meridiem, "AM");
        } else {
            lv_strcpy(meridiem, "PM");
        }
    }
}

static void screen_digital_clock_1_timer(lv_timer_t *timer)
{
    digital_clock_count(&screen_digital_clock_1_hour_value, &screen_digital_clock_1_min_value, &screen_digital_clock_1_sec_value, screen_digital_clock_1_meridiem);

    if (lv_obj_is_valid(g_ui_screen.screen_digital_clock_1)){
        lv_label_set_text_fmt(g_ui_screen.screen_digital_clock_1, "%d:%02d %s", screen_digital_clock_1_hour_value, screen_digital_clock_1_min_value, screen_digital_clock_1_meridiem);
    }
}

void lv_setup_scr_screen(lv_ui *ui)
{
    //Write codes screen
    ui->screen = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen, 480, 272);
    lv_obj_set_scrollbar_mode(ui->screen, LV_SCROLLBAR_MODE_OFF);

    //Write style for screen, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(ui->screen, &_moto_RGB565A8_480x272, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(ui->screen, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(ui->screen, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_img_1
    ui->screen_img_1 = lv_image_create(ui->screen);
    lv_obj_set_pos(ui->screen_img_1, 206, 8);
    lv_obj_set_size(ui->screen_img_1, 73, 36);
    lv_obj_add_flag(ui->screen_img_1, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_1, &_beken_logo_blue_RGB565A8_73x36);
    lv_image_set_pivot(ui->screen_img_1, 50,50);
    lv_image_set_rotation(ui->screen_img_1, 0);

    //Write style for screen_img_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->screen_img_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_meter_1
    ui->screen_meter_1 = lv_scale_create(ui->screen);
    lv_obj_set_pos(ui->screen_meter_1, 151, 64);
    lv_obj_set_size(ui->screen_meter_1, 179, 179);
    lv_obj_update_layout(ui->screen_meter_1);
    lv_scale_set_mode(ui->screen_meter_1, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(ui->screen_meter_1, 41);
    lv_scale_set_major_tick_every(ui->screen_meter_1, 8);
    lv_scale_set_label_show(ui->screen_meter_1, true);
    lv_scale_set_range(ui->screen_meter_1, 0, 100);
    lv_scale_set_angle_range(ui->screen_meter_1, 260);
    lv_scale_set_rotation(ui->screen_meter_1, 140);
    lv_scale_set_post_draw(ui->screen_meter_1, true);
    lv_scale_section_t * screen_meter_1_section_0 = lv_scale_add_section(ui->screen_meter_1);
    static lv_style_t screen_meter_1_section_0_minor_tick_style;
    static lv_style_t screen_meter_1_section_0_label_style;
    static lv_style_t screen_meter_1_section_0_main_line_style;
    lv_style_init(&screen_meter_1_section_0_label_style);
    lv_style_init(&screen_meter_1_section_0_minor_tick_style);
    lv_style_init(&screen_meter_1_section_0_main_line_style);

    lv_style_set_text_color(&screen_meter_1_section_0_label_style, lv_color_hex(0xd4f90f));
    lv_style_set_line_color(&screen_meter_1_section_0_label_style, lv_color_hex(0x08cffb));
    lv_style_set_line_color(&screen_meter_1_section_0_minor_tick_style, lv_color_hex(0x08cffb));
    lv_style_set_arc_color(&screen_meter_1_section_0_main_line_style, lv_color_hex(0x03ffb0));
    lv_style_set_arc_width(&screen_meter_1_section_0_main_line_style, 2);

    lv_scale_section_set_range(screen_meter_1_section_0, 0, 25);
    lv_scale_section_set_style(screen_meter_1_section_0, LV_PART_INDICATOR, &screen_meter_1_section_0_label_style);
    lv_scale_section_set_style(screen_meter_1_section_0, LV_PART_MAIN, &screen_meter_1_section_0_main_line_style);
    lv_scale_section_set_style(screen_meter_1_section_0, LV_PART_ITEMS, &screen_meter_1_section_0_minor_tick_style);

    ui->screen_meter_1_ndline_0 = lv_line_create(ui->screen_meter_1);
    lv_obj_set_style_line_width(ui->screen_meter_1_ndline_0, 5, LV_PART_MAIN);
    lv_obj_set_style_line_color(ui->screen_meter_1_ndline_0, lv_color_hex(0x0933fe), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(ui->screen_meter_1_ndline_0, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(ui->screen_meter_1, ui->screen_meter_1_ndline_0, 65, 0);
    lv_timer_create(lv_speed_time_cb, 150, NULL);

    //Write style for screen_meter_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen_meter_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_meter_1, 100, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_meter_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui->screen_meter_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui->screen_meter_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui->screen_meter_1, lv_color_hex(0x757575), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(ui->screen_meter_1, true, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_meter_1, lv_color_hex(0xff0027), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_meter_1, &lv_font_montserratMedium_13, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_meter_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for screen_meter_1, Part: LV_PART_ITEMS, State: LV_STATE_DEFAULT.
    lv_obj_set_style_length(ui->screen_meter_1, 10, LV_PART_ITEMS|LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(ui->screen_meter_1, 2, LV_PART_ITEMS|LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(ui->screen_meter_1, lv_color_hex(0xff0027), LV_PART_ITEMS|LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(ui->screen_meter_1, 255, LV_PART_ITEMS|LV_STATE_DEFAULT);

    //Write style for screen_meter_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_length(ui->screen_meter_1, 18, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(ui->screen_meter_1, 4, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(ui->screen_meter_1, lv_color_hex(0xefff34), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(ui->screen_meter_1, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);

    //Write codes screen_digital_clock_1
    static bool screen_digital_clock_1_timer_enabled = false;
    ui->screen_digital_clock_1 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_digital_clock_1, 17, 14);
    lv_obj_set_size(ui->screen_digital_clock_1, 94, 17);
    lv_label_set_text(ui->screen_digital_clock_1, "11:25 AM");
    if (!screen_digital_clock_1_timer_enabled) {
        lv_timer_create(screen_digital_clock_1_timer, 1000, NULL);
        screen_digital_clock_1_timer_enabled = true;
    }

    //Write style for screen_digital_clock_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_radius(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_digital_clock_1, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_digital_clock_1, &lv_font_Abel_regular_16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_digital_clock_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_digital_clock_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_digital_clock_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_digital_clock_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_img_4
    ui->screen_img_4 = lv_image_create(ui->screen);
    lv_obj_set_pos(ui->screen_img_4, 376, 5);
    lv_obj_set_size(ui->screen_img_4, 21, 20);
    lv_obj_add_flag(ui->screen_img_4, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_4, &_bluetooth_1_RGB565A8_21x20);
    lv_image_set_pivot(ui->screen_img_4, 50,50);
    lv_image_set_rotation(ui->screen_img_4, 0);

    //Write style for screen_img_4, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->screen_img_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_4, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_img_5
    ui->screen_img_5 = lv_image_create(ui->screen);
    lv_obj_set_pos(ui->screen_img_5, 44, 168);
    lv_obj_set_size(ui->screen_img_5, 49, 40);
    lv_obj_add_flag(ui->screen_img_5, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_5, &_light_green_RGB565A8_49x40);
    lv_image_set_pivot(ui->screen_img_5, 50,50);
    lv_image_set_rotation(ui->screen_img_5, 0);
    lv_timer_create(lv_turn_signal_timer_cb, 800, NULL);

    //Write style for screen_img_5, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->screen_img_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_5, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_img_6
    ui->screen_img_6 = lv_image_create(ui->screen);
    lv_obj_set_pos(ui->screen_img_6, 410, 5);
    lv_obj_set_size(ui->screen_img_6, 20, 20);
    lv_obj_add_flag(ui->screen_img_6, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_6, &_wifi_1_RGB565A8_20x20);
    lv_image_set_pivot(ui->screen_img_6, 50,50);
    lv_image_set_rotation(ui->screen_img_6, 0);

    //Write style for screen_img_6, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->screen_img_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_6, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_img_7
    ui->screen_img_7 = lv_image_create(ui->screen);
    lv_obj_set_pos(ui->screen_img_7, 330, 107);
    lv_obj_set_size(ui->screen_img_7, 49, 40);
    lv_obj_add_flag(ui->screen_img_7, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_7, &_light_grey_RGB565A8_49x40);
    lv_image_set_pivot(ui->screen_img_7, 50,50);
    lv_image_set_rotation(ui->screen_img_7, 1800);

    //Write style for screen_img_7, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->screen_img_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_7, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_bar_1
    ui->screen_bar_1 = lv_bar_create(ui->screen);
    lv_obj_set_pos(ui->screen_bar_1, 441, 8);
    lv_obj_set_size(ui->screen_bar_1, 26, 14);
    lv_obj_set_style_anim_duration(ui->screen_bar_1, 1000, 0);
    lv_bar_set_mode(ui->screen_bar_1, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(ui->screen_bar_1, 0, 100);
    lv_bar_set_value(ui->screen_bar_1, 50, LV_ANIM_OFF);

    //Write style for screen_bar_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen_bar_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_bar_1, 10, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_bar_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(ui->screen_bar_1, &_battery_bak_RGB565A8_26x14, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(ui->screen_bar_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(ui->screen_bar_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write style for screen_bar_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen_bar_1, 0, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_bar_1, 10, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(ui->screen_bar_1, &_battery_ind_RGB565A8_26x14, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(ui->screen_bar_1, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(ui->screen_bar_1, 0, LV_PART_INDICATOR|LV_STATE_DEFAULT);

    //Write codes screen_img_8
    ui->screen_img_8 = lv_image_create(ui->screen);
    lv_obj_set_pos(ui->screen_img_8, 227, 216);
    lv_obj_set_size(ui->screen_img_8, 28, 23);
    lv_obj_add_flag(ui->screen_img_8, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_8, &_light_11_RGB565A8_28x23);
    lv_image_set_pivot(ui->screen_img_8, 50,50);
    lv_image_set_rotation(ui->screen_img_8, 0);

    //Write style for screen_img_8, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->screen_img_8, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_8, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_label_1
    ui->screen_label_1 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_1, 44, 72);
    lv_obj_set_size(ui->screen_label_1, 65, 24);
    lv_label_set_text(ui->screen_label_1, "TRIP");
    lv_label_set_long_mode(ui->screen_label_1, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_1, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_1, &lv_font_montserratMedium_21, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_label_2
    ui->screen_label_2 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_2, 35, 117);
    lv_obj_set_size(ui->screen_label_2, 66, 30);
    lv_label_set_text(ui->screen_label_2, "12.4");
    lv_label_set_long_mode(ui->screen_label_2, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_2, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_2, &lv_font_montserratMedium_32, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_label_3
    ui->screen_label_3 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_3, 438, 130);
    lv_obj_set_size(ui->screen_label_3, 66, 30);
    lv_label_set_text(ui->screen_label_3, "KM");
    lv_label_set_long_mode(ui->screen_label_3, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_3, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_3, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_3, &lv_font_montserratMedium_14, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_label_4
    ui->screen_label_4 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_4, 376, 117);
    lv_obj_set_size(ui->screen_label_4, 61, 31);
    lv_label_set_text(ui->screen_label_4, "300");
    lv_label_set_long_mode(ui->screen_label_4, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_4, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_4, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_4, &lv_font_montserratMedium_32, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_4, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_4, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_4, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_label_5
    ui->screen_label_5 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_5, 102, 130);
    lv_obj_set_size(ui->screen_label_5, 65, 24);
    lv_label_set_text(ui->screen_label_5, "KM");
    lv_label_set_long_mode(ui->screen_label_5, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_5, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_5, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_5, &lv_font_montserratMedium_14, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_5, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_5, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_timer_create(lv_mileage_time_cb, 2000, NULL);

    //Write codes screen_label_6
    ui->screen_label_6 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_6, 376, 72);
    lv_obj_set_size(ui->screen_label_6, 65, 24);
    lv_label_set_text(ui->screen_label_6, "ODO");
    lv_label_set_long_mode(ui->screen_label_6, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_6, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_6, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_6, &lv_font_montserratMedium_21, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_6, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_6, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_6, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_label_7
    ui->screen_label_7 = lv_label_create(ui->screen);
    lv_obj_set_pos(ui->screen_label_7, 189, 102);
    lv_obj_set_size(ui->screen_label_7, 100, 100);
    lv_label_set_text(ui->screen_label_7, "0");
    lv_label_set_long_mode(ui->screen_label_7, LV_LABEL_LONG_WRAP);

    //Write style for screen_label_7, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_label_7, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_label_7, &lv_font_montserratMedium_24, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_label_7, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_label_7, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_7, 40, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_src(ui->screen_label_7, &_kmbg_RGB565A8_100x100, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_opa(ui->screen_label_7, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_label_7, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Update current screen layout.
    lv_obj_update_layout(ui->screen);
}

void lv_setup_bottom_layer(void)
{
    lv_theme_apply(lv_layer_bottom());
}

void lv_setup_ui(void)
{
    lv_setup_bottom_layer();
    lv_setup_scr_screen(&g_ui_screen);
    lv_screen_load(g_ui_screen.screen);
}