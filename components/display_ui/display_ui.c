#include "display_ui.h"

#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_display.h>
#include <components/bk_display_dpu_ctlr.h>
#include <components/bk_display_bus.h>
#include <components/bk_lcd_panel.h>
#if CONFIG_LCD_HX8399C_MIPI_1080x1920
#include <lcd/lcd_hx8399c_mipi_1080x1920.h>
#endif
#if CONFIG_LCD_HX8394F_MIPI_720x1280
#include <lcd/lcd_hx8394f_mipi_720x1280.h>
#endif
#if CONFIG_LCD_GC9702_MIPI_720x1280
#include <lcd/lcd_gc9702_mipi_720x1280.h>
#endif
#if CONFIG_LCD_FL7703NP_MIPI_720x1280
#include <lcd/lcd_fl7703np_mipi_720x1280.h>
#endif
#if CONFIG_LCD_LT8912B_MIPI_BRIDGE
#include <lcd/lcd_lt8912b_mipi_bridge.h>
#endif
#include <common/avdk_pixel_types.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include "driver/drv_tp.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include "display_ui_cast_context.h"
#include "sdkconfig.h"

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

extern void beken_ui_init(void);

static lv_vnd_config_t vendor_config = {0};

static display_ctx_t *g_disp_ctx = NULL;
static bk_display_dpu_config_t s_dpu_config;
static int s_scooter_lvgl_enabled = 0;
static uint8_t *s_lvgl_fb[2] = {NULL, NULL};
static uint32_t s_lvgl_fb_size = 0;

uint8_t *lvgl_get_idle_framebuffer(int index, uint32_t *out_size)
{
    if (index < 0 || index > 1 || s_lvgl_fb[index] == NULL)
        return NULL;
    if (out_size)
        *out_size = s_lvgl_fb_size;
    return s_lvgl_fb[index];
}

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
#elif CONFIG_LCD_LT8912B_MIPI_BRIDGE
    return &lcd_device_lt8912b_mipi;
#else
    return NULL;
#endif
}

static bk_err_t display_hw_init_internal(void)
{
    bk_err_t ret = BK_OK;
    bk_display_dpu_config_t dpu_config = {
        .video.enable = true,
        .video.decompress = false,
        .video.format = BK_PIXEL_FORMAT_RGB565,
    };
    const bk_lcd_panel_dev_config_t panel_dev_config = {
        .reset_pin = GPIO_60,
        .rgb_ele_order = COLOR_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .flags.reset_active_level = 0,
    };

    g_disp_ctx = os_malloc(sizeof(display_ctx_t));
    if (g_disp_ctx == NULL) {
        LOGE("Failed to allocate display context\n");
        return BK_FAIL;
    }
    os_memset(g_disp_ctx, 0, sizeof(display_ctx_t));
    AVDK_GOTO_ON_ERROR(bk_display_dsi_bus_new(&g_disp_ctx->dis_bus_handle, NULL), err, TAG, "display dsi bus new err\n");
    AVDK_GOTO_ON_ERROR(bk_display_bus_enable(g_disp_ctx->dis_bus_handle), err, TAG, "display bus enable err\n");
    {
        const bk_display_dsi_panel_t *panel = scooter_get_mipi_panel();
        if (panel == NULL) {
            LOGE("No MIPI panel selected in Kconfig (enable one LCD_* under DSI)\n");
            ret = BK_FAIL;
            goto err;
        }
        AVDK_GOTO_ON_ERROR(bk_lcd_mipi_panel_new(g_disp_ctx->dis_bus_handle, &panel_dev_config, panel, &g_disp_ctx->panel_handle),
                           err, TAG, "create panel err\n");
    }

    bk_lcd_panel_reset(g_disp_ctx->panel_handle);
    bk_lcd_panel_init(g_disp_ctx->panel_handle);
    bk_lcd_panel_get_disp_timing(g_disp_ctx->panel_handle, &dpu_config.timing);

    AVDK_GOTO_ON_ERROR(bk_display_dpu_ctlr_new(&g_disp_ctx->dpu_ctlr_handle, &dpu_config), err, TAG, "display dpu ctlr new err\n");
    AVDK_GOTO_ON_ERROR(bk_display_init(g_disp_ctx->dpu_ctlr_handle), err, TAG, "display init err\n");
    AVDK_GOTO_ON_ERROR(bk_display_open(g_disp_ctx->dpu_ctlr_handle), err, TAG, "display open err\n");  
    gpio_dev_unmap(GPIO_7);
    BK_LOG_ON_ERR(bk_gpio_enable_output(GPIO_7));
    BK_LOG_ON_ERR(bk_gpio_pull_up(GPIO_7));
    bk_gpio_set_capacity(GPIO_7, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_7);

    s_dpu_config = dpu_config;
    vendor_config.args = g_disp_ctx->dpu_ctlr_handle;
    return BK_OK;

err:
    if (g_disp_ctx) {
        if (g_disp_ctx->dpu_ctlr_handle) {
            bk_display_delete(g_disp_ctx->dpu_ctlr_handle);
            g_disp_ctx->dpu_ctlr_handle = NULL;
        }
        if (g_disp_ctx->panel_handle) {
            bk_lcd_panel_del(g_disp_ctx->panel_handle);
            g_disp_ctx->panel_handle = NULL;
        }
        if (g_disp_ctx->dis_bus_handle) {
            bk_display_bus_disable(g_disp_ctx->dis_bus_handle);
            bk_display_bus_delete(g_disp_ctx->dis_bus_handle);
            g_disp_ctx->dis_bus_handle = NULL;
        }
        os_free(g_disp_ctx);
        g_disp_ctx = NULL;
    }
    return ret;
}

