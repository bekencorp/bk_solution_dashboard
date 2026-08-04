// Copyright 2026 Beken
//
// SD card MTP control / CLI layer.
//
// Mirrors the MTP control block of avdk/projects/multimedia/usb_example/ap/ap_main.c,
// but packaged as a reusable solution component. The actual MTP protocol engine
// (usb_mtp_init / usb_mtp_deinit) is provided by the SDK bk_usb component under
// CONFIG_USBD_MTP; here we only own the runtime start/stop lifecycle and the
// `mtp` shell command.

#include <common/bk_include.h>
#include <os/str.h>
#include <os/mem.h>
#include <components/log.h>
#include "cli.h"
#include "sdcard_mtp.h"

#define TAG "sdcard_mtp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#if CONFIG_USBD_MTP

#include "bk_usb_mtp.h"

static volatile int s_mtp_active;

/* ============================================================
 * On-board SD card browsing (`mtp ls`)
 *
 * Lists the same storage MTP exposes to the PC: the SD card FATFS mounted at
 * /sd0 via the bk_vfs POSIX layer. Works whether or not `mtp start` has run
 * (mounts /sd0 on demand). Uses opendir/readdir so it never pulls ff.h (whose
 * DIR type clashes with the bk_vfs one).
 * ============================================================ */
#if CONFIG_VFS
#include "bk_posix.h"
#include "bk_partition.h"
#include "bk_filesystem.h"

#define SDCARD_MTP_LS_MAX_DEPTH 8
#define SDCARD_MTP_LS_PATH_MAX  256

static void sdcard_mtp_mount_sd(void)
{
    struct bk_fatfs_partition partition;

    partition.part_type = FATFS_DEVICE;
    partition.part_dev.device_name = FATFS_DEV_SDCARD;
    partition.mount_path = VFS_SD_0_PATITION_0;
    /* Idempotent: if MTP (or a previous ls) already mounted /sd0, the second
     * mount just returns an error we deliberately ignore. */
    (void)mount("SOURCE_NONE", partition.mount_path, "fatfs", 0, &partition);
}

static void sdcard_mtp_scan_dir(char *path, int depth, int *count)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        LOGE("opendir(%s) failed\n", path);
        return;
    }

    size_t base_len = os_strlen(path);
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == 0)
        {
            break;
        }
        if (os_strcmp(ent->d_name, ".") == 0 || os_strcmp(ent->d_name, "..") == 0)
        {
            continue;
        }

        (*count)++;
        if (ent->d_type == DT_DIR)
        {
            LOGI("%*s<DIR>  %s/\n", depth * 2, "", ent->d_name);
            if (depth + 1 < SDCARD_MTP_LS_MAX_DEPTH)
            {
                const char *sep = (base_len && path[base_len - 1] == '/') ? "" : "/";
                int n = os_snprintf(path + base_len, SDCARD_MTP_LS_PATH_MAX - base_len,
                                    "%s%s", sep, ent->d_name);
                if (n > 0 && (base_len + (size_t)n) < SDCARD_MTP_LS_PATH_MAX)
                {
                    sdcard_mtp_scan_dir(path, depth + 1, count);
                }
                path[base_len] = '\0';
            }
        }
        else
        {
            LOGI("%*s<FIL>  %s  %ld B\n", depth * 2, "", ent->d_name, ent->d_size);
        }
    }

    closedir(dir);
}

int sdcard_mtp_ls(const char *path)
{
    const char *root = (path && path[0]) ? path : VFS_SD_0_PATITION_0;
    char *buf;
    int count = 0;

    sdcard_mtp_mount_sd();

    buf = (char *)os_malloc(SDCARD_MTP_LS_PATH_MAX);
    if (buf == NULL)
    {
        LOGE("ls: out of memory\n");
        return -1;
    }
    os_strncpy(buf, root, SDCARD_MTP_LS_PATH_MAX - 1);
    buf[SDCARD_MTP_LS_PATH_MAX - 1] = '\0';

    LOGI("---- SD card file list (%s) ----\n", buf);
    sdcard_mtp_scan_dir(buf, 0, &count);
    LOGI("---- %d entries total ----\n", count);

    os_free(buf);
    return 0;
}
#else  /* !CONFIG_VFS */
int sdcard_mtp_ls(const char *path)
{
    (void)path;
    LOGW("CONFIG_VFS not set; sdcard ls unavailable\n");
    return -1;
}
#endif /* CONFIG_VFS */

