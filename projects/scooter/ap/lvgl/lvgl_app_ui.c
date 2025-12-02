#include <os/os.h>
#include "media_evt.h"
#if (CONFIG_TP)
#include "driver/drv_tp.h"
#endif
#include "media_devices.h"
#include "frame_buffer.h"
#include "yuv_encode.h"
#include "lv_vendor.h"
#include "components/bk_display.h"
#include "lcd_panel_devices.h"
#include "driver/gpio.h"
#include "gpio_driver.h"
#include "driver/flash_partition.h"
#include "beken_ui.h"
#if CONFIG_AVI_PLAYER
#include "avi_player.h"
#include "bk_posix.h"
#endif

extern lv_vnd_config_t vendor_config;

#if CONFIG_AVI_PLAYER
static lv_obj_t *avi_img = NULL;
static lv_timer_t *avi_timer = NULL;
static bk_avi_player_t *handle = NULL;

static lv_image_dsc_t avi_img_dsc =
{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.stride = 0,
    .header.w = 0,
    .header.h = 0,
    .data_size = 0,
    .data = NULL,
};
#endif

#define LCD_BL_IO GPIO_37
#define LCD_LDO_IO GPIO_39

bk_display_rgb_ctlr_config_t rgb_config = {
#if (CONFIG_LCD_PANEL_USE_480X272)
    .lcd_device = &lcd_device_st7282,
#elif (CONFIG_LCD_PANEL_USE_800X480)
    .lcd_device = &lcd_device_h050iwv,
#else
    .lcd_device = &lcd_device_hx8282,
#endif
    .clk_pin = -1,
    .cs_pin = -1,
    .sda_pin = -1,
    .rst_pin = -1,
};

static avdk_err_t gpio_up(uint8_t io)
{
    gpio_dev_unmap(io);
    BK_LOG_ON_ERR(bk_gpio_enable_output(io));
    BK_LOG_ON_ERR(bk_gpio_pull_up(io));
    BK_LOG_ON_ERR(bk_gpio_set_output_high(io));
    return AVDK_ERR_OK;
}

static avdk_err_t lcd_backlight_open(uint8_t bl_io)
{
    return gpio_up(bl_io);
}

static avdk_err_t lcd_ldo_open(int8_t lcd_ldo_pin)
{
    return gpio_up(lcd_ldo_pin);
}

static avdk_err_t lcd_ldo_close(int8_t lcd_ldo_pin)
{
     bk_gpio_set_output_low(lcd_ldo_pin);
     return AVDK_ERR_OK;
}

static avdk_err_t lcd_backlight_close(uint8_t bl_io)
{
    bk_gpio_set_output_low(bl_io);
    return AVDK_ERR_OK;
}

static int _fs_mount(void)
{
    struct bk_fatfs_partition partition;
    char *fs_name = NULL;
    int ret;

    fs_name = "fatfs";
    partition.part_type = FATFS_DEVICE;

    partition.part_dev.device_name = FATFS_DEV_SDCARD;
    partition.mount_path = VFS_SD_0_PATITION_0;

    ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);

    return ret;
}

static bk_err_t bk_avi_player_vfs_init(void)
{
    bk_err_t ret = BK_FAIL;

    do {
        ret = _fs_mount();
        if (BK_OK != ret)
        {
            BK_LOGD(NULL, "[%s][%d] mount fail:%d\r\n", __FUNCTION__, __LINE__, ret);
            break;
        }

        BK_LOGD(NULL, "[%s][%d] mount success\r\n", __FUNCTION__, __LINE__);
    } while(0);

    return ret;
}

static bk_err_t bk_avi_player_vfs_deinit(void)
{
    bk_err_t ret = BK_FAIL;

    ret = umount(VFS_SD_0_PATITION_0);
    if (BK_OK != ret) {
        BK_LOGD(NULL, "[%s][%d] unmount fail:%d\r\n", __FUNCTION__, __LINE__, ret);
        return ret;
    }

    BK_LOGD(NULL, "[%s][%d] unmount success\r\n", __FUNCTION__, __LINE__);

    return ret;
}

#if CONFIG_AVI_PLAYER
static void lv_timer_cb(lv_timer_t *timer)
{
    bk_err_t ret = BK_OK;

    handle->pos++;
    if (handle->pos < handle->video_num) {
        ret = bk_avi_player_video_parse();
        if (ret != BK_OK) {
            os_printf("%s %d bk_avi_video_prase_to_rgb565 failed\r\n", __func__, __LINE__);
            return;
        }

        lv_img_set_src(avi_img, &avi_img_dsc);
    } else {
        lv_timer_del(avi_timer);
        avi_timer = NULL;
        lv_obj_del(avi_img);
        avi_img = NULL;
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        beken_ui_init();
    }
}
#endif

void lvgl_app_init(void)
{
    lv_vnd_config_t lv_vnd_config = {0};

    lv_vnd_config.width = rgb_config.lcd_device->width;
    lv_vnd_config.height = rgb_config.lcd_device->height;
    lv_vnd_config.render_mode = RENDER_DIRECT_MODE;

    lv_vnd_config.rotation = ROTATE_NONE;
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        lv_vnd_config.frame_buffer[i] = frame_buffer_display_malloc(lv_vnd_config.width * lv_vnd_config.height * sizeof(bk_color_t));
        if (lv_vnd_config.frame_buffer[i] == NULL) {
            os_printf("lv_frame_buffer[%d] malloc failed\r\n", i);
            return;
        }
    }
    bk_display_rgb_new(&lv_vnd_config.handle, &rgb_config);
    lv_vendor_init(&lv_vnd_config);

