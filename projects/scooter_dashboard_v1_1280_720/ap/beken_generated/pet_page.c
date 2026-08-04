#include "beken_ui.h"
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <stdbool.h>

#if CONFIG_FATFS
#include "ff.h"
#include "diskio.h"
#endif

#define TAG "pet_page"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#if LV_USE_GIF

#define PET_GIF_COUNT 2

static bool s_on_pet_page = false;

#if CONFIG_FATFS
static FATFS *s_pet_fs = NULL;

static bool pet_sdcard_ensure_mounted(void)
{
    FILINFO fno;
    char drive[8];
    char root[8];
    FRESULT fr;

    lv_snprintf(drive, sizeof(drive), "%d:", DISK_NUMBER_SDIO_SD);
    lv_snprintf(root, sizeof(root), "%s/", drive);

    fr = f_stat(root, &fno);
    if (fr == FR_OK)
        return true;

    if (!s_pet_fs) {
        s_pet_fs = os_malloc(sizeof(FATFS));
        if (!s_pet_fs) {
            LOGW("FATFS alloc failed\n");
            return false;
        }
    }

    fr = f_mount(s_pet_fs, drive, 1);
    if (fr != FR_OK) {
        LOGW("f_mount failed: fr=%d drive=%s\n", fr, drive);
        return false;
    }

    LOGI("SD card mounted\n");
    return true;
}

static bool pet_select_gif_path(char *lv_path, size_t lv_path_size,
                                char *fat_path, size_t fat_path_size)
{
    FILINFO fno;
    int start = lv_rand(1, PET_GIF_COUNT);

    for (int i = 0; i < PET_GIF_COUNT; i++) {
        int idx = ((start - 1 + i) % PET_GIF_COUNT) + 1;
        FRESULT fr;

        lv_snprintf(fat_path, fat_path_size, "%d:/pet_%d.gif",
                    DISK_NUMBER_SDIO_SD, idx);
        fr = f_stat(fat_path, &fno);
        if (fr == FR_OK && !(fno.fattrib & AM_DIR)) {
            lv_snprintf(lv_path, lv_path_size, "%c:%s",
                        LV_FS_FATFS_LETTER, fat_path);
            return true;
        }

        LOGW("pet GIF not available: fr=%d path=%s\n", fr, fat_path);
    }

    return false;
}
#endif

void init_page_pet(bk_lv_ui_t *ui)
{
    if (ui->page_pet && lv_obj_is_valid(ui->page_pet))
        destroy_page_pet(ui);

    ui->page_pet = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->page_pet);
    lv_obj_set_style_bg_opa(ui->page_pet, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui->page_pet, lv_color_white(), 0);

    ui->page_pet_gif = lv_gif_create(ui->page_pet);
    lv_obj_set_size(ui->page_pet_gif, 480, 270);
    lv_obj_center(ui->page_pet_gif);
    lv_obj_set_style_bg_opa(ui->page_pet_gif, LV_OPA_TRANSP, 0);

#if CONFIG_FATFS
    char path[32];
    char fat_path[32];

    if (!pet_sdcard_ensure_mounted()) {
        LOGW("SD card not available, skip pet GIF\n");
        return;
    }

    if (!pet_select_gif_path(path, sizeof(path), fat_path, sizeof(fat_path))) {
        LOGW("no pet GIF found, expected %d:/pet_1.gif or %d:/pet_2.gif\n",
             DISK_NUMBER_SDIO_SD, DISK_NUMBER_SDIO_SD);
        return;
    }

    LOGI("loading %s\n", path);

    lv_fs_file_t test_f;
    lv_fs_res_t res = lv_fs_open(&test_f, path, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        LOGW("lv_fs_open failed: res=%d path=%s\n", res, path);
        return;
    } else {
        uint8_t hdr[10];
        uint32_t br = 0;
        lv_fs_read(&test_f, hdr, sizeof(hdr), &br);
        if (br >= 10) {
            uint16_t w = hdr[6] | ((uint16_t)hdr[7] << 8);
            uint16_t h = hdr[8] | ((uint16_t)hdr[9] << 8);
            LOGI("GIF header: %.3s%.3s  %ux%u  need %u bytes\n",
                 hdr, hdr + 3, w, h, (unsigned)(5u * w * h));
        } else {
            LOGW("read only %u bytes\n", (unsigned)br);
        }
        lv_fs_close(&test_f);
    }

    lv_gif_set_src(ui->page_pet_gif, path);

    if (!lv_gif_is_loaded(ui->page_pet_gif)) {
        LOGW("GIF load failed: %s\n", path);
    } else {
        const lv_image_dsc_t *dsc = lv_image_get_src(ui->page_pet_gif);
        if (dsc && dsc->data) {
            uint8_t *canvas = (uint8_t *)dsc->data;
            uint32_t total = dsc->data_size / 4;
            for (uint32_t i = 0; i < total; i++) {
                canvas[i * 4 + 0] = 0xFF;
                canvas[i * 4 + 1] = 0xFF;
                canvas[i * 4 + 2] = 0xFF;
                canvas[i * 4 + 3] = 0xFF;
            }
            lv_gif_restart(ui->page_pet_gif);
        }
    }
#else
    LOGW("FatFS support disabled, skip pet GIF\n");
#endif
}

void destroy_page_pet(bk_lv_ui_t *ui)
{
    if (ui->page_pet && lv_obj_is_valid(ui->page_pet)) {
        lv_obj_del(ui->page_pet);
        ui->page_pet = NULL;
        ui->page_pet_gif = NULL;
    }
}

static void pet_toggle_async_cb(void *user_data)
{
    (void)user_data;
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (!s_on_pet_page) {
        init_page_pet(ui);
        lv_screen_load(ui->page_pet);
        s_on_pet_page = true;
        LOGI("switched to pet page\n");
    } else {
        lv_screen_load(ui->Page_1);
        destroy_page_pet(ui);
        s_on_pet_page = false;
        LOGI("switched to dashboard\n");
    }
}

void pet_page_toggle(void)
{
    lv_async_call(pet_toggle_async_cb, NULL);
}

#else

void init_page_pet(bk_lv_ui_t *ui)   { (void)ui; }
void destroy_page_pet(bk_lv_ui_t *ui) { (void)ui; }
void pet_page_toggle(void)            { LOGW("GIF support disabled\n"); }

#endif
