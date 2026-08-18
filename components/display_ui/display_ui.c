#include "display_ui.h"
#include <avdk_check.h>
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_display.h>
#if CONFIG_LCD_HX8399C_MIPI_1080x1920
#include <lcd/lcd_mipi_hx8399c_1080x1920.h>
#endif
#if CONFIG_LCD_HX8394F_MIPI_720x1280
#include <lcd/lcd_mipi_hx8394f_720x1280.h>
#endif
#if CONFIG_LCD_GC9702_MIPI_720x1280
#include <lcd/lcd_mipi_gc9702720x1280.h>
#endif
#if CONFIG_LCD_FL7703NP_MIPI_720x1280
#include <lcd/lcd_mipi_fl7703np_720x1280.h>
#endif
#if CONFIG_LCD_LT8912B_MIPI_BRIDGE
#include <lcd/lcd_mipi_lt8912b_bridge.h>
#endif
#if CONFIG_LCD_ER68576B_MIPI_720x1280
#include <lcd/lcd_mipi_er68576b_720x1280.h>
#endif
#if CONFIG_LCD_EK79007AD_MIPI_1024x600
#include <lcd/lcd_mipi_ek79007ad_1024x600.h>
#endif
#include <common/avdk_pixel_types.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include "driver/drv_tp.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include "display_ui_cast_context.h"
#include "sdkconfig.h"
#include "app_display.h"

static rott_angle_t scooter_lvgl_rotation_from_deg(int deg)
{
    switch (deg) {
    case 0:
        return ROTATE_NONE;
    case 90:
        return ROTATE_90;
    case 180:
        return ROTATE_180;
    case 270:
        return ROTATE_270;
    default:
        return ROTATE_90;
    }
}

#define TAG "display_ui"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/*
 * Panel / DSI-bus / DPU ownership lives in the app_display layer
 * (app_mipi_lcd_turn_on). This component does not keep a heap display_ctx_t:
 * the DPU controller handle is obtained through app_mipi_lcd_handle_get() and
 * cached here, alongside the bound panel descriptor for geometry queries.
 */
static bk_display_ctlr_handle_t s_dpu_handle = NULL;
static const bk_display_dsi_panel_t *s_panel_desc = NULL;
static uint8_t *s_lvgl_fb[2] = {NULL, NULL};
static uint32_t s_lvgl_fb_size = 0;
static display_ui_init_callback_t s_ui_init_callback = NULL;

static void bk_widgets_flush_cb(void *args, void *frame_buffer, int (*cb)(void *args))
{
    bk_display_flush(args, frame_buffer, cb);
}

static const bk_display_dsi_panel_t *scooter_get_mipi_panel(void)
{
#if CONFIG_LCD_HX8399C_MIPI_1080x1920
    return &lcd_device_hx8399c_mipi_1080x1920;
#elif CONFIG_LCD_HX8394F_MIPI_720x1280
    return &lcd_device_hx8394f_mipi_720x1280;
#elif CONFIG_LCD_GC9702_MIPI_720x1280
    return &lcd_device_gc9702_mipi_720x1280;
#elif CONFIG_LCD_FL7703NP_MIPI_720x1280
    return &lcd_device_fl7703np_mipi_720x1280;
#elif CONFIG_LCD_ER68576B_MIPI_720x1280
    return &lcd_device_er68576b_mipi_720x1280;
#elif CONFIG_LCD_EK79007AD_MIPI_1024x600
    return &lcd_device_ek79007ad_mipi_1024x600;
#elif CONFIG_LCD_LT8912B_MIPI_BRIDGE
    return &lcd_device_lt8912b_mipi;
#else
    return NULL;
#endif
}

static void display_board_lcd_power_enable()
{
#if defined(CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0) && CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0
    /* LCD panel control/power enable (P45_LCD_PCTRL): must be high before the
     * panel is reset and initialized, otherwise the panel stays unpowered/dark. */
    gpio_dev_unmap(GPIO_45);
    BK_LOG_ON_ERR(bk_gpio_enable_output(GPIO_45));
    BK_LOG_ON_ERR(bk_gpio_pull_up(GPIO_45));
    bk_gpio_set_capacity(GPIO_45, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_45);

    /* Bring-up: drive the backlight (P46_LCD_BL) high here, BEFORE the DSI/panel
     * init, so it is not gated behind bk_display_open() success. This makes the
     * backlight a standalone electrical test: if it stays dark now, the issue is
     * power/pin/polarity; if it lights but shows no image, the issue is DSI/panel. */
    gpio_dev_unmap(GPIO_46);
    BK_LOG_ON_ERR(bk_gpio_enable_output(GPIO_46));
    BK_LOG_ON_ERR(bk_gpio_pull_up(GPIO_46));
    bk_gpio_set_capacity(GPIO_46, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_46);
#endif /* CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0 */
}