#if (CONFIG_TP)
    drv_tp_open(lv_vnd_config.width, lv_vnd_config.height, TP_MIRROR_NONE);
#endif

    lcd_ldo_open(LCD_LDO_IO);
    bk_display_open(lv_vnd_config.handle);

#if CONFIG_AVI_PLAYER
    bk_avi_player_config_t avi_player_config = {0};
    bk_err_t ret;

    ret = bk_avi_player_vfs_init();
    if (ret != BK_OK) {
        os_printf("%s %d bk_avi_player_vfs_init failed\r\n", __func__, __LINE__);
        goto avi_player_fail;
    }

#if (CONFIG_LCD_PANEL_USE_480X272)
    avi_player_config.file_path = PATH_SD_FILE("animation_480_272.avi");
#elif (CONFIG_LCD_PANEL_USE_800X480)    
    avi_player_config.file_path = PATH_SD_FILE("animation_800_480.avi");
#else
    avi_player_config.file_path = PATH_SD_FILE("animation_1024_600.avi");
#endif
    avi_player_config.output_format = AVI_PLAYER_OUTPUT_FORMAT_RGB565;
    avi_player_config.segment_flag = false;
    avi_player_config.rgb565_byte_swap_flag = false;
    ret = bk_avi_player_open(&avi_player_config);
    if (ret != BK_OK) {
        os_printf("%s %d bk_avi_player_open failed\r\n", __func__, __LINE__);
        goto avi_player_fail;
    }

    handle = bk_avi_player_get_handle();
    avi_img_dsc.header.w = handle->avi->width;
    avi_img_dsc.header.h = handle->avi->height;
    avi_img_dsc.header.stride = avi_img_dsc.header.w * 2;
    avi_img_dsc.data_size = handle->avi->width * handle->avi->height * 2;
    avi_img_dsc.data = (const uint8_t *)handle->framebuffer;

    ret = bk_avi_player_video_parse();
    if (ret != BK_OK) {
        os_printf("%s %d bk_avi_video_prase_to_rgb565 failed\r\n", __func__, __LINE__);
        goto avi_player_fail;
    }

    lv_vendor_disp_lock();
    avi_img = lv_img_create(lv_scr_act());
    lv_img_set_src(avi_img, &avi_img_dsc);
    lv_obj_align(avi_img, LV_ALIGN_CENTER, 0, 0);

    avi_timer = lv_timer_create(lv_timer_cb, 1000 / (uint32_t)handle->avi->fps, NULL);
    lv_vendor_disp_unlock();

    lcd_backlight_open(LCD_BL_IO);
    lv_vendor_start();
    return;

avi_player_fail:
    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();
#else // CONFIG_AVI_PLAYER
    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();
#endif

    lcd_backlight_open(LCD_BL_IO);

    lv_vendor_start();
}

bk_err_t lvgl_app_deinit(void)
{
    lcd_backlight_close(LCD_BL_IO);

    lv_vendor_stop();

#if (CONFIG_TP)
    drv_tp_close();
#endif

    bk_display_close(vendor_config.handle);
    bk_display_delete(vendor_config.handle);
    lcd_ldo_close(LCD_LDO_IO);

    lv_vendor_deinit();

    return BK_OK;
}

bk_err_t lvgl_app_suspend_display(void)
{
    lv_vendor_stop();

    return BK_OK;
}

bk_err_t lvgl_app_resume_display(void)
{
    lv_vendor_start();

    return BK_OK;
}

static bool navigation_is_opened = false;
static bool navigation_map_is_first_frame = true;

static lv_image_dsc_t navigation_map_265x195 = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 265,
    .header.h = 195,
    .data_size = 265 * 195 * 2,
    .data = NULL,
    .reserved = NULL,
};

void lvgl_app_enter_navigation(void)
{
    if (navigation_is_opened) {
        os_printf("%s %d navigation is already entered\r\n", __func__, __LINE__);
        return;
    }

    lv_vendor_disp_lock();
    init_page_page_2(&bk_lv_tool_ui);
    lv_vendor_disp_unlock();

    navigation_is_opened = true;
}

void lvgl_app_exit_navigation(void)
{
    if (!navigation_is_opened) {
        os_printf("%s %d navigation is already exited\r\n", __func__, __LINE__);
        return;
    }

    lv_vendor_disp_lock();
    lv_screen_load(bk_lv_tool_ui.page_1);
    destroy_page_page_2(&bk_lv_tool_ui);
    lv_vendor_disp_unlock();

    navigation_map_is_first_frame = true;
    navigation_is_opened = false;
}

void lvgl_app_display_navigation(uint8_t *data, uint32_t data_len)
{
    if (data == NULL || data_len == 0) {
        os_printf("%s %d data is NULL or data_len is 0\r\n", __func__, __LINE__);
        return;
    }

    if (navigation_map_265x195.data_size != data_len) {
        os_printf("%s %d data_len is not equal to navigation_map_265x195.data_size\r\n", __func__, __LINE__);
        return;
    }

    lv_vendor_disp_lock();
    navigation_map_265x195.data = data;

    if (navigation_map_is_first_frame) {
        navigation_map_is_first_frame = false;
        lv_image_set_src(bk_lv_tool_ui.page_2_image_8, &navigation_map_265x195);
        lv_screen_load(bk_lv_tool_ui.page_2);
    } else {
        lv_obj_invalidate(bk_lv_tool_ui.page_2_image_8);
    }
    lv_vendor_disp_unlock();
}