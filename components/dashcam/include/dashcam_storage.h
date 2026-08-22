#ifndef __DASHCAM_STORAGE_H__
#define __DASHCAM_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/bk_err.h"

#define DASHCAM_STORAGE_DIR            "/sd0/dashcam"
#define DASHCAM_STORAGE_MAX_PATH       128
#define DASHCAM_STORAGE_MAX_NAME       64

typedef struct
{
    char path[DASHCAM_STORAGE_MAX_PATH];
    char name[DASHCAM_STORAGE_MAX_NAME];
    uint32_t size_bytes;
    uint32_t seq;        /* sequence index parsed from filename */
    uint32_t uptime_ms;  /* boot-relative timestamp parsed from filename (plan B) */
    /* Wall-clock fields parsed from the filename; valid only when the build uses
     * wall-clock naming (DASHCAM_USE_WALL_CLOCK, plan A). Zero otherwise. */
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} dashcam_file_info_t;

typedef enum
{
    DASHCAM_MP4_CHECK_VALID = 0,
    DASHCAM_MP4_CHECK_EMPTY,
    DASHCAM_MP4_CHECK_INVALID,
    DASHCAM_MP4_CHECK_IO_ERROR,
} dashcam_mp4_check_result_t;

bk_err_t dashcam_storage_init(void);
bk_err_t dashcam_storage_scan(dashcam_file_info_t *files,
                              uint32_t max_files,
                              uint32_t *file_count);
bk_err_t dashcam_storage_make_record_path(char *path, size_t path_size);
bk_err_t dashcam_storage_start_ftp_server(void);
bool dashcam_storage_is_ftp_started(void);

/* Number of recorded clips currently on the card. */
bk_err_t dashcam_storage_count(uint32_t *count);
/* Free space on the recording volume, in MB (req5 §4.2 容量水位). */
bk_err_t dashcam_storage_free_mb(uint32_t *free_mb);
/* Delete the oldest clip (smallest filename); used for release ring-recycle. */
bk_err_t dashcam_storage_delete_oldest(void);
/* Reclaim space by deleting oldest clips until under the ring file cap. */
bk_err_t dashcam_storage_recycle_for_release(void);
/*
 * Adaptive cleanup before a new segment: orphan idx, zero-byte zombies, ring
 * delete, then last-resort lost-cluster reclaim if free is still below floor.
 */
bk_err_t dashcam_storage_ensure_record_space(void);
/*
 * Begin boot-time lost-cluster reclaim in a background thread so it can overlap
 * DASHCAM_BOOT_RECORD_DELAY_MS. No-op when the dirty marker is absent.
 */
bk_err_t dashcam_storage_boot_reclaim_start(void);
/*
 * Wait for boot reclaim (async or sync) before opening the first MP4.
 */
bk_err_t dashcam_storage_boot_reclaim(void);
/* Mark / clear unclean-recording marker (power-cut during an open MP4). */
void dashcam_storage_mark_recording_dirty(void);
void dashcam_storage_clear_recording_dirty(void);
/* Drop a failed MP4 open (0-byte / header-less zombie) so recycle can recover. */
void dashcam_storage_discard_failed_clip(const char *path);
/* Build the primary human-readable list label for one clip. */
void dashcam_storage_format_label(const dashcam_file_info_t *info, char *buf, size_t size);
/* Classify a finalized MP4 without conflating an empty clip with an I/O error. */
dashcam_mp4_check_result_t dashcam_storage_check_mp4(const char *path);
/* Validate finalized top-level MP4 structure before handing a clip to the player. */
bool dashcam_storage_mp4_is_playable(const char *path);
/* Delete one finalized recording and its optional .idx sidecar. */
bk_err_t dashcam_storage_delete_record(const char *path);
/* Unlink orphan .mp4.idx sidecars; removed/failed may be NULL. */
bk_err_t dashcam_storage_cleanup_orphan_idx(uint32_t *removed, uint32_t *failed);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_STORAGE_H__ */
