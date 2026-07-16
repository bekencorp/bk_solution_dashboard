#pragma once

#include <common/bk_err.h>

/**
 * Play boot AVI animation from SD card before LVGL starts.
 *
 * Requires display HW to be initialized (display_ui_init_display_hw).
 * If SD card is absent or boot.avi not found, returns BK_OK immediately.
 * Blocks until playback finishes.
 */
bk_err_t boot_avi_play(void);
