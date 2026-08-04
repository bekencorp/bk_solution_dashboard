#pragma once

#include <components/bk_display.h>

/*
 * Internal getters — for display_ui component only (display_ui.c, display_ui_cast_hooks.c).
 * External consumers should use the public API in display_ui_cast_hooks.h.
 *
 * The display HW (panel / DSI-bus / DPU) is owned by the app_display layer
 * (app_mipi_lcd_turn_on); this component only caches the DPU controller handle
 * and the bound panel descriptor, so there is no per-instance display_ctx_t.
 */
uint8_t *display_ui_get_lvgl_fb(int index);
