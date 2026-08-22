#include "dashcam_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "bk_std_header.h"
#include "bk_vfs.h"
#include "bk_filesystem.h"
#include "bk_partition.h"
#include "components/log.h"
#include "dashcam_config.h"
#include "os/os.h"
#include "boot_sd_mount.h"

#ifndef DASHCAM_RECYCLE_MAX_DELETE
#define DASHCAM_RECYCLE_MAX_DELETE         32u
#endif
#ifndef DASHCAM_BOOT_RECLAIM_LOST
#define DASHCAM_BOOT_RECLAIM_LOST          1
#endif
/* 1: reclaim on boot only if the previous session left a dirty marker (power-cut). */
#ifndef DASHCAM_BOOT_RECLAIM_ONLY_IF_DIRTY
#define DASHCAM_BOOT_RECLAIM_ONLY_IF_DIRTY 1
#endif

#define DASHCAM_REC_DIRTY_MARKER           DASHCAM_STORAGE_DIR "/.rec_dirty"
#define DASHCAM_RECLAIM_MIGRATED_MARKER    DASHCAM_STORAGE_DIR "/.reclaim_ok"
#define DASHCAM_RECLAIM_THREAD_STACK       4096
#define DASHCAM_RECLAIM_THREAD_PRIO        5
#define DASHCAM_RECLAIM_WAIT_MS            10000u

#if DASHCAM_USE_WALL_CLOCK
#include "components/app_time_intf.h"
#endif

#if CONFIG_FTP_SERVER
#include "ftpd.h"
#endif

#define TAG "dashcam_storage"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

extern bk_err_t dashcam_fat_reclaim_lost(uint32_t *reclaimed_clusters);

static bool s_storage_ready = false;
static bool s_ftp_started = false;
static uint32_t s_record_index = 0;
static bool s_boot_reclaim_done = false;
static volatile bool s_reclaim_async_running = false;
static beken_semaphore_t s_reclaim_done_sem = NULL;

static bool dashcam_storage_has_video_suffix(const char *name)
{
    size_t len;
    const char *suffix;

    if (name == NULL)
    {
        return false;
    }

    len = strlen(name);
    if (len < 4)
    {
        return false;
    }

    suffix = &name[len - 4];
    return ((suffix[0] == '.') &&
            (suffix[1] == 'm' || suffix[1] == 'M' || suffix[1] == 'a' || suffix[1] == 'A') &&
            (suffix[2] == 'p' || suffix[2] == 'P' || suffix[2] == 'v' || suffix[2] == 'V') &&
            (suffix[3] == '4' || suffix[3] == 'i' || suffix[3] == 'I'));
}

static bool dashcam_storage_has_mp4_suffix(const char *name)
{
    size_t len;
    const char *suffix;

    if (name == NULL)
    {
        return false;
    }

    len = strlen(name);
    if (len < 4)
    {
        return false;
    }

    suffix = &name[len - 4];
    return (suffix[0] == '.' &&
            (suffix[1] == 'm' || suffix[1] == 'M') &&
            (suffix[2] == 'p' || suffix[2] == 'P') &&
            suffix[3] == '4');
}

static uint32_t dashcam_storage_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static uint64_t dashcam_storage_be64(const uint8_t *data)
{
    return ((uint64_t)dashcam_storage_be32(data) << 32) |
           dashcam_storage_be32(data + 4);
}