static bk_err_t display_hw_init_internal(void)
{
    s_panel_desc = scooter_get_mipi_panel();

    display_board_config_t display_board = {0};
    display_board.mipi.enable = true;
    display_board.mipi.pin_reset = CONFIG_PROJECT_LCD_RESET_PIN;
    display_board.mipi.pin_backlight = ((uint8_t)-1);
    display_board.mipi.panel = s_panel_desc;
    display_board.dpu_video.enable = true;
    /* LVGL outputs GPU-compressed ARGB8888 (VG_LITE_DEC_HV_SAMPLE tiles), so the
     * DPU scans out in ARGB8888 + decompress permanently. This matches the
     * boot_avi / cast / assist GPU output format, which lets us drop every
     * runtime bk_display_pixel_format_set() switch (see lvgl_start_internal:
     * output_compress). */
    display_board.dpu_video.decompress = true;
    display_board.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    app_display_board_config_set(&display_board);

    bk_err_t ret = app_mipi_lcd_turn_on(&display_board);
    if (ret != BK_OK) {
        LOGE("app_mipi_lcd_turn_on failed, ret: %d", ret);
        return BK_FAIL;
    }

    /* The DPU controller is owned by the app_display layer; grab its handle. */
    s_dpu_handle = (bk_display_ctlr_handle_t)app_mipi_lcd_handle_get();
    if (s_dpu_handle == NULL) {
        LOGE("app_mipi_lcd_handle_get returned NULL\n");
        return BK_FAIL;
    }
    return BK_OK;
}

