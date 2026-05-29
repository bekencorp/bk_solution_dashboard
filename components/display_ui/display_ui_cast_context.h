#pragma once

#include <components/bk_display.h>

typedef struct
{
    uint8_t enable;
    bk_display_ctlr_handle_t dpu_ctlr_handle;
    bk_display_bus_handle_t dis_bus_handle;
    bk_avdk_lcd_panel_handle_t panel_handle;
    /** Bound MIPI panel descriptor (timing, name, ...). Set alongside
     *  @c panel_handle so external consumers can read panel metadata
     *  without going through the opaque panel handle. */
    const bk_display_dsi_panel_t *panel_desc;
} display_ctx_t;

/*
 * Internal getters — for display_ui component only (display_ui.c, display_ui_cast_hooks.c).
 * External consumers should use the public API in display_ui_cast_hooks.h.
 */
display_ctx_t *display_ui_get_ctx(void);
int            display_ui_is_lvgl_enabled(void);
uint8_t       *display_ui_get_lvgl_fb(int index);

/* display_ui_get_dpu_config() is declared in display_ui_cast_hooks.h (public API) */