dashcam_mp4_check_result_t dashcam_storage_check_mp4(const char *path)
{
    static const uint32_t box_ftyp = 0x66747970U;
    static const uint32_t box_mdat = 0x6D646174U;
    static const uint32_t box_moov = 0x6D6F6F76U;
    uint8_t header[16];
    bool saw_mdat = false;
    bool has_mdat_payload = false;
    bool has_moov = false;
    bool malformed = false;
    int fd;
    off_t file_size;
    uint64_t offset = 0U;

    if (path == NULL || path[0] == '\0')
    {
        return DASHCAM_MP4_CHECK_INVALID;
    }

    fd = bk_vfs_open(path, O_RDONLY);
    if (fd < 0)
    {
        return DASHCAM_MP4_CHECK_IO_ERROR;
    }

    file_size = bk_vfs_lseek(fd, 0, SEEK_END);
    if (file_size < 0 || bk_vfs_lseek(fd, 0, SEEK_SET) < 0)
    {
        bk_vfs_close(fd);
        return DASHCAM_MP4_CHECK_IO_ERROR;
    }
    if (file_size == 0)
    {
        bk_vfs_close(fd);
        return DASHCAM_MP4_CHECK_EMPTY;
    }
    if (file_size < 8)
    {
        bk_vfs_close(fd);
        return DASHCAM_MP4_CHECK_INVALID;
    }

    while (offset + 8U <= (uint64_t)file_size)
    {
        uint32_t size32;
        uint32_t type;
        uint32_t header_size = 8U;
        uint64_t box_size;

        if (bk_vfs_read(fd, header, 8U) != 8)
        {
            bk_vfs_close(fd);
            return DASHCAM_MP4_CHECK_IO_ERROR;
        }

        size32 = dashcam_storage_be32(header);
        type = dashcam_storage_be32(header + 4);
        box_size = size32;
        if (size32 == 1U)
        {
            if (bk_vfs_read(fd, header + 8, 8U) != 8)
            {
                bk_vfs_close(fd);
                return DASHCAM_MP4_CHECK_IO_ERROR;
            }
            box_size = dashcam_storage_be64(header + 8);
            header_size = 16U;
        }

        if ((offset == 0U && type != box_ftyp) ||
            box_size < header_size ||
            box_size > (uint64_t)file_size - offset)
        {
            malformed = true;
            break;
        }

        if (type == box_mdat)
        {
            saw_mdat = true;
            has_mdat_payload = has_mdat_payload || box_size > header_size;
        }
        has_moov = has_moov || type == box_moov;
        offset += box_size;
        if (has_mdat_payload && has_moov)
        {
            break;
        }
        if (bk_vfs_lseek(fd, (off_t)offset, SEEK_SET) < 0)
        {
            bk_vfs_close(fd);
            return DASHCAM_MP4_CHECK_IO_ERROR;
        }
    }

    bk_vfs_close(fd);
    if (saw_mdat && !has_mdat_payload)
    {
        return DASHCAM_MP4_CHECK_EMPTY;
    }
    if (!malformed && has_mdat_payload && has_moov)
    {
        return DASHCAM_MP4_CHECK_VALID;
    }
    return DASHCAM_MP4_CHECK_INVALID;
}

bool dashcam_storage_mp4_is_playable(const char *path)
{
    return dashcam_storage_check_mp4(path) == DASHCAM_MP4_CHECK_VALID;
}

static bk_err_t dashcam_storage_mount_sd(void)
{
    /* Centralized, idempotent SD mount shared with the boot animation and the
     * background preloader (boot_sd_mount component). */
    return boot_sd_mount();
}

bk_err_t dashcam_storage_init(void)
{
    struct stat st;

    if (s_storage_ready)
    {
        return BK_OK;
    }

    if (dashcam_storage_mount_sd() != BK_OK)
    {
        return BK_FAIL;
    }

    if (bk_vfs_stat(DASHCAM_STORAGE_DIR, &st) == 0)
    {
        s_storage_ready = true;
        return BK_OK;
    }

    if (bk_vfs_mkdir(DASHCAM_STORAGE_DIR, 0) != 0)
    {
        LOGE("mkdir %s failed\n", DASHCAM_STORAGE_DIR);
        return BK_FAIL;
    }

    s_storage_ready = true;
    LOGI("created %s\n", DASHCAM_STORAGE_DIR);
    return BK_OK;
}

static void dashcam_storage_insert_file(dashcam_file_info_t *files,
                                        uint32_t max_files,
                                        uint32_t *file_count,
                                        const dashcam_file_info_t *item)
{
    uint32_t pos = 0;
    uint32_t count = *file_count;

    while (pos < count && strcmp(files[pos].name, item->name) > 0)
    {
        pos++;
    }

    if (pos >= max_files)
    {
        return;
    }

    if (count < max_files)
    {
        count++;
    }

    for (uint32_t i = count - 1; i > pos; i--)
    {
        files[i] = files[i - 1];
    }

    files[pos] = *item;
    *file_count = count;
}

