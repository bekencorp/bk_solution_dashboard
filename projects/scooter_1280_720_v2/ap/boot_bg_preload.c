#include "boot_bg_preload.h"

#include <os/os.h>
#include <os/mem.h>
#include <string.h>
#include <stdio.h>
#include <components/log.h>

#include "ff.h"
#include "diskio.h"
#if CONFIG_VFS
#include "bk_partition.h"
#endif

/*
 * LVGL's software JPEG decoder (TinyJPEG / TJPGD). We call its low-level API
 * directly instead of going through lv_image_decoder, because the preloader
 * runs during the boot animation - before display_ui_start_lvgl()/lv_init(),
 * so the high-level LVGL decoder pipeline is not registered yet.
 */
#include "src/libs/tjpgd/tjpgd.h"

#define TAG "boot_bg"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BG_W        1280
#define BG_H        720
#define BG_SIZE     ((uint32_t)BG_W * BG_H * 2u) /* RGB565 */
#define BG_PATH     "1:/home_bg.jpg"

/* TJPGD working pool (huffman/quant tables + MCU + input buffer). 4KB is the
 * size LVGL's own lv_tjpgd decoder uses and is enough for a 1280-wide image. */
#define BG_TJPGD_POOL_SIZE  4096u

volatile bool g_boot_sd_pre_mounted = false;

#if !CONFIG_VFS
static FATFS *s_fs = NULL;
#endif
static uint8_t *s_buf = NULL;
static lv_image_dsc_t s_dsc;
static volatile bool s_ready = false;
static beken_semaphore_t s_done_sem = NULL;
static beken_thread_t s_thread = NULL;

static bk_err_t preload_mount(void)
{
#if CONFIG_VFS
    struct bk_fatfs_partition partition;
    int ret;
#else
    char drive[8];
    FRESULT fr;
#endif

    if (g_boot_sd_pre_mounted)
    {
        return BK_OK;
    }

#if CONFIG_VFS
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

    g_boot_sd_pre_mounted = true;
    LOGI("SD card mounted (preloader)\n");
    return BK_OK;
}

/* TJPGD stream input: pull JPEG bytes from the open FATFS file. */
static size_t preload_jpg_input(JDEC *jd, uint8_t *buff, size_t ndata)
{
    FIL *fp = (FIL *)jd->device;

    if (buff != NULL)
    {
        UINT br = 0;
        if (f_read(fp, buff, (UINT)ndata, &br) != FR_OK)
        {
            return 0;
        }
        return (size_t)br;
    }

    /* buff == NULL: skip ndata bytes forward in the stream. */
    if (f_lseek(fp, f_tell(fp) + ndata) != FR_OK)
    {
        return 0;
    }
    return ndata;
}

/*
 * TJPGD output: receive a decoded RGB888 (LVGL byte order B,G,R) MCU tile and
 * pack it into the PSRAM RGB565 buffer. jd_mcu_output() already clips the rect
 * to the image bounds, so writes stay inside s_buf (validated to be BG_WxBG_H).
 */
static int preload_jpg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint8_t *src = (const uint8_t *)bitmap;
    uint16_t *dst_base = (uint16_t *)s_buf;
    uint32_t w = (uint32_t)rect->right - rect->left + 1u;
    uint32_t h = (uint32_t)rect->bottom - rect->top + 1u;
    uint32_t y;

    (void)jd;

    for (y = 0; y < h; y++)
    {
        uint16_t *dst = dst_base + ((uint32_t)rect->top + y) * BG_W + rect->left;
        uint32_t x;

        for (x = 0; x < w; x++)
        {
            uint8_t b = *src++;
            uint8_t g = *src++;
            uint8_t r = *src++;

            *dst++ = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
        }
    }

    return 1; /* continue decoding */
}

