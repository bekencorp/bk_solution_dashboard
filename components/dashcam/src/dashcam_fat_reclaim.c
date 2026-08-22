#include <stdio.h>

/*
 * FatFs-only helpers for dashcam storage maintenance.
 * Keep ff.h out of dashcam_storage.c (bk_vfs.h and ff.h both define DIR).
 */
#include "common/bk_err.h"
#include "components/log.h"
#include "diskio.h"
#include "ff.h"

#define TAG "dashcam_fat"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

bk_err_t dashcam_fat_reclaim_lost(uint32_t *reclaimed_clusters)
{
    char drive[8];
    DWORD reclaimed = 0;
    FRESULT fr;

    if (reclaimed_clusters != NULL)
    {
        *reclaimed_clusters = 0;
    }

    snprintf(drive, sizeof(drive), "%d:", DISK_NUMBER_SDIO_SD);
    fr = f_reclaim_lost(drive, &reclaimed);
    if (fr != FR_OK)
    {
        LOGW("f_reclaim_lost failed: %d\n", (int)fr);
        return BK_FAIL;
    }

    if (reclaimed > 0)
    {
        LOGI("reclaimed %lu lost cluster(s)\n", (unsigned long)reclaimed);
    }

    if (reclaimed_clusters != NULL)
    {
        *reclaimed_clusters = (uint32_t)reclaimed;
    }

    return BK_OK;
}