bk_err_t dashcam_storage_scan(dashcam_file_info_t *files,
                              uint32_t max_files,
                              uint32_t *file_count)
{
    DIR *dir;
    struct dirent *entry;

    if (files == NULL || file_count == NULL || max_files == 0)
    {
        return BK_ERR_PARAM;
    }

    *file_count = 0;
    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    LOGD("scan %s (max=%u)\n", DASHCAM_STORAGE_DIR, (unsigned)max_files);

    dir = bk_vfs_opendir(DASHCAM_STORAGE_DIR);
    if (dir == NULL)
    {
        LOGE("opendir %s failed\n", DASHCAM_STORAGE_DIR);
        return BK_FAIL;
    }

    while ((entry = bk_vfs_readdir(dir)) != NULL)
    {
        dashcam_file_info_t item = {0};
        dashcam_mp4_check_result_t check_result;

        if (!dashcam_storage_has_video_suffix(entry->d_name))
        {
            continue;
        }

        /*
         * files[] is kept newest-first. Once it is full, entries no newer than
         * the current tail cannot enter the result set, so avoid formatting and
         * parsing metadata for them. The directory still has to be traversed,
         * but the work per old clip stays minimal.
         */
        if (*file_count == max_files &&
            strcmp(entry->d_name, files[max_files - 1].name) <= 0)
        {
            continue;
        }

        snprintf(item.name, sizeof(item.name), "%s", entry->d_name);
        snprintf(item.path, sizeof(item.path), "%s/%s", DASHCAM_STORAGE_DIR, entry->d_name);

        if (dashcam_storage_has_mp4_suffix(entry->d_name))
        {
            check_result = dashcam_storage_check_mp4(item.path);
            if (check_result == DASHCAM_MP4_CHECK_EMPTY ||
                check_result == DASHCAM_MP4_CHECK_INVALID)
            {
                LOGW("delete unplayable recording: %s check=%d\n",
                     item.path, (int)check_result);
                (void)dashcam_storage_delete_record(item.path);
                continue;
            }
        }

        /*
         * FatFs readdir already returns the file size in struct dirent. Calling
         * bk_vfs_stat() here performs another path lookup for every clip and is
         * prohibitively slow when the release ring contains many files.
         * Retain a stat fallback for VFS backends that do not provide metadata.
         */
        if (entry->d_stat_valid)
        {
            item.size_bytes = (uint32_t)entry->d_size;
        }
        else
        {
            struct stat st;

            if (bk_vfs_stat(item.path, &st) == 0)
            {
                item.size_bytes = (uint32_t)st.st_size;
            }
        }

        {
            unsigned int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
            unsigned long uptime = 0;
            unsigned long seq = 0;

            if (sscanf(entry->d_name, "dashcam_%4u%2u%2u_%2u%2u%2u_%lu.mp4",
                       &y, &mo, &d, &h, &mi, &s, &seq) == 7)
            {
                item.year = (uint16_t)y;
                item.month = (uint8_t)mo;
                item.day = (uint8_t)d;
                item.hour = (uint8_t)h;
                item.minute = (uint8_t)mi;
                item.second = (uint8_t)s;
                item.seq = (uint32_t)seq;
            }
            else if (sscanf(entry->d_name, "dashcam_%lu_%lu.mp4",
                            &uptime, &seq) == 2)
            {
                item.uptime_ms = (uint32_t)uptime;
                item.seq = (uint32_t)seq;
            }
        }

        dashcam_storage_insert_file(files, max_files, file_count, &item);
    }

    bk_vfs_closedir(dir);
    LOGD("scan done: %u file(s)\n", (unsigned)*file_count);
    return BK_OK;
}