static bk_err_t lvgl_start_internal(void)
{
    lv_vnd_config_t lv_vnd_config = {0};

    if (s_ui_init_callback == NULL)
    {
        LOGE("UI init callback is not registered\n");
        return BK_FAIL;
    }

    if (s_dpu_handle == NULL) {
        LOGE("display HW not initialized before LVGL start\n");
        return BK_FAIL;
    }

#if defined(CONFIG_SCOOTER_LVGL_HOR_RES) && defined(CONFIG_SCOOTER_LVGL_VER_RES)
    const uint32_t ui_w = (uint32_t)CONFIG_SCOOTER_LVGL_HOR_RES;
    const uint32_t ui_h = (uint32_t)CONFIG_SCOOTER_LVGL_VER_RES;
#else
    const uint32_t ui_w = 1080U;
    const uint32_t ui_h = 1920U;
#endif

    lv_vnd_config.width = (lv_coord_t)ui_w;
    lv_vnd_config.height = (lv_coord_t)ui_h;
    lv_vnd_config.render_mode = RENDER_PARTIAL_MODE;
    if (lv_vnd_config.render_mode == RENDER_PARTIAL_MODE) {
        lv_vnd_config.draw_pixel_size = 120 * 1024;
    }
#if defined(CONFIG_SCOOTER_UI_ROTATION)
    lv_vnd_config.rotation = scooter_lvgl_rotation_from_deg(CONFIG_SCOOTER_UI_ROTATION);
#else
    lv_vnd_config.rotation = ROTATE_90;
#endif
    /* LVGL -> GPU (rotate + compress into ARGB8888 VG_LITE_DEC_HV_SAMPLE tiles)
     * -> DPU (decompress + scanout). Enabling output_compress makes the GPU do
     * the rotation inside the compressed blit, so the porting layer skips the
     * 120KB software rotate_buffer (lv_port_disp.c only allocates it when
     * rotation != NONE && !output_compress). This frees ~120KB HSRAM (needed so
     * recording + full-screen assist coexist) and keeps the DPU in one format. */
    lv_vnd_config.output_compress = true;
    lv_vnd_config.disp_width = (uint16_t)ui_w;
    lv_vnd_config.disp_height = (uint16_t)ui_h;
    if (lv_vnd_config.output_compress &&
        lv_vnd_config.render_mode == RENDER_PARTIAL_MODE) {
        /* Compressed tiles are 16x4; the scanout surface must be tile-aligned. */
        if ((ui_w % 16U) || (ui_h % 4U)) {
            lv_vnd_config.disp_width = (uint16_t)((ui_w + 15U) & ~15U);
            lv_vnd_config.disp_height = (uint16_t)((ui_h + 3U) & ~3U);
        }
        LOGI("compress disp %ux%u (ui %ux%u)\n",
             (unsigned)lv_vnd_config.disp_width, (unsigned)lv_vnd_config.disp_height,
             (unsigned)ui_w, (unsigned)ui_h);
    }

    /* Compressed output frame buffers are tiled ARGB8888 stored at ~1 byte per
     * pixel (disp_w * disp_h bytes), smaller than the old RGB565 w*h*2. Mirrors
     * the reference widgets_v9 sizing for output_compress. */
    uint32_t fb_sz = (uint32_t)lv_vnd_config.disp_width * lv_vnd_config.disp_height;
    lv_vnd_config.frame_buffer[0] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, fb_sz);
    lv_vnd_config.frame_buffer[1] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, fb_sz);
    if (lv_vnd_config.frame_buffer[0] == NULL || lv_vnd_config.frame_buffer[1] == NULL) {
        LOGE("LVGL frame buffer alloc failed (need %u bytes each)\n", (unsigned)fb_sz);
        if (lv_vnd_config.frame_buffer[0])
            bk_frame_buffer_free(lv_vnd_config.frame_buffer[0]);
        if (lv_vnd_config.frame_buffer[1])
            bk_frame_buffer_free(lv_vnd_config.frame_buffer[1]);
        return BK_FAIL;
    }
    s_lvgl_fb[0] = lv_vnd_config.frame_buffer[0];
    s_lvgl_fb[1] = lv_vnd_config.frame_buffer[1];
    s_lvgl_fb_size = fb_sz;

    lv_vnd_config.args = s_dpu_handle;
    lv_vnd_config.flush_cb = bk_widgets_flush_cb;
    lv_vendor_init(&lv_vnd_config);

#if (CONFIG_TP)
    drv_tp_open(lv_vnd_config.width, lv_vnd_config.height, TP_MIRROR_NONE);
#endif

    lv_vendor_disp_lock();
    s_ui_init_callback();
    lv_vendor_disp_unlock();
    lv_vendor_start();

#if defined(CONFIG_SCOOTER_UI_ROTATION)
    const int rot_log = CONFIG_SCOOTER_UI_ROTATION;
#else
    const int rot_log = 90;
#endif
    LOGI("LVGL UI started (lv_vendor w=%u h=%u rot=%d deg)\n",
         (unsigned)ui_w, (unsigned)ui_h, rot_log);
    return BK_OK;
}

static bk_err_t lvgl_app_widgets_init(void)
{
    display_board_lcd_power_enable();
    bk_err_t ret = display_hw_init_internal();
    if (ret != BK_OK)
        return ret;
    return lvgl_start_internal();
}

bk_err_t display_ui_register_init_callback(display_ui_init_callback_t callback)
{
    if (callback == NULL)
    {
        return BK_FAIL;
    }

    s_ui_init_callback = callback;
    return BK_OK;
}

bk_err_t display_ui_init_display_hw(void)
{
    return display_hw_init_internal();
}

bk_err_t display_ui_start_lvgl(void)
{
    return lvgl_start_internal();
}

bk_err_t display_ui_init(void)
{
    return lvgl_app_widgets_init();
}

/* ==================== Internal getters (display_ui component only) ==================== */

uint8_t *display_ui_get_lvgl_fb(int index)
{
    if (index < 0 || index > 1)
        return NULL;
    return s_lvgl_fb[index];
}

/* ==================== Public getters (for external consumers) ==================== */

bk_display_ctlr_handle_t display_ui_get_dpu_handle(void)
{
    return s_dpu_handle;
}

const bk_display_dsi_panel_t *display_ui_get_panel_desc(void)
{
    return s_panel_desc;
}
