#include "beken_ui.h"
#include <os/os.h>
#include <components/log.h>
#include <stdbool.h>

#define TAG "pet_page"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#if LV_USE_GIF

static bool s_on_pet_page = false;

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

    int idx = lv_rand(1, 2);
    char path[32];
    lv_snprintf(path, sizeof(path), "S:1:/pet_%d.gif", idx);
    LOGI("loading %s\n", path);

    lv_fs_file_t test_f;
    lv_fs_res_t res = lv_fs_open(&test_f, path, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        LOGW("lv_fs_open failed: res=%d path=%s\n", res, path);
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
