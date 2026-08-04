#include "boot_sd_mount.h"

#include <os/os.h>
#include <os/mem.h>
#include <stdio.h>
#include <components/log.h>

#if CONFIG_VFS
/* VFS backend. Do NOT include ff.h here: bk_vfs.h and FatFs both define `DIR`
 * and mixing the two headers is a type conflict. */
#include "bk_vfs.h"
#include "bk_filesystem.h"
#include "bk_partition.h"
#else
#include "ff.h"
#include "diskio.h"
#endif

#define TAG "boot_sd"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bool s_mounted = false;
#if !CONFIG_VFS
static FATFS *s_fs = NULL;
#endif

bk_err_t boot_sd_mount(void)
{
	if (s_mounted)
	{
		return BK_OK;
	}

#if CONFIG_VFS
	struct stat st;
	struct bk_fatfs_partition partition;
	int ret;

	/* Another subsystem may have mounted the same volume already. */
	if (bk_vfs_stat(VFS_SD_0_PATITION_0, &st) == 0)
	{
		s_mounted = true;
		return BK_OK;
	}

	partition.part_type = FATFS_DEVICE;
	partition.part_dev.device_name = FATFS_DEV_SDCARD;
	partition.mount_path = VFS_SD_0_PATITION_0;

	ret = bk_fatfs_mount(&partition, 1);
	if (ret != BK_OK)
	{
		LOGE("bk_fatfs_mount failed: %d\n", ret);
		return BK_FAIL;
	}
#else
	char drive[8];
	FRESULT fr;

	s_fs = os_malloc(sizeof(FATFS));
	if (s_fs == NULL)
	{
		LOGE("FATFS alloc failed\n");
		return BK_FAIL;
	}

	sprintf(drive, "%d:", DISK_NUMBER_SDIO_SD);
	fr = f_mount(s_fs, drive, 1);
	if (fr != FR_OK)
	{
		LOGE("f_mount failed: %d\n", fr);
		os_free(s_fs);
		s_fs = NULL;
		return BK_FAIL;
	}
#endif

	s_mounted = true;
	LOGI("SD card mounted\n");
	return BK_OK;
}

void boot_sd_unmount(void)
{
	if (!s_mounted)
	{
		return;
	}

#if CONFIG_VFS
	/*
	 * VFS SD mount is a shared, process-wide resource (dashcam storage, FTP,
	 * background preloader all rely on it). Keep it mounted for the rest of
	 * runtime, matching the original behavior where the boot mount was never
	 * torn down.
	 */
#else
	{
		char drive[8];

		sprintf(drive, "%d:", DISK_NUMBER_SDIO_SD);
		f_mount(NULL, drive, 0);
	}
	if (s_fs != NULL)
	{
		os_free(s_fs);
		s_fs = NULL;
	}
	s_mounted = false;
#endif
}

bool boot_sd_is_mounted(void)
{
	return s_mounted;
}
