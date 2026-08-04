#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <common/bk_err.h>

bk_err_t display_ui_init(void);
bk_err_t display_ui_init_display_hw(void);
bk_err_t display_ui_start_lvgl(void);
void display_ui_register_cast_hooks_once(void);
