#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <common/bk_err.h>

typedef void (*display_ui_init_callback_t)(void);

bk_err_t display_ui_register_init_callback(display_ui_init_callback_t callback);
bk_err_t display_ui_init(void);
bk_err_t display_ui_init_display_hw(void);
bk_err_t display_ui_start_lvgl(void);
void display_ui_register_cast_hooks_once(void);