static bk_err_t lvgl_start_internal(void)
{
    lv_vnd_config_t lv_vnd_config = {0};

    if (g_disp_ctx == NULL || g_disp_ctx->dpu_ctlr_handle == NULL) {
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
    {
        uint32_t fb_sz = ui_w * ui_h * sizeof(lv_color_t);
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
    }
    lv_vnd_config.args = g_disp_ctx->dpu_ctlr_handle;
    lv_vnd_config.flush_cb = bk_widgets_flush_cb;
    lv_vendor_init(&lv_vnd_config);

#if (CONFIG_TP)
    drv_tp_open(lv_vnd_config.width, lv_vnd_config.height, TP_MIRROR_NONE);
#endif

    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();
    lv_vendor_start();

    s_scooter_lvgl_enabled = 1;
    {
        int rot_log = 90;
#if defined(CONFIG_SCOOTER_UI_ROTATION)
        rot_log = CONFIG_SCOOTER_UI_ROTATION;
#endif
        LOGI("LVGL UI started (lv_vendor w=%u h=%u rot=%d deg)\n",
             (unsigned)ui_w, (unsigned)ui_h, rot_log);
    }
    return BK_OK;
}

static bk_err_t lvgl_app_widgets_init(void)
{
    bk_err_t ret = display_hw_init_internal();
    if (ret != BK_OK)
        return ret;
    return lvgl_start_internal();
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

display_ctx_t *display_ui_get_ctx(void)
{
    return g_disp_ctx;
}

int display_ui_is_lvgl_enabled(void)
{
    return s_scooter_lvgl_enabled;
}

uint8_t *display_ui_get_lvgl_fb(int index)
{
    if (index < 0 || index > 1)
        return NULL;
    return s_lvgl_fb[index];
}

/* ==================== Public getters (for external consumers) ==================== */

bk_display_ctlr_handle_t display_ui_get_dpu_handle(void)
{
    if (g_disp_ctx == NULL)
        return NULL;
    return g_disp_ctx->dpu_ctlr_handle;
}

bk_display_dpu_config_t *display_ui_get_dpu_config(void)
{
    return &s_dpu_config;
}
