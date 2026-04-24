#pragma once

#include <stdint.h>
#include <common/bk_err.h>

bk_err_t display_ui_init(void);
bk_err_t display_ui_init_display_hw(void);
bk_err_t display_ui_start_lvgl(void);
void display_ui_register_cast_hooks_once(void);

uint8_t *lvgl_get_idle_framebuffer(int index, uint32_t *out_size);
