#pragma once

#include <components/bk_display.h>

void display_ui_register_cast_hooks_once(void);

/* Public getters for external consumers (boot_avi_play, media_devices, etc.) */
bk_display_ctlr_handle_t display_ui_get_dpu_handle(void);
/* Bound MIPI panel descriptor (timing, name, ...), or NULL if display HW is
 * not up yet. Lets consumers read panel geometry without the display_ctx_t. */
const bk_display_dsi_panel_t *display_ui_get_panel_desc(void);
