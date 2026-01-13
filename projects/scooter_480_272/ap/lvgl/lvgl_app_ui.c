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
#include "driver/dma2d.h"
#include "driver/flash_partition.h"
#include "beken_ui.h"
#if CONFIG_AVI_PLAYER
#include "avi_player.h"
#include "bk_posix.h"
#endif

#define TAG "lvgl_app"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

extern lv_vnd_config_t vendor_config;

#if CONFIG_AVI_PLAYER
static bk_avi_player_t *handle = NULL;

#if CONFIG_AVI_PLAYER_WITHOUT_LVGL
static beken_thread_t g_avi_player_thread = NULL;
static beken_semaphore_t g_avi_player_sem = NULL;
static frame_buffer_t *lcd_frame_buffer = NULL;
static bool g_avi_player_is_running = false;
static bk_display_ctlr_handle_t lcd_display_handle = NULL;

#else // CONFIG_AVI_PLAYER_WITHOUT_LVGL
static lv_obj_t *avi_img = NULL;
static lv_timer_t *avi_timer = NULL;

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
#endif // CONFIG_AVI_PLAYER_WITHOUT_LVGL
#endif // CONFIG_AVI_PLAYER

#define LCD_BL_IO GPIO_37
#define LCD_LDO_IO GPIO_39

bk_display_rgb_ctlr_config_t rgb_config = {
    .lcd_device = &lcd_device_st7282,
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
            LOGE("[%s][%d] mount fail:%d\r\n", __FUNCTION__, __LINE__, ret);
            break;
        }

        LOGI("[%s][%d] mount success\r\n", __FUNCTION__, __LINE__);
    } while(0);

    return ret;
}

static bk_err_t bk_avi_player_vfs_deinit(void)
{
    bk_err_t ret = BK_FAIL;

    ret = umount(VFS_SD_0_PATITION_0);
    if (BK_OK != ret) {
        LOGE("[%s][%d] unmount fail:%d\r\n", __FUNCTION__, __LINE__, ret);
        return ret;
    }

    LOGI("[%s][%d] unmount success\r\n", __FUNCTION__, __LINE__);

    return ret;
}

#if CONFIG_AVI_PLAYER
#if CONFIG_AVI_PLAYER_WITHOUT_LVGL
static avdk_err_t display_frame_free_cb(void *frame)
{
    if (g_avi_player_is_running == false) {
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        rtos_deinit_semaphore(&g_avi_player_sem);
        g_avi_player_sem = NULL;
        os_free(lcd_frame_buffer);
        lcd_frame_buffer = NULL;
    }

    return AVDK_ERR_OK;
}

static void avi_player_thread(beken_thread_arg_t data)
{
    bk_err_t ret;
    uint32_t delay_time = 0;
    uint32_t start_time, end_time;

    g_avi_player_is_running = true;
    rtos_set_semaphore(&g_avi_player_sem);

    handle->pos = 0;
    delay_time = 1000 / (uint32_t)handle->avi->fps;

    lcd_backlight_open(LCD_BL_IO);

    while (g_avi_player_is_running)
    {
        if (handle->pos == handle->video_num) {
            lv_vendor_disp_lock();
            lv_screen_load(bk_lv_tool_ui.page_1);
            lv_vendor_disp_unlock();
            g_avi_player_is_running = false;

            break;
        }

        start_time = rtos_get_time();
        ret = bk_avi_player_video_parse();
        if (ret != BK_OK) {
            LOGE("%s %d bk_avi_player_video_parse failed\r\n", __func__, __LINE__);
            handle->pos++;
            continue;
        }
        handle->pos++;

        lcd_frame_buffer->frame = handle->framebuffer;
        bk_display_flush(lcd_display_handle, lcd_frame_buffer, display_frame_free_cb);

        end_time = rtos_get_time();
        LOGV("bk_avi_player_video_parse time: %d ms\n", end_time - start_time);

        if (end_time - start_time > delay_time) {
            LOGV("bk_avi_player_video_parse time is too long, just delay 2ms, time: %d ms\n",  end_time - start_time);
            rtos_delay_milliseconds(2);
        } else {
            rtos_delay_milliseconds(delay_time - (end_time - start_time));
        }
    }

    g_avi_player_thread = NULL;
    rtos_delete_thread(NULL);
}

