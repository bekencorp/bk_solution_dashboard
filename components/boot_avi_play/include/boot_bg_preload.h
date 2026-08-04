#pragma once

#include <common/bk_include.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Background pre-loader.
 *
 * The home background is a 1280x720 image. Decoding a JPEG for it at UI-init
 * time costs ~1s and would show up as a gap after the boot animation. Instead
 * the background JPEG is stored on the SD card (1:/home_bg.jpg) and decoded to
 * an RGB565 bitmap in PSRAM by a worker thread that runs concurrently with the
 * boot animation (FATFS is reentrant, so the worker can read the SD while
 * boot_avi_play() reads boot.avi). The decode uses LVGL's software JPEG decoder
 * (TJPGD) called directly, because lv_init() has not run yet at this point.
 * When the animation ends, beken_ui_init() wraps the already-decoded buffer as
 * an LVGL image with no further decoding, so the UI appears instantly.
 */

/* Mount the SD card (shared) and start the worker that loads the background. */
void boot_bg_preload_start(void);

/*
 * Wait up to timeout_ms for the background load to finish and return it as an
 * LVGL image descriptor referencing the PSRAM buffer. Returns NULL on
 * failure/timeout (caller should fall back to decoding the JPEG).
 */
const lv_image_dsc_t *boot_bg_preload_get(uint32_t timeout_ms);

/* Release the preloader's transient resources (keeps the PSRAM image + SD mount). */
void boot_bg_preload_finish(void);

#ifdef __cplusplus
}
#endif