static void preload_thread(void *arg)
{
    FIL f;
    FRESULT fr;
    void *pool = NULL;
    JDEC jd;
    JRESULT jr;
    bool opened = false;
    uint32_t t_begin = rtos_get_time();

    (void)arg;

    LOGI("[overlap] worker decode BEGIN  @%u ms\n", (unsigned)t_begin);

    fr = f_open(&f, BG_PATH, FA_READ);
    if (fr != FR_OK)
    {
        LOGW("open %s failed: %d (will fall back to JPEG)\n", BG_PATH, fr);
        goto done;
    }
    opened = true;

    pool = os_malloc(BG_TJPGD_POOL_SIZE);
    if (pool == NULL)
    {
        LOGE("tjpgd pool alloc(%u) failed\n", (unsigned)BG_TJPGD_POOL_SIZE);
        goto done;
    }

    jr = jd_prepare(&jd, preload_jpg_input, pool, BG_TJPGD_POOL_SIZE, &f);
    if (jr != JDR_OK)
    {
        LOGW("jd_prepare failed: %d (will fall back to JPEG)\n", jr);
        goto done;
    }

    if (jd.width != BG_W || jd.height != BG_H)
    {
        LOGW("unexpected JPEG size %ux%u (want %ux%u), fall back\n",
             (unsigned)jd.width, (unsigned)jd.height, (unsigned)BG_W, (unsigned)BG_H);
        goto done;
    }

    jr = jd_decomp(&jd, preload_jpg_output, 0); /* scale 0 = full resolution */
    if (jr != JDR_OK)
    {
        LOGW("jd_decomp failed: %d (will fall back to JPEG)\n", jr);
        goto done;
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w = BG_W;
    s_dsc.header.h = BG_H;
    s_dsc.header.stride = BG_W * 2;
    s_dsc.data = s_buf;
    s_dsc.data_size = BG_SIZE;
    s_ready = true;
    LOGI("[overlap] worker decode END    @%u ms (took %u ms) %s -> PSRAM RGB565 %ux%u\n",
         (unsigned)rtos_get_time(), (unsigned)(rtos_get_time() - t_begin),
         BG_PATH, (unsigned)BG_W, (unsigned)BG_H);

done:
    if (pool != NULL)
    {
        os_free(pool);
    }
    if (opened)
    {
        f_close(&f);
    }
    /* On failure free the PSRAM bitmap so the canvas fallback does not end up
     * holding a second ~1.8MB buffer. */
    if (!s_ready && s_buf != NULL)
    {
        psram_free(s_buf);
        s_buf = NULL;
    }
    if (s_done_sem)
    {
        rtos_set_semaphore(&s_done_sem);
    }
    s_thread = NULL;
    rtos_delete_thread(NULL);
}

void boot_bg_preload_start(void)
{
    if (preload_mount() != BK_OK)
    {
        return;
    }

    s_buf = psram_malloc(BG_SIZE);
    if (s_buf == NULL)
    {
        LOGE("psram_malloc(%u) failed\n", (unsigned)BG_SIZE);
        return;
    }

    if (rtos_init_semaphore_ex(&s_done_sem, 1, 0) != BK_OK)
    {
        LOGE("done sem init failed\n");
        s_done_sem = NULL;
    }

    if (rtos_create_thread(&s_thread, 4, "bg_preload",
                           (beken_thread_function_t)preload_thread,
                           8192, NULL) != BK_OK)
    {
        LOGE("preload thread create failed\n");
        s_thread = NULL;
    }
}

const lv_image_dsc_t *boot_bg_preload_get(uint32_t timeout_ms)
{
    if (!s_ready && s_done_sem)
    {
        rtos_get_semaphore(&s_done_sem, timeout_ms);
    }

    return s_ready ? &s_dsc : NULL;
}

void boot_bg_preload_finish(void)
{
    /*
     * Intentionally does NOT deinit s_done_sem: the worker thread may still be
     * running (if the decode outran boot_bg_preload_get()'s timeout, or simply
     * has not reached its rtos_set_semaphore() yet) and would then signal a
     * freed semaphore. The semaphore is a single one-shot boot resource, so it
     * is left allocated for the rest of runtime rather than risk a
     * use-after-free. The PSRAM bitmap and SD mount are likewise kept alive.
     */
}
