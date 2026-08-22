#ifndef KLOK_LV_CONF_CUSTOM_H
#define KLOK_LV_CONF_CUSTOM_H

/*
 * The bk_avdk_smp_release_4.0.1 LVGL configuration hardcodes Snapshot off and
 * does not expose CONFIG_LV_USE_SNAPSHOT through Kconfig. PP-OSD takes an LVGL
 * snapshot for its ARGB8888 overlay, so enable it for this project only.
 */
#include "lv_conf.h"

#undef LV_USE_SNAPSHOT
#define LV_USE_SNAPSHOT 1

/* Runtime CJK metadata font loaded once from /ud0 into PSRAM. */
#undef LV_USE_TINY_TTF
#define LV_USE_TINY_TTF 1
#undef LV_TINY_TTF_FILE_SUPPORT
#define LV_TINY_TTF_FILE_SUPPORT 0

/* The on-board SD-NAND is FatFs drive 1. Map S:/foo to 1:/foo so LVGL can
 * load the runtime TTF after the storage layer mounts /sd0. */
#undef LV_USE_FS_FATFS
#define LV_USE_FS_FATFS 1
#undef LV_FS_FATFS_LETTER
#define LV_FS_FATFS_LETTER 'S'
#undef LV_FS_FATFS_CACHE_SIZE
#define LV_FS_FATFS_CACHE_SIZE 512
#undef LV_FS_FATFS_PATH
#define LV_FS_FATFS_PATH "1:"

/*
 * lv_freertos.c divides this byte count by sizeof(StackType_t) before passing
 * it to the BK SMP thread API, which already expects bytes. The SDK default
 * therefore gives swdraw only 2 KiB and the 1280x280 ARGB8888 OSD snapshot can
 * stall after overflowing it. Request 64 KiB so the effective stack is 16 KiB.
 */
#undef LV_DRAW_THREAD_STACK_SIZE
#define LV_DRAW_THREAD_STACK_SIZE (64 * 1024)

#endif /* KLOK_LV_CONF_CUSTOM_H */