int sdcard_mtp_start(void)
{
    if (s_mtp_active)
    {
        LOGI("MTP already active\n");
        return 0;
    }

    /* usb_mtp_init() mounts the SD card at /sd0 (bk_vfs) and brings up the USB
     * device gadget on busid 0 itself. This project runs no other USB gadget at
     * boot, so MTP can own the controller directly. */
    int ret = usb_mtp_init();
    LOGI("usb_mtp_init ret=%d\n", ret);
    s_mtp_active = (ret == 0) ? 1 : 0;
    if (s_mtp_active)
    {
        LOGI("==== MTP device active (browse the SD card on the PC) ====\n");
    }
    return ret;
}

int sdcard_mtp_stop(void)
{
    if (!s_mtp_active)
    {
        LOGI("MTP not active\n");
        return 0;
    }

    int ret = usb_mtp_deinit();
    LOGI("usb_mtp_deinit ret=%d\n", ret);
    s_mtp_active = 0;
    return ret;
}

bool sdcard_mtp_is_active(void)
{
    return s_mtp_active ? true : false;
}

static void cli_mtp_help(void)
{
    LOGI("usage:\n");
    LOGI("  mtp start     - mount SD + bring up the MTP gadget\n");
    LOGI("  mtp stop      - tear down the MTP gadget\n");
    LOGI("  mtp status    - print current MTP state\n");
    LOGI("  mtp ls [path] - list SD card contents (/sd0 by default)\n");
}

static void cli_mtp_cmd(char *pcWriteBuffer, int xWriteBufferLen,
                        int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2)
    {
        cli_mtp_help();
        LOGI("mtp active=%d\n", s_mtp_active);
        return;
    }

    const char *sub = argv[1];
    if (os_strcmp(sub, "start") == 0)
    {
        (void)sdcard_mtp_start();
    }
    else if (os_strcmp(sub, "stop") == 0)
    {
        (void)sdcard_mtp_stop();
    }
    else if (os_strcmp(sub, "status") == 0)
    {
        LOGI("mtp active=%d\n", s_mtp_active);
    }
    else if (os_strcmp(sub, "ls") == 0)
    {
        (void)sdcard_mtp_ls(argc >= 3 ? argv[2] : NULL);
    }
    else
    {
        cli_mtp_help();
    }
}

static const struct cli_command s_mtp_cmds[] =
{
    {"mtp", "mtp start|stop|status|ls", cli_mtp_cmd},
};

bk_err_t sdcard_mtp_init(void)
{
    bk_err_t ret = cli_register_commands(s_mtp_cmds,
                                         sizeof(s_mtp_cmds) / sizeof(s_mtp_cmds[0]));
    if (ret != BK_OK)
    {
        LOGE("register mtp cli failed ret=%d\n", ret);
    }
    else
    {
        LOGI("mtp CLI registered (try: mtp start)\n");
    }
    return ret;
}

#else /* !CONFIG_USBD_MTP */

/* MTP not compiled in: provide stubs so a project can link unconditionally. */

bk_err_t sdcard_mtp_init(void)
{
    LOGW("CONFIG_USBD_MTP is not set; mtp CLI unavailable\n");
    return BK_OK;
}

int sdcard_mtp_start(void)
{
    return -1;
}

int sdcard_mtp_stop(void)
{
    return -1;
}

bool sdcard_mtp_is_active(void)
{
    return false;
}

int sdcard_mtp_ls(const char *path)
{
    (void)path;
    LOGW("CONFIG_USBD_MTP is not set; sdcard ls unavailable\n");
    return -1;
}

#endif /* CONFIG_USBD_MTP */