bk_err_t dashcam_storage_make_record_path(char *path, size_t path_size)
{
    uint32_t now_ms;

    if (path == NULL || path_size == 0)
    {
        return BK_ERR_PARAM;
    }

    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    s_record_index++;

#if DASHCAM_USE_WALL_CLOCK
    {
        /* Plan A: name by real date/time once the RTC has been set (SNTP). */
        user_datetime_t dt = {0};
        if (app_time_datetime_get(&dt) == 0 && dt.year >= 2020)
        {
            snprintf(path,
                     path_size,
                     "%s/dashcam_%04u%02u%02u_%02u%02u%02u_%03u.mp4",
                     DASHCAM_STORAGE_DIR,
                     (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.mday,
                     (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
                     (unsigned)(s_record_index % 1000u));
            LOGD("record path (wall): %s\n", path);
            return BK_OK;
        }
        LOGW("wall clock not ready, fall back to uptime name\n");
    }
#endif

    now_ms = rtos_get_time();
    snprintf(path,
             path_size,
             "%s/dashcam_%010u_%03u.mp4",
             DASHCAM_STORAGE_DIR,
             (unsigned)now_ms,
             (unsigned)(s_record_index % 1000u));
    LOGD("record path: %s\n", path);
    return BK_OK;
}

bk_err_t dashcam_storage_start_ftp_server(void)
{
    if (s_ftp_started)
    {
        return BK_OK;
    }

    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

#if CONFIG_FTP_SERVER
    bk_err_t ret = ftpd_server_init();
    if (ret != BK_OK)
    {
        LOGE("ftpd_server_init failed: %d\n", ret);
        return ret;
    }

    s_ftp_started = true;
    LOGI("FTP server started at %s\n", VFS_SD_0_PATITION_0);
    return BK_OK;
#else
    LOGW("CONFIG_FTP_SERVER is disabled\n");
    return BK_ERR_NOT_SUPPORT;
#endif
}

bool dashcam_storage_is_ftp_started(void)
{
    return s_ftp_started;
}

bk_err_t dashcam_storage_count(uint32_t *count)
{
    DIR *dir;
    struct dirent *entry;
    uint32_t n = 0;

    if (count == NULL)
    {
        return BK_ERR_PARAM;
    }

    *count = 0;
    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    dir = bk_vfs_opendir(DASHCAM_STORAGE_DIR);
    if (dir == NULL)
    {
        return BK_FAIL;
    }

    while ((entry = bk_vfs_readdir(dir)) != NULL)
    {
        if (dashcam_storage_has_video_suffix(entry->d_name))
        {
            n++;
        }
    }

    bk_vfs_closedir(dir);
    *count = n;
    return BK_OK;
}

static bool dashcam_storage_is_idx_sidecar_name(const char *name)
{
    size_t len;

    if (name == NULL)
    {
        return false;
    }

    len = strlen(name);
    if (len <= 8)
    {
        return false;
    }

    return (strcmp(name + len - 8, ".mp4.idx") == 0);
}

static void dashcam_storage_unlink_sidecar_idx(const char *mp4_path)
{
    char idx_path[DASHCAM_STORAGE_MAX_PATH + 4];
    struct stat st;
    int ret;

    snprintf(idx_path, sizeof(idx_path), "%s.idx", mp4_path);
    if (bk_vfs_stat(idx_path, &st) != 0)
    {
        return;
    }

    ret = bk_vfs_unlink(idx_path);
    if (ret != 0)
    {
        LOGW("unlink sidecar %s failed: %d\n", idx_path, ret);
    }
}

bk_err_t dashcam_storage_delete_record(const char *path)
{
    size_t dir_len = strlen(DASHCAM_STORAGE_DIR);
    int ret;

    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }

    /* Only permit deletion of files directly under the recording directory. */
    if (strncmp(path, DASHCAM_STORAGE_DIR, dir_len) != 0 ||
        path[dir_len] != '/' ||
        strchr(path + dir_len + 1U, '/') != NULL ||
        !dashcam_storage_has_video_suffix(path))
    {
        LOGE("refuse to delete non-record path: %s\n", path);
        return BK_ERR_PARAM;
    }

    ret = bk_vfs_unlink(path);
    if (ret != 0)
    {
        LOGE("unlink %s failed: %d\n", path, ret);
        return BK_FAIL;
    }

    dashcam_storage_unlink_sidecar_idx(path);
    LOGI("deleted invalid recording: %s\n", path);
    return BK_OK;
}

static bk_err_t dashcam_storage_unlink_clip(const char *clip_name)
{
    char path[DASHCAM_STORAGE_MAX_PATH];
    int ret;

    snprintf(path, sizeof(path), "%s/%s", DASHCAM_STORAGE_DIR, clip_name);
    ret = bk_vfs_unlink(path);
    if (ret != 0)
    {
        LOGE("unlink %s failed: %d\n", path, ret);
        return BK_FAIL;
    }

    dashcam_storage_unlink_sidecar_idx(path);
    LOGI("recycled clip: %s\n", path);
    return BK_OK;
}

static bool dashcam_storage_skip_list_has(const char skip_names[][DASHCAM_STORAGE_MAX_NAME],
                                          uint32_t skip_count,
                                          const char *name)
{
    uint32_t i;

    if (name == NULL || name[0] == '\0')
    {
        return false;
    }

    for (i = 0; i < skip_count; i++)
    {
        if (strcmp(skip_names[i], name) == 0)
        {
            return true;
        }
    }

    return false;
}

static bk_err_t dashcam_storage_delete_oldest_skip(const char skip_names[][DASHCAM_STORAGE_MAX_NAME],
                                                   uint32_t skip_count,
                                                   char *attempted_name,
                                                   size_t attempted_size)
{
    DIR *dir;
    struct dirent *entry;
    char oldest[DASHCAM_STORAGE_MAX_NAME] = {0};
    bool found = false;

    if (attempted_name != NULL && attempted_size > 0)
    {
        attempted_name[0] = '\0';
    }

    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    dir = bk_vfs_opendir(DASHCAM_STORAGE_DIR);
    if (dir == NULL)
    {
        return BK_FAIL;
    }

    while ((entry = bk_vfs_readdir(dir)) != NULL)
    {
        if (!dashcam_storage_has_video_suffix(entry->d_name))
        {
            continue;
        }

        if (skip_count > 0 &&
            dashcam_storage_skip_list_has(skip_names, skip_count, entry->d_name))
        {
            continue;
        }

        if (!found || strcmp(entry->d_name, oldest) < 0)
        {
            snprintf(oldest, sizeof(oldest), "%s", entry->d_name);
            found = true;
        }
    }

    bk_vfs_closedir(dir);

    if (!found)
    {
        return BK_FAIL;
    }

    if (attempted_name != NULL && attempted_size > 0)
    {
        snprintf(attempted_name, attempted_size, "%s", oldest);
    }

    return dashcam_storage_unlink_clip(oldest);
}

static bk_err_t dashcam_storage_space_meets_floor(uint32_t *free_mb_out)
{
    uint32_t free_mb = 0;

    if (dashcam_storage_free_mb(&free_mb) != BK_OK)
    {
        return BK_FAIL;
    }

    if (free_mb_out != NULL)
    {
        *free_mb_out = free_mb;
    }

    return (free_mb >= DASHCAM_RECYCLE_MIN_FREE_MB) ? BK_OK : BK_FAIL;
}

static bk_err_t dashcam_storage_unlink_path(const char *path)
{
    int ret;

    if (path == NULL || path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }

    ret = bk_vfs_unlink(path);
    if (ret != 0)
    {
        return BK_FAIL;
    }

    dashcam_storage_unlink_sidecar_idx(path);
    return BK_OK;
}

static uint32_t dashcam_storage_cleanup_zero_byte_clips(void)
{
    DIR *dir;
    struct dirent *entry;
    uint32_t removed = 0;

    if (dashcam_storage_init() != BK_OK)
    {
        return 0;
    }

    dir = bk_vfs_opendir(DASHCAM_STORAGE_DIR);
    if (dir == NULL)
    {
        return 0;
    }

    while ((entry = bk_vfs_readdir(dir)) != NULL)
    {
        char path[DASHCAM_STORAGE_MAX_PATH];
        struct stat st;

        if (!dashcam_storage_has_video_suffix(entry->d_name))
        {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", DASHCAM_STORAGE_DIR, entry->d_name);
        if (bk_vfs_stat(path, &st) != 0)
        {
            continue;
        }

        if (st.st_size > 0)
        {
            continue;
        }

        if (dashcam_storage_unlink_path(path) == BK_OK)
        {
            removed++;
            LOGI("removed zero-byte clip: %s\n", path);
        }
    }

    bk_vfs_closedir(dir);
    return removed;
}

static bk_err_t dashcam_storage_delete_until_space(void)
{
    char skip_names[DASHCAM_RECYCLE_UNLINK_RETRY_MAX][DASHCAM_STORAGE_MAX_NAME];
    char attempted[DASHCAM_STORAGE_MAX_NAME] = {0};
    uint32_t skip_count = 0;
    uint32_t unlink_fail_streak = 0;
    uint32_t pass;

    for (pass = 0; pass < DASHCAM_RECYCLE_MAX_DELETE; pass++)
    {
        uint32_t free_mb = 0;

        if (dashcam_storage_space_meets_floor(&free_mb) == BK_OK)
        {
            return BK_OK;
        }

        if (dashcam_storage_delete_oldest_skip(skip_count ? skip_names : NULL,
                                               skip_count,
                                               attempted, sizeof(attempted)) == BK_OK)
        {
            skip_count = 0;
            unlink_fail_streak = 0;
            continue;
        }

        if (attempted[0] != '\0' &&
            !dashcam_storage_skip_list_has(skip_names, skip_count, attempted) &&
            skip_count < DASHCAM_RECYCLE_UNLINK_RETRY_MAX)
        {
            snprintf(skip_names[skip_count], sizeof(skip_names[skip_count]), "%s", attempted);
            skip_count++;
        }

        unlink_fail_streak++;
        LOGW("recycle unlink fail streak=%u (skipped %s)\n",
             (unsigned)unlink_fail_streak, attempted);
        if (unlink_fail_streak >= DASHCAM_RECYCLE_UNLINK_RETRY_MAX)
        {
            LOGE("recycle: too many unlink failures, stop this round\n");
            break;
        }
    }

    return dashcam_storage_space_meets_floor(NULL);
}

bk_err_t dashcam_storage_delete_oldest(void)
{
    return dashcam_storage_delete_oldest_skip(NULL, 0, NULL, 0);
}

bk_err_t dashcam_storage_free_mb(uint32_t *free_mb)
{
    struct statfs fs = {0};
    uint64_t free_bytes;

    if (free_mb == NULL)
    {
        return BK_ERR_PARAM;
    }

    *free_mb = 0;
    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    if (bk_vfs_statfs(VFS_SD_0_PATITION_0, &fs) != 0)
    {
        LOGW("statfs %s failed\n", VFS_SD_0_PATITION_0);
        return BK_FAIL;
    }

    free_bytes = (uint64_t)fs.f_bsize * (uint64_t)fs.f_bavail;
    *free_mb = (uint32_t)(free_bytes / (1024u * 1024u));
    return BK_OK;
}

static bool dashcam_storage_marker_present(const char *path)
{
    struct stat st;

    return (path != NULL && bk_vfs_stat(path, &st) == 0);
}

static void dashcam_storage_touch_marker(const char *path)
{
    int fd;
    char mark = '1';

    if (path == NULL)
    {
        return;
    }

    fd = bk_vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0)
    {
        return;
    }

    (void)bk_vfs_write(fd, &mark, 1);
    (void)bk_vfs_fsync(fd);
    (void)bk_vfs_close(fd);
}

void dashcam_storage_mark_recording_dirty(void)
{
    if (dashcam_storage_init() != BK_OK)
    {
        return;
    }

    dashcam_storage_touch_marker(DASHCAM_REC_DIRTY_MARKER);
}

void dashcam_storage_clear_recording_dirty(void)
{
    if (dashcam_storage_init() != BK_OK)
    {
        return;
    }

    if (bk_vfs_unlink(DASHCAM_REC_DIRTY_MARKER) == 0)
    {
        LOGD("cleared recording dirty marker\n");
    }
}

static bk_err_t dashcam_storage_run_reclaim(void)
{
    uint32_t reclaimed = 0;
    uint32_t free_before = 0;
    uint32_t free_after = 0;

    (void)dashcam_storage_free_mb(&free_before);
    if (dashcam_fat_reclaim_lost(&reclaimed) != BK_OK)
    {
        return BK_FAIL;
    }

    (void)dashcam_storage_free_mb(&free_after);
    LOGI("reclaim done: clusters=%u free %u->%u MB\n",
         (unsigned)reclaimed, (unsigned)free_before, (unsigned)free_after);
    dashcam_storage_clear_recording_dirty();
    dashcam_storage_touch_marker(DASHCAM_RECLAIM_MIGRATED_MARKER);
    return BK_OK;
}

static bool dashcam_storage_boot_reclaim_should_run(void)
{
#if !DASHCAM_BOOT_RECLAIM_LOST
    return false;
#elif DASHCAM_BOOT_RECLAIM_ONLY_IF_DIRTY
    /* Dirty: last session was cut mid-record. Missing .reclaim_ok: one-shot
     * migration for cards that already had lost clusters before dirty tracking. */
    return dashcam_storage_marker_present(DASHCAM_REC_DIRTY_MARKER) ||
           !dashcam_storage_marker_present(DASHCAM_RECLAIM_MIGRATED_MARKER);
#else
    return true;
#endif
}

static void dashcam_storage_reclaim_async_thread(void *arg)
{
    (void)arg;

    if (dashcam_storage_boot_reclaim_should_run())
    {
        if (dashcam_storage_marker_present(DASHCAM_REC_DIRTY_MARKER))
        {
            LOGI("boot reclaim (async): dirty marker, scanning lost clusters\n");
        }
        else
        {
            LOGI("boot reclaim (async): one-shot migration, scanning lost clusters\n");
        }
        (void)dashcam_storage_run_reclaim();
    }
    else
    {
        LOGD("boot reclaim (async): skip (clean)\n");
    }

    s_boot_reclaim_done = true;
    s_reclaim_async_running = false;
    if (s_reclaim_done_sem != NULL)
    {
        rtos_set_semaphore(&s_reclaim_done_sem);
    }
    rtos_delete_thread(NULL);
}

bk_err_t dashcam_storage_boot_reclaim_start(void)
{
    bk_err_t ret;

    if (s_boot_reclaim_done || s_reclaim_async_running)
    {
        return BK_OK;
    }

    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    if (!dashcam_storage_boot_reclaim_should_run())
    {
        s_boot_reclaim_done = true;
        LOGD("boot reclaim: skip (clean)\n");
        return BK_OK;
    }

    if (s_reclaim_done_sem == NULL)
    {
        ret = rtos_init_semaphore(&s_reclaim_done_sem, 1);
        if (ret != BK_OK)
        {
            LOGW("reclaim sem init failed, run sync\n");
            ret = dashcam_storage_run_reclaim();
            s_boot_reclaim_done = true;
            return ret;
        }
    }

    s_reclaim_async_running = true;
    ret = rtos_create_thread(NULL,
                             DASHCAM_RECLAIM_THREAD_PRIO,
                             "dcam_reclaim",
                             (beken_thread_function_t)dashcam_storage_reclaim_async_thread,
                             DASHCAM_RECLAIM_THREAD_STACK,
                             NULL);
    if (ret != BK_OK)
    {
        s_reclaim_async_running = false;
        LOGW("reclaim thread create failed, run sync\n");
        ret = dashcam_storage_run_reclaim();
        s_boot_reclaim_done = true;
        return ret;
    }

    LOGI("boot reclaim: started in background (overlap boot delay)\n");
    return BK_OK;
}

bk_err_t dashcam_storage_boot_reclaim(void)
{
    bk_err_t ret;

    if (s_boot_reclaim_done)
    {
        return BK_OK;
    }

    if (s_reclaim_async_running && s_reclaim_done_sem != NULL)
    {
        (void)rtos_get_semaphore(&s_reclaim_done_sem, DASHCAM_RECLAIM_WAIT_MS);
        if (s_boot_reclaim_done)
        {
            return BK_OK;
        }
        /* Timed out or race: fall through to sync. */
    }

#if !DASHCAM_BOOT_RECLAIM_LOST
    s_boot_reclaim_done = true;
    return BK_OK;
#else
    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    if (!dashcam_storage_boot_reclaim_should_run())
    {
        s_boot_reclaim_done = true;
        LOGD("boot reclaim: skip (clean)\n");
        return BK_OK;
    }

    LOGI("boot reclaim: scanning lost clusters before first record\n");
    ret = dashcam_storage_run_reclaim();
    s_boot_reclaim_done = true;
    return ret;
#endif
}

bk_err_t dashcam_storage_recycle_for_release(void)
{
    return dashcam_storage_ensure_record_space();
}

bk_err_t dashcam_storage_ensure_record_space(void)
{
    uint32_t free_mb = 0;
    uint32_t orphan_removed = 0;
    uint32_t orphan_failed = 0;
    uint32_t reclaimed = 0;
    uint32_t zombies;

    if (dashcam_storage_space_meets_floor(&free_mb) == BK_OK)
    {
        return BK_OK;
    }

    LOGW("ensure space: free=%u MB target=%u MB\n",
         (unsigned)free_mb, (unsigned)DASHCAM_RECYCLE_MIN_FREE_MB);

    (void)dashcam_storage_cleanup_orphan_idx(&orphan_removed, &orphan_failed);
    if (orphan_removed > 0)
    {
        LOGI("orphan idx cleanup: removed=%u failed=%u\n",
             (unsigned)orphan_removed, (unsigned)orphan_failed);
    }

    zombies = dashcam_storage_cleanup_zero_byte_clips();
    if (dashcam_storage_space_meets_floor(&free_mb) == BK_OK)
    {
        LOGI("space OK after cleanup (free=%u MB)\n", (unsigned)free_mb);
        return BK_OK;
    }

    if (dashcam_storage_delete_until_space() == BK_OK)
    {
        (void)dashcam_storage_free_mb(&free_mb);
        LOGI("space OK after ring delete (free=%u MB)\n", (unsigned)free_mb);
        return BK_OK;
    }

    /* Last resort only when free is still below floor (not opportunistic). */
    (void)dashcam_fat_reclaim_lost(&reclaimed);
    if (reclaimed > 0)
    {
        dashcam_storage_clear_recording_dirty();
        dashcam_storage_touch_marker(DASHCAM_RECLAIM_MIGRATED_MARKER);
    }
    if (dashcam_storage_space_meets_floor(&free_mb) == BK_OK)
    {
        LOGI("space OK after lost-cluster reclaim (free=%u MB)\n", (unsigned)free_mb);
        return BK_OK;
    }

    if (dashcam_storage_delete_until_space() == BK_OK)
    {
        (void)dashcam_storage_free_mb(&free_mb);
        LOGI("space OK after reclaim+delete (free=%u MB)\n", (unsigned)free_mb);
        return BK_OK;
    }

    if (dashcam_storage_space_meets_floor(&free_mb) == BK_OK)
    {
        return BK_OK;
    }

    LOGE("ensure space failed: free=%u MB target=%u MB zombies=%u reclaimed=%u\n",
         (unsigned)free_mb, (unsigned)DASHCAM_RECYCLE_MIN_FREE_MB,
         (unsigned)zombies, (unsigned)reclaimed);
    return BK_FAIL;
}

void dashcam_storage_discard_failed_clip(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0')
    {
        return;
    }

    if (bk_vfs_stat(path, &st) != 0)
    {
        return;
    }

    /* Failed mp4_record_start leaves FA_CREATE_ALWAYS entries with no payload. */
    if (st.st_size > 32)
    {
        return;
    }

    if (dashcam_storage_unlink_path(path) == BK_OK)
    {
        LOGW("discarded failed clip: %s (%ld bytes)\n", path, (long)st.st_size);
    }
}

void dashcam_storage_format_label(const dashcam_file_info_t *info, char *buf, size_t size)
{
    if (buf == NULL || size == 0)
    {
        return;
    }

    if (info == NULL)
    {
        buf[0] = '\0';
        return;
    }

    /* Show wall-clock recordings as a readable date regardless of the current
     * naming configuration, so clips created under either mode remain usable. */
    if (info->year >= 2020)
    {
        snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)info->year, (unsigned)info->month, (unsigned)info->day,
                 (unsigned)info->hour, (unsigned)info->minute, (unsigned)info->second);
        return;
    }

    /* Uptime-named recordings have no real date; preserve the complete
     * dashcam_uptime_seq.mp4 filename instead of showing only the sequence. */
    if (info->name[0] != '\0')
    {
        snprintf(buf, size, "%s", info->name);
        return;
    }

