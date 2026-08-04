#pragma once

#include <common/bk_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared SD-card mount manager for the boot path.
 *
 * The boot animation (boot_avi_play), the background preloader
 * (boot_bg_preload) and dashcam storage all need the SD card mounted early and
 * must not mount the same volume twice. This module owns that single mount and
 * tracks its state internally, replacing the old g_boot_sd_pre_mounted global.
 *
 * All entry points are idempotent and safe to call from any of those init
 * paths; the first successful call performs the mount, later calls are no-ops.
 */

/* Mount the SD card if not already mounted. Returns BK_OK when the card is
 * available (already mounted or mounted just now), BK_FAIL otherwise. */
bk_err_t boot_sd_mount(void);

/* Unmount the SD card if this module mounted a private FATFS volume.
 * For the VFS backend the mount is a shared, process-wide resource, so this is
 * a no-op (kept alive for dashcam storage / FTP, matching the original boot
 * behavior where the mount was never torn down). */
void boot_sd_unmount(void);

/* True once the SD card has been mounted (by this module or observed present). */
bool boot_sd_is_mounted(void);

#ifdef __cplusplus
}
#endif
