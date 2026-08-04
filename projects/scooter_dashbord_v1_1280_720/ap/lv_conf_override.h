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

#undef LV_FS_FATFS_LETTER
#define LV_FS_FATFS_LETTER 'S'

#undef LV_FS_FATFS_CACHE_SIZE
#define LV_FS_FATFS_CACHE_SIZE 512

#endif /* LV_CONF_OVERRIDE_H */
