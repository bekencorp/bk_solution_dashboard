// Copyright 2026 Beken
//
// SD card MTP (Media Transfer Protocol) control component.
//
// The MTP protocol engine itself lives in the SDK (bk_usb/bk_mtp/bk_usbd_mtp.c,
// gated by CONFIG_USBD_MTP). This component is only the thin control / CLI layer
// that brings the MTP USB device gadget up and down at runtime and exposes the
// on-board SD card filesystem to a PC as a media device.
//
// A project only needs to call sdcard_mtp_init() once (typically during its CLI
// bring-up); the USB gadget is not started until the user issues `mtp start`.

#pragma once

#include <common/bk_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the `mtp` CLI command (mtp start|stop|status).
 *
 * Call once from the project (e.g. in its CLI init). This does NOT bring up the
 * USB gadget; that happens on `mtp start` / sdcard_mtp_start().
 *
 * @return BK_OK on success, otherwise a bk_err_t error code.
 */
bk_err_t sdcard_mtp_init(void);

/**
 * @brief Bring up the MTP device gadget (mount SD at /sd0 + enumerate as MTP).
 * @return 0 on success, negative on failure.
 */
int sdcard_mtp_start(void);

/**
 * @brief Tear down the MTP device gadget.
 * @return 0 on success, negative on failure.
 */
int sdcard_mtp_stop(void);

/**
 * @brief Query whether the MTP gadget is currently active.
 */
bool sdcard_mtp_is_active(void);

/**
 * @brief List the on-board SD card contents (the same /sd0 storage MTP exposes).
 *
 * Mounts the SD card FATFS at /sd0 on demand if needed, then recursively prints
 * every directory and file (name + size) to the log.
 *
 * @param path Directory to list; NULL or "" means the SD root (/sd0).
 * @return 0 on success, negative on failure.
 */
int sdcard_mtp_ls(const char *path);

#ifdef __cplusplus
}
#endif
