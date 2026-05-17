#pragma once

#include <components/bk_display.h>

void display_ui_register_cast_hooks_once(void);

/* Public getters for external consumers (boot_avi_play, media_devices, etc.) */
bk_display_ctlr_handle_t display_ui_get_dpu_handle(void);
bk_display_dpu_config_t *display_ui_get_dpu_config(void);
