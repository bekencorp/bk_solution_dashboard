#ifndef LV_CONF_OVERRIDE_H
#define LV_CONF_OVERRIDE_H

/*
 * Workaround: SDK Kconfig missing LV_USE_GIF / LV_USE_FS_FATFS entries.
 * Include the original lv_conf.h, then override the values we need.
 * TODO: Remove this file once SDK adds proper Kconfig entries.
 */

#include "lv_conf.h"

#undef LV_USE_GIF
#define LV_USE_GIF 1

#undef LV_GIF_USE_PSRAM
#define LV_GIF_USE_PSRAM 1

#undef LV_USE_FS_FATFS
#define LV_USE_FS_FATFS 1

#undef LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1

#undef LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 1

#undef LV_FS_FATFS_LETTER
#define LV_FS_FATFS_LETTER 'S'

#undef LV_FS_FATFS_CACHE_SIZE
#define LV_FS_FATFS_CACHE_SIZE 512

/*
 * The SD card is FATFS physical drive 1 (DISK_NUMBER_SDIO_SD), but LVGL's
 * fsdrv strips only the "S:" prefix and passes the bare path to f_open(),
 * which would target the default drive 0. Prepend "1:" so "S:/foo" maps to
 * "1:/foo" on the SD card.
 */
#undef LV_FS_FATFS_PATH
#define LV_FS_FATFS_PATH "1:"

/* Enable the built-in TinyJPEG decoder so LVGL can read .jpg files directly
 * from the SD card (streaming MCU decode, no full-frame buffer). */
#undef LV_USE_TJPGD
#define LV_USE_TJPGD 1

/*
 * The LVGL SW-draw worker thread ("swdraw") runs the TJPGD decoder, whose
 * decoder_info() puts a 4KB work buffer on the stack. The default 8KB here is
 * effectively quartered by the BK lvgl FreeRTOS port (lv_freertos.c divides by
 * sizeof(StackType_t)=4 but rtos_smp_create_thread treats the arg as BYTES),
 * giving only 2KB -> UsageFault stack overflow. Set 64KB so the effective
 * swdraw stack is 64K/4 = 16KB. (Must be set here, not in defconfig: lv_conf.h
 * hardcodes LV_DRAW_THREAD_STACK_SIZE, so the Kconfig value is ignored.)
 */
#undef LV_DRAW_THREAD_STACK_SIZE
#define LV_DRAW_THREAD_STACK_SIZE (64 * 1024)

#endif /* LV_CONF_OVERRIDE_H */
