#include "dashcam_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bk_std_header.h"
#include "bk_vfs.h"
#include "bk_filesystem.h"
#include "bk_partition.h"
#include "components/log.h"
#include "dashcam_config.h"
#include "os/os.h"
#include "boot_sd_mount.h"

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

static bool s_storage_ready = false;
static bool s_ftp_started = false;
static uint32_t s_record_index = 0;

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

bool dashcam_storage_mp4_is_playable(const char *path)
{
    static const uint32_t box_ftyp = 0x66747970U;
    static const uint32_t box_mdat = 0x6D646174U;
    static const uint32_t box_moov = 0x6D6F6F76U;
    uint8_t header[16];
    bool has_mdat = false;
    bool has_moov = false;
    int fd;
    off_t file_size;
    uint64_t offset = 0U;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    fd = bk_vfs_open(path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    file_size = bk_vfs_lseek(fd, 0, SEEK_END);
    if (file_size < 8 || bk_vfs_lseek(fd, 0, SEEK_SET) < 0)
    {
        bk_vfs_close(fd);
        return false;
    }

    while (offset + 8U <= (uint64_t)file_size)
    {
        uint32_t size32;
        uint32_t type;
        uint32_t header_size = 8U;
        uint64_t box_size;

        if (bk_vfs_read(fd, header, 8U) != 8)
        {
            break;
        }

        size32 = dashcam_storage_be32(header);
        type = dashcam_storage_be32(header + 4);
        box_size = size32;
        if (size32 == 1U)
        {
            if (bk_vfs_read(fd, header + 8, 8U) != 8)
            {
                break;
            }
            box_size = dashcam_storage_be64(header + 8);
            header_size = 16U;
        }

        if ((offset == 0U && type != box_ftyp) ||
            box_size < header_size ||
            box_size > (uint64_t)file_size - offset)
        {
            break;
        }

        has_mdat = has_mdat || type == box_mdat;
        has_moov = has_moov || type == box_moov;
        offset += box_size;
        if (has_mdat && has_moov)
        {
            break;
        }
        if (bk_vfs_lseek(fd, (off_t)offset, SEEK_SET) < 0)
        {
            break;
        }
    }

    bk_vfs_close(fd);
    return has_mdat && has_moov;
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

#if DASHCAM_USE_WALL_CLOCK
        {
            unsigned int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
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
        }
#else
        {
            unsigned long uptime = 0;
            unsigned long seq = 0;
            if (sscanf(entry->d_name, "dashcam_%lu_%lu.mp4", &uptime, &seq) == 2)
            {
                item.uptime_ms = (uint32_t)uptime;
                item.seq = (uint32_t)seq;
            }
        }
#endif

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

bk_err_t dashcam_storage_delete_oldest(void)
{
    DIR *dir;
    struct dirent *entry;
    char oldest[DASHCAM_STORAGE_MAX_NAME] = {0};
    char path[DASHCAM_STORAGE_MAX_PATH];
    bool found = false;

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

    snprintf(path, sizeof(path), "%s/%s", DASHCAM_STORAGE_DIR, oldest);
    if (bk_vfs_unlink(path) != 0)
    {
        LOGE("unlink %s failed\n", path);
        return BK_FAIL;
    }

    LOGI("recycled oldest clip: %s\n", path);
    return BK_OK;
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

bk_err_t dashcam_storage_recycle_for_release(void)
{
    uint32_t count = 0;
    uint32_t guard;

    if (dashcam_storage_count(&count) != BK_OK)
    {
        return BK_FAIL;
    }

    /*
     * Release ring (req5 §11): no fixed file-count cap. Delete the oldest clips
     * one at a time until the card's free space climbs back above the floor.
     * Free MB is re-queried each pass so deletions are reflected; if statfs is
     * unsupported we cannot judge space, so stop. The guard bounds deletions to
     * the snapshot count so a card that never recovers space cannot loop.
     */
    for (guard = count; guard > 0; guard--)
    {
        uint32_t free_mb = 0;

        if (dashcam_storage_free_mb(&free_mb) != BK_OK)
        {
            break;
        }
        if (free_mb >= DASHCAM_RECYCLE_MIN_FREE_MB)
        {
            break;
        }
        if (dashcam_storage_delete_oldest() != BK_OK)
        {
            break;
        }
    }

    return BK_OK;
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

#if DASHCAM_USE_WALL_CLOCK
    /* Plan A: show the real record date/time parsed from the filename. */
    if (info->year >= 2020)
    {
        snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)info->year, (unsigned)info->month, (unsigned)info->day,
                 (unsigned)info->hour, (unsigned)info->minute, (unsigned)info->second);
        return;
    }
#endif

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