#if DASHCAM_STORAGE_LABEL_WITH_SIZE
    if (info->size_bytes >= (1024U * 1024U))
    {
        uint32_t size_mb = info->size_bytes / (1024U * 1024U);
        uint32_t size_mb_tenth =
            ((info->size_bytes % (1024U * 1024U)) * 10U) / (1024U * 1024U);
        snprintf(buf, size, "REC %03u  %u.%uMB",
                 (unsigned)info->seq,
                 (unsigned)size_mb,
                 (unsigned)size_mb_tenth);
    }
    else
    {
        snprintf(buf, size, "REC %03u  %uKB",
                 (unsigned)info->seq,
                 (unsigned)(info->size_bytes / 1024U));
    }
#else
    /* The dashboard UI displays size in a separate label below the title. */
    snprintf(buf, size, "REC %03u", (unsigned)info->seq);
#endif
}

void dashboard_network_ready_hook(void)
{
    (void)dashcam_storage_start_ftp_server();
}

bk_err_t dashcam_storage_cleanup_orphan_idx(uint32_t *removed, uint32_t *failed)
{
    DIR *dir;
    struct dirent *entry;
    uint32_t n_removed = 0;
    uint32_t n_failed = 0;

    if (removed != NULL)
    {
        *removed = 0;
    }
    if (failed != NULL)
    {
        *failed = 0;
    }

    if (dashcam_storage_init() != BK_OK)
    {
        return BK_FAIL;
    }

    dir = bk_vfs_opendir(DASHCAM_STORAGE_DIR);
    if (dir == NULL)
    {
        return BK_FAIL;
    }

    while ((entry = bk_vfs_readdir(dir)) != NULL)
    {
        char mp4_path[DASHCAM_STORAGE_MAX_PATH];
        char idx_path[DASHCAM_STORAGE_MAX_PATH];
        struct stat st;
        size_t name_len;
        int ret;

        if (!dashcam_storage_is_idx_sidecar_name(entry->d_name))
        {
            continue;
        }

        name_len = strlen(entry->d_name);
        if (name_len <= 4 || name_len >= DASHCAM_STORAGE_MAX_NAME)
        {
            continue;
        }

        snprintf(mp4_path, sizeof(mp4_path), "%s/%.*s",
                 DASHCAM_STORAGE_DIR,
                 (int)(name_len - 4),
                 entry->d_name);
        if (bk_vfs_stat(mp4_path, &st) == 0)
        {
            continue;
        }

        snprintf(idx_path, sizeof(idx_path), "%s/%s", DASHCAM_STORAGE_DIR, entry->d_name);
        ret = bk_vfs_unlink(idx_path);
        if (ret == 0)
        {
            n_removed++;
            LOGI("removed orphan idx: %s\n", idx_path);
        }
        else
        {
            n_failed++;
            LOGW("unlink orphan idx %s failed: %d\n", idx_path, ret);
        }
    }

    bk_vfs_closedir(dir);

    if (removed != NULL)
    {
        *removed = n_removed;
    }
    if (failed != NULL)
    {
        *failed = n_failed;
    }

    return BK_OK;
}