bk_err_t bk_avi_player_start(char *file_path)
{
    bk_err_t ret = BK_OK;
    bk_avi_player_config_t avi_player_config = {0};

    if (g_avi_player_is_running) {
        LOGE("%s %d avi_player is already running\r\n", __func__, __LINE__);
        return BK_OK;
    }

    ret = bk_avi_player_vfs_init();
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_vfs_init failed\r\n", __func__, __LINE__);
        return ret;
    }

    avi_player_config.file_path = file_path;
    avi_player_config.output_format = AVI_PLAYER_OUTPUT_FORMAT_YUYV;
    avi_player_config.segment_flag = false;
    avi_player_config.rgb565_byte_swap_flag = false;
    ret = bk_avi_player_open(&avi_player_config);
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_open failed\r\n", __func__, __LINE__);
        goto avi_player_start_fail;
    }

    handle = bk_avi_player_get_handle();
    if (handle == NULL) {
        LOGE("%s %d bk_avi_player_get_handle failed\r\n", __func__, __LINE__);
        bk_avi_player_vfs_deinit();
        return BK_FAIL;
    }

    ret = rtos_init_semaphore_ex(&g_avi_player_sem, 1, 0);
    if (ret != BK_OK) {
        LOGE("%s g_avi_player_sem init failed\n", __func__);
        bk_avi_player_close();
        goto avi_player_start_fail;
    }

    lcd_frame_buffer = os_malloc(sizeof(frame_buffer_t));
    if (lcd_frame_buffer == NULL) {
        LOGE("%s %d lcd_frame_buffer malloc failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        goto avi_player_start_fail;
    }
    lcd_frame_buffer->size = handle->frame_size;
    lcd_frame_buffer->width = handle->avi->width;
    lcd_frame_buffer->height = handle->avi->height;
    lcd_frame_buffer->fmt = PIXEL_FMT_YUYV;

    ret = rtos_create_thread(&g_avi_player_thread,
                            BEKEN_DEFAULT_WORKER_PRIORITY - 1,
                            "avi_player_thread",
                            (beken_thread_function_t)avi_player_thread,
                            1024 * 4,
                            NULL);
    if (ret != BK_OK) {
        LOGE("%s %d rtos_create_thread failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        goto avi_player_start_fail;
    }

    rtos_get_semaphore(&g_avi_player_sem, BEKEN_NEVER_TIMEOUT);

    LOGI("%s complete\n", __func__);

    return ret;

avi_player_start_fail:

    if (lcd_frame_buffer) {
        os_free(lcd_frame_buffer);
        lcd_frame_buffer = NULL;
    }

    if (g_avi_player_sem) {
        rtos_deinit_semaphore(&g_avi_player_sem);
        g_avi_player_sem = NULL;
    }

    bk_avi_player_close();

    bk_avi_player_vfs_deinit();

    return ret;
}
#else
static void lv_timer_cb(lv_timer_t *timer)
{
    bk_err_t ret = BK_OK;

    handle->pos++;
    if (handle->pos < handle->video_num) {
        ret = bk_avi_player_video_parse();
        if (ret != BK_OK) {
            LOGE("%s %d bk_avi_player_video_parse failed\r\n", __func__, __LINE__);
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
#endif

void lvgl_app_init(void)
{
    bk_err_t ret = BK_OK;
    lv_vnd_config_t lv_vnd_config = {0};

    lv_vnd_config.width = rgb_config.lcd_device->width;
    lv_vnd_config.height = rgb_config.lcd_device->height;
    lv_vnd_config.render_mode = RENDER_DIRECT_MODE;

    lv_vnd_config.rotation = ROTATE_NONE;
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        lv_vnd_config.frame_buffer[i] = frame_buffer_display_malloc(lv_vnd_config.width * lv_vnd_config.height * sizeof(bk_color_t));
        if (lv_vnd_config.frame_buffer[i] == NULL) {
            LOGE("lv_frame_buffer[%d] malloc failed\r\n", i);
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
#if CONFIG_AVI_PLAYER_WITHOUT_LVGL
    lcd_display_handle = lv_vnd_config.handle;

    lv_vendor_disp_lock();
    init_page_page_1(&bk_lv_tool_ui);
    lv_vendor_disp_unlock();

    ret = bk_avi_player_start(PATH_SD_FILE("animation_480_272.avi"));
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_start failed\r\n", __func__, __LINE__);
        lv_vendor_disp_lock();
        lv_screen_load(bk_lv_tool_ui.page_1);
        lv_vendor_disp_unlock();

        lcd_backlight_open(LCD_BL_IO);
    }

    lv_vendor_start();
    return;
#else
    bk_avi_player_config_t avi_player_config = {0};

    ret = bk_avi_player_vfs_init();
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_vfs_init failed\r\n", __func__, __LINE__);
        goto avi_player_fail;
    }

    avi_player_config.file_path = PATH_SD_FILE("animation_480_272.avi");
    avi_player_config.output_format = AVI_PLAYER_OUTPUT_FORMAT_RGB565;
    avi_player_config.segment_flag = false;
    avi_player_config.rgb565_byte_swap_flag = false;
    ret = bk_avi_player_open(&avi_player_config);
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_open failed\r\n", __func__, __LINE__);
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
        LOGE("%s %d bk_avi_video_prase_to_rgb565 failed\r\n", __func__, __LINE__);
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
#endif // CONFIG_AVI_PLAYER_WITHOUT_LVGL
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


#define NAVIGATION_MAP_WIDTH         265
#define NAVIGATION_MAP_HEIGHT        195

static bool navigation_is_opened = false;
static bool navigation_map_is_first_frame = true;
static beken_semaphore_t navigation_map_dma2d_sem = NULL;
static uint8_t *navigation_map_data_rgb565 = NULL;
static bool navigation_map_dma2d_is_initialized = false;

static lv_image_dsc_t navigation_map = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = NAVIGATION_MAP_WIDTH,
    .header.h = NAVIGATION_MAP_HEIGHT,
    .data_size = NAVIGATION_MAP_WIDTH * NAVIGATION_MAP_HEIGHT * sizeof(bk_color_t),
    .data = NULL,
    .reserved = NULL,
};

static void navigation_map_dma2d_config_error(void *arg)
{
    LOGD("%s \n", __func__);
}

static void navigation_map_dma2d_transfer_error(void *arg)
{
    LOGE("%s \n", __func__);
}

static void navigation_map_dma2d_transfer_complete(void *arg)
{
    rtos_set_semaphore(&navigation_map_dma2d_sem);
}

bk_err_t navigation_map_dma2d_yuyv2rgb565_init(void)
{
    bk_err_t ret;

    if (navigation_map_dma2d_is_initialized) {
        LOGW("%s already initialized\n", __func__);
        return BK_OK;
    }

    ret = rtos_init_semaphore_ex(&navigation_map_dma2d_sem, 1, 0);
    if (BK_OK != ret) {
        LOGE("%s %d navigation_map_dma2d_sem init failed\n", __func__, __LINE__);
        return ret;
    }

    bk_dma2d_driver_init();
    bk_dma2d_register_int_callback_isr(DMA2D_CFG_ERROR_ISR, navigation_map_dma2d_config_error, NULL);
    bk_dma2d_register_int_callback_isr(DMA2D_TRANS_ERROR_ISR, navigation_map_dma2d_transfer_error, NULL);
    bk_dma2d_register_int_callback_isr(DMA2D_TRANS_COMPLETE_ISR, navigation_map_dma2d_transfer_complete, NULL);
    bk_dma2d_int_enable(DMA2D_CFG_ERROR | DMA2D_TRANS_ERROR | DMA2D_TRANS_COMPLETE, 1);

    navigation_map_data_rgb565 = psram_malloc(NAVIGATION_MAP_WIDTH * NAVIGATION_MAP_HEIGHT * sizeof(bk_color_t));
    if (navigation_map_data_rgb565 == NULL) {
        LOGE("%s %d navigation_map_data_rgb565 malloc failed\r\n", __func__, __LINE__);
        return BK_FAIL;
    }

    navigation_map_dma2d_is_initialized = true;

    return ret;
}

bk_err_t navigation_map_dma2d_yuyv2rgb565_deinit(void)
{
    bk_err_t ret;

    if (!navigation_map_dma2d_is_initialized) {
        LOGW("%s already deinitialized\n", __func__);
        return BK_OK;
    }

    bk_dma2d_stop_transfer();
    bk_dma2d_int_enable(DMA2D_CFG_ERROR | DMA2D_TRANS_ERROR | DMA2D_TRANS_COMPLETE, 0);
    bk_dma2d_driver_deinit();
    ret = rtos_deinit_semaphore(&navigation_map_dma2d_sem);
    if (BK_OK != ret) {
        LOGE("%s %d navigation_map_dma2d_sem deinit failed\n", __func__, __LINE__);
    }

    if (navigation_map_data_rgb565) {
        psram_free(navigation_map_data_rgb565);
        navigation_map_data_rgb565 = NULL;
    }

    navigation_map_dma2d_is_initialized = false;

    return ret;
}

static void navigation_map_dma2d_yuyv2rgb565(void *src, const void *dst, uint16_t width, uint16_t height, bool byte_swap)
{
    dma2d_memcpy_pfc_t dma2d_memcpy_pfc = {0};

    dma2d_memcpy_pfc.input_addr = (char *)src;
    dma2d_memcpy_pfc.output_addr = (char *)dst;
    dma2d_memcpy_pfc.mode = DMA2D_M2M_PFC;
    dma2d_memcpy_pfc.input_color_mode = DMA2D_INPUT_YUYV;
    dma2d_memcpy_pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
    dma2d_memcpy_pfc.src_pixel_byte = TWO_BYTES;
    dma2d_memcpy_pfc.dst_pixel_byte = TWO_BYTES;
    dma2d_memcpy_pfc.dma2d_width = width;
    dma2d_memcpy_pfc.dma2d_height = height;
    dma2d_memcpy_pfc.src_frame_width = width;
    dma2d_memcpy_pfc.src_frame_height = height;
    dma2d_memcpy_pfc.dst_frame_width = width;
    dma2d_memcpy_pfc.dst_frame_height = height;
    dma2d_memcpy_pfc.src_frame_xpos = 0;
    dma2d_memcpy_pfc.src_frame_ypos = 0;
    dma2d_memcpy_pfc.dst_frame_xpos = 0;
    dma2d_memcpy_pfc.dst_frame_ypos = 0;
    dma2d_memcpy_pfc.input_red_blue_swap = 0;
    dma2d_memcpy_pfc.output_red_blue_swap = 0;

    if (byte_swap) {
        dma2d_memcpy_pfc.out_byte_by_byte_reverse = 1;
    } else {
        dma2d_memcpy_pfc.out_byte_by_byte_reverse = 0;
    }

    bk_dma2d_memcpy_or_pixel_convert(&dma2d_memcpy_pfc);
    bk_dma2d_start_transfer();

    rtos_get_semaphore(&navigation_map_dma2d_sem, BEKEN_NEVER_TIMEOUT);
}

void lvgl_app_enter_navigation(void)
{
    if (navigation_is_opened) {
        LOGE("%s %d navigation is already entered\r\n", __func__, __LINE__);
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
        LOGE("%s %d navigation is already exited\r\n", __func__, __LINE__);
        return;
    }

    lv_vendor_disp_lock();
    lv_screen_load(bk_lv_tool_ui.page_1);
    destroy_page_page_2(&bk_lv_tool_ui);
    lv_vendor_disp_unlock();

    navigation_map_is_first_frame = true;
    navigation_is_opened = false;
}

void lvgl_app_display_navigation(uint8_t *data, uint32_t data_len, bool data_is_rgb565)
{
    if (data == NULL || data_len == 0) {
        LOGE("%s %d data is NULL or data_len is 0\r\n", __func__, __LINE__);
        return;
    }

    if (navigation_map.data_size != data_len) {
        LOGE("%s %d data_len is not equal to navigation_map.data_size\r\n", __func__, __LINE__);
        return;
    }

    if (data_is_rgb565 == false) {
        navigation_map_dma2d_yuyv2rgb565(data, navigation_map_data_rgb565, NAVIGATION_MAP_WIDTH, NAVIGATION_MAP_HEIGHT, false);
        navigation_map.data = navigation_map_data_rgb565;
    } else {
        navigation_map.data = data;
    }

    lv_vendor_disp_lock();

    if (navigation_map_is_first_frame) {
        navigation_map_is_first_frame = false;
        lv_image_set_src(bk_lv_tool_ui.page_2_image_8, &navigation_map);
        lv_screen_load(bk_lv_tool_ui.page_2);
    } else {
        lv_obj_invalidate(bk_lv_tool_ui.page_2_image_8);
    }
    lv_vendor_disp_unlock();
}
