#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/bk_frame_buffer.h>
#include <components/shell_task.h>
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <os/os.h>
#include <os/str.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_camera.h"
#include "app_display.h"
#include "app_gpu.h"
#include "avdk_monitor.h"
#include "beken_ui.h"
#include "cli.h"
#include "components/bk_display.h"
#include "devices_mgmt.h"
#include "driver/drv_tp.h"
#include "event_runtime.h"
#include "gpio_driver.h"
#include "klok_mv_render.h"
#include "klok_player_adapter.h"
#include "lv_vendor.h"
#include "media_service.h"
#include "sdcard_mtp.h"
#include "video_player_cli.h"

#include <lcd/lcd_mipi_fl7703np_720x1280.h>
#include <lcd/lcd_mipi_gc9702_720x1280.h>
#include <lcd/lcd_mipi_hx8394f_720x1280.h>
#include <lcd/lcd_mipi_er68576b_720x1280.h>

#define KLOK_MIPI_PANEL_GC9702    1
#define KLOK_MIPI_PANEL_HX8394F   2
#define KLOK_MIPI_PANEL_FL7703NP  3
#define KLOK_MIPI_PANEL_ER68576B  4

#ifndef KLOK_MIPI_PANEL
#define KLOK_MIPI_PANEL KLOK_MIPI_PANEL_ER68576B
#endif

#if KLOK_MIPI_PANEL == KLOK_MIPI_PANEL_GC9702
#define KLOK_MIPI_PANEL_DEVICE lcd_device_gc9702_mipi_720x1280
#elif KLOK_MIPI_PANEL == KLOK_MIPI_PANEL_HX8394F
#define KLOK_MIPI_PANEL_DEVICE lcd_device_hx8394f_mipi_720x1280
#elif KLOK_MIPI_PANEL == KLOK_MIPI_PANEL_FL7703NP
#define KLOK_MIPI_PANEL_DEVICE lcd_device_fl7703np_mipi_720x1280
#elif KLOK_MIPI_PANEL == KLOK_MIPI_PANEL_ER68576B
#define KLOK_MIPI_PANEL_DEVICE lcd_device_er68576b_mipi_720x1280
#else
#error "Unsupported KLOK_MIPI_PANEL"
#endif

#if CONFIG_BK_DECODER && CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
#include "components/bk_decode/bk_h264_decode_ctlr.h"
#include "components/bk_decode/bk_h264_decode_types.h"
#endif

#define SYS_ANA_REG_BASE    (0x44010000)
#define LDO_ANA_REG         (0x69)

#define KLOK_UI_WIDTH        (1280)
#define KLOK_UI_HEIGHT       (720)
#define KLOK_DISP_WIDTH      (720)
#define KLOK_DISP_HEIGHT     (1280)
#define KLOK_CLI_PATH_MAX    (256)

typedef enum {
    KLOK_UI_PAGE_HOME = 0,
    KLOK_UI_PAGE_MAIN,
    KLOK_UI_PAGE_LIST,
    KLOK_UI_PAGE_PLAY,
} klok_ui_page_t;

static bool s_klok_ui_ready = false;
static klok_ui_page_t s_klok_current_page = KLOK_UI_PAGE_HOME;

static void bk_auxldo_enable(void)
{
    uint32_t reg = REG_READ(SYS_ANA_REG_BASE + LDO_ANA_REG * 4);
    reg |= (0xF << 28) | (0x2 << 23) | (0x7 << 19) | (0x7 << 15);
    reg &= ~(0xF << 11);
    reg |= (0x8 << 11);
    REG_WRITE(SYS_ANA_REG_BASE + LDO_ANA_REG * 4, reg);
}

#if CONFIG_BK_DECODER && CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
static void h264_hw_decoder_prewarm(void)
{
    bk_h264_decode_ctlr_handle_t handle = NULL;
    bk_h264_decode_frame_config_t cfg = DEFAULT_H264_DECODE_FRAME_CONFIG;

    cfg.timeout_ms = 1000U;
    cfg.out_width = KLOK_UI_WIDTH;
    cfg.out_height = KLOK_UI_HEIGHT;
    cfg.out_format = BK_PIXEL_FORMAT_NV12;
    cfg.frame_done_cb = NULL;
    cfg.frame_done_args = NULL;

    if (bk_h264_decode_frame_ctlr_new(&handle, &cfg) != BK_OK || handle == NULL) {
        bk_printf("h264 prewarm: ctlr_new failed\r\n");
        return;
    }

    if (bk_h264_decode_init(handle) != BK_OK) {
        bk_printf("h264 prewarm: init failed\r\n");
        (void)bk_h264_decode_delete(handle);
        return;
    }

    (void)bk_h264_decode_deinit(handle);
    (void)bk_h264_decode_delete(handle);
}
#endif

static void bk_ui_flush_cb(void *args, void *frame_buffer, int (*cb)(void *args))
{
    if (klok_mv_render_handle_lvgl_flush(frame_buffer, cb)) {
        return;
    }

    (void)bk_display_flush((bk_display_ctlr_handle_t)args, frame_buffer, cb);
}

static void klok_cli_usage(void)
{
    bk_printf("usage:\r\n");
    bk_printf("  klok page <home|main|list|play> [file]\r\n");
    bk_printf("  klok back\r\n");
    bk_printf("  klok play [file]\r\n");
    bk_printf("  klok full | fullscreen\r\n");
    bk_printf("  klok next | replay | stop | pause | resume | playpause\r\n");
    bk_printf("  klok audio <0|1> | accompany | vocal\r\n");
    bk_printf("  klok mute | volup | voldown\r\n");
}

static int klok_cli_build_file_path(const char *arg, char *path, size_t path_size)
{
    int len;

    if (arg == NULL || path == NULL || path_size == 0) {
        return BK_FAIL;
    }

    if (arg[0] == '/') {
        len = snprintf(path, path_size, "%s", arg);
    } else {
        len = snprintf(path, path_size, "/sd0/%s", arg);
    }

    if (len < 0 || (size_t)len >= path_size) {
        bk_printf("Klok file path is too long: %s\r\n", arg);
        return BK_FAIL;
    }

    return BK_OK;
}

static klok_ui_page_t klok_ui_current_page_locked(void)
{
    lv_obj_t *active = lv_screen_active();

    if (active != NULL) {
        if (bk_lv_tool_ui.mv_play != NULL && lv_obj_is_valid(bk_lv_tool_ui.mv_play) &&
            active == bk_lv_tool_ui.mv_play) {
            return KLOK_UI_PAGE_PLAY;
        }
        if (bk_lv_tool_ui.song_list != NULL && lv_obj_is_valid(bk_lv_tool_ui.song_list) &&
            active == bk_lv_tool_ui.song_list) {
            return KLOK_UI_PAGE_LIST;
        }
        if (bk_lv_tool_ui.klok_main != NULL && lv_obj_is_valid(bk_lv_tool_ui.klok_main) &&
            active == bk_lv_tool_ui.klok_main) {
            return KLOK_UI_PAGE_MAIN;
        }
        if (bk_lv_tool_ui.home != NULL && lv_obj_is_valid(bk_lv_tool_ui.home) &&
            active == bk_lv_tool_ui.home) {
            return KLOK_UI_PAGE_HOME;
        }
    }

    return s_klok_current_page;
}

static int klok_ui_navigate(const char *page)
{
    klok_ui_page_t target_page = s_klok_current_page;
    const bool leaving_play =
        s_klok_current_page == KLOK_UI_PAGE_PLAY &&
        os_strcmp(page, "play") != 0 &&
        os_strcmp(page, "mv") != 0 &&
        os_strcmp(page, "list") != 0 &&
        os_strcmp(page, "song") != 0;

    if (!s_klok_ui_ready) {
        bk_printf("Klok UI is not ready\r\n");
        return BK_FAIL;
    }

    if (leaving_play) {
        (void)klok_player_stop();
    }

    lv_vendor_disp_lock();

    if (os_strcmp(page, "home") == 0) {
        klok_mv_render_leave_locked();
        navigate_to_screen(&bk_lv_tool_ui.home, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_home);
        target_page = KLOK_UI_PAGE_HOME;
    } else if (os_strcmp(page, "main") == 0 || os_strcmp(page, "klok") == 0) {
        klok_mv_render_leave_locked();
        navigate_to_screen(&bk_lv_tool_ui.klok_main, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_klok_main);
        target_page = KLOK_UI_PAGE_MAIN;
    } else if (os_strcmp(page, "list") == 0 || os_strcmp(page, "song") == 0) {
        klok_mv_render_leave_locked();
        navigate_to_screen(&bk_lv_tool_ui.song_list, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_song_list);
        klok_song_list_refresh(&bk_lv_tool_ui);
        target_page = KLOK_UI_PAGE_LIST;
    } else if (os_strcmp(page, "play") == 0 || os_strcmp(page, "mv") == 0) {
        navigate_to_screen(&bk_lv_tool_ui.mv_play, LV_SCR_LOAD_ANIM_NONE, 300, 0, false, init_page_mv_play);
        target_page = KLOK_UI_PAGE_PLAY;
    } else {
        lv_vendor_disp_unlock();
        bk_printf("unknown page: %s\r\n", page);
        return BK_FAIL;
    }

    s_klok_current_page = target_page;
    lv_vendor_disp_unlock();
    bk_printf("Klok page switched to %s\r\n", page);
    return BK_OK;
}

static int klok_ui_back(void)
{
    klok_ui_page_t current_page;
    const char *target = "home";

    if (!s_klok_ui_ready) {
        bk_printf("Klok UI is not ready\r\n");
        return BK_FAIL;
    }

    lv_vendor_disp_lock();
    current_page = klok_ui_current_page_locked();
    lv_vendor_disp_unlock();

    switch (current_page) {
    case KLOK_UI_PAGE_PLAY:
        target = "list";
        break;
    case KLOK_UI_PAGE_LIST:
        target = "main";
        break;
    case KLOK_UI_PAGE_MAIN:
    case KLOK_UI_PAGE_HOME:
    default:
        target = "home";
        break;
    }

    return klok_ui_navigate(target);
}

static void klok_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char file_path[KLOK_CLI_PATH_MAX];

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        klok_cli_usage();
        return;
    }

    if (os_strcmp(argv[1], "page") == 0) {
        if (argc < 3) {
            klok_cli_usage();
            return;
        }
        if ((os_strcmp(argv[2], "play") == 0 || os_strcmp(argv[2], "mv") == 0) && argc >= 4) {
            if (klok_cli_build_file_path(argv[3], file_path, sizeof(file_path)) == BK_OK) {
                /*
                 * Load and arm the selected MV renderer before starting the
                 * frame decoder. PP OSD must own the very first RGB565 frame;
                 * starting playback first leaves a transition window where
                 * VCDEC PP runs without its OSD consumer.
                 */
                (void)klok_ui_navigate(argv[2]);
                (void)klok_player_play_file(file_path);
                return;
            }
        }
        (void)klok_ui_navigate(argv[2]);
    } else if (os_strcmp(argv[1], "back") == 0 || os_strcmp(argv[1], "return") == 0) {
        (void)klok_ui_back();
    } else if (os_strcmp(argv[1], "play") == 0) {
        (void)klok_ui_navigate("play");
        if (argc >= 3) {
            if (klok_cli_build_file_path(argv[2], file_path, sizeof(file_path)) == BK_OK) {
                (void)klok_player_play_file(file_path);
            }
        } else {
            (void)klok_player_play_default();
        }
    } else if (os_strcmp(argv[1], "full") == 0 || os_strcmp(argv[1], "fullscreen") == 0) {
        (void)klok_ui_navigate("play");
    } else if (os_strcmp(argv[1], "next") == 0) {
        (void)klok_player_next();
        (void)klok_ui_navigate("play");
    } else if (os_strcmp(argv[1], "pause") == 0) {
        (void)klok_player_pause();
    } else if (os_strcmp(argv[1], "resume") == 0) {
        (void)klok_player_resume();
        (void)klok_ui_navigate("play");
    } else if (os_strcmp(argv[1], "playpause") == 0 || os_strcmp(argv[1], "toggle") == 0) {
        (void)klok_player_pause_toggle();
    } else if (os_strcmp(argv[1], "replay") == 0) {
        (void)klok_player_replay();
        (void)klok_ui_navigate("play");
    } else if (os_strcmp(argv[1], "stop") == 0) {
        (void)klok_player_stop();
    } else if (os_strcmp(argv[1], "audio") == 0) {
        if (argc < 3) {
            klok_cli_usage();
            return;
        }
        (void)klok_player_audio_track((uint8_t)os_strtoul(argv[2], NULL, 10));
    } else if (os_strcmp(argv[1], "accompany") == 0) {
        (void)klok_player_accompany();
    } else if (os_strcmp(argv[1], "vocal") == 0) {
        (void)klok_player_vocal();
    } else if (os_strcmp(argv[1], "mute") == 0) {
        (void)klok_player_mute_toggle();
    } else if (os_strcmp(argv[1], "volup") == 0 || os_strcmp(argv[1], "volume_up") == 0) {
        (void)klok_player_volume_up();
    } else if (os_strcmp(argv[1], "voldown") == 0 || os_strcmp(argv[1], "volume_down") == 0) {
        (void)klok_player_volume_down();
    } else {
        klok_cli_usage();
    }
}

static const struct cli_command s_klok_commands[] =
{
    {"klok", "klok commands", klok_cli_cmd},
};

static int cli_klok_init(void)
{
    return cli_register_commands(s_klok_commands, sizeof(s_klok_commands) / sizeof(s_klok_commands[0]));
}

/* SCH-7259 V1.0: P39 enables 3V3-E. The on-board SD-NAND SD_VDD rail is fed
 * from 3V3-E through R61, so this rail must be up before any FATFS mount. */
static void board_3v3_e_power_enable(void)
{
    gpio_dev_unmap(GPIO_39);
    (void)bk_gpio_enable_output(GPIO_39);
    (void)bk_gpio_pull_up(GPIO_39);
    bk_gpio_set_capacity(GPIO_39, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_39);
    rtos_delay_milliseconds(10);
}

static void display_board_panel_power_enable(void)
{
    gpio_dev_unmap(GPIO_45);
    (void)bk_gpio_enable_output(GPIO_45);
    (void)bk_gpio_pull_up(GPIO_45);
    bk_gpio_set_capacity(GPIO_45, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_45);
    rtos_delay_milliseconds(20);
    bk_printf("LCD panel power: P45=%d\r\n", bk_gpio_get_output(GPIO_45));
}

static void lvgl_ui_init(void)
{
    lv_vnd_config_t lv_vnd_config = {0};
    bk_display_ctlr_handle_t lcd_handle = NULL;
    uint32_t frame_buffer_size = 0;
    int ret;

    display_board_panel_power_enable();
    ret = app_mipi_lcd_turn_on(app_display_board_config_get());

    if (ret != BK_OK) {
        bk_printf("Klok UI: app_mipi_lcd_turn_on failed, ret=%d\r\n", ret);
        return;
    }

    lcd_handle = (bk_display_ctlr_handle_t)app_mipi_lcd_handle_get();
    if (lcd_handle == NULL) {
        bk_printf("Klok UI: app_mipi_lcd_handle_get returned NULL\r\n");
        return;
    }
    lv_vnd_config.width = KLOK_DISP_WIDTH;
    lv_vnd_config.height = KLOK_DISP_HEIGHT;
    lv_vnd_config.render_mode = RENDER_PARTIAL_MODE;
    lv_vnd_config.draw_pixel_size = KLOK_UI_WIDTH * 36 * sizeof(bk_color_t);
    /* LVGL ROTATE_270 maps to vg_lite 90°, matching PP-OSD VIDEO_PLAY_ROTATE_90. */
    lv_vnd_config.rotation = ROTATE_270;
    lv_vnd_config.output_compress = true;
    lv_vnd_config.disp_width = KLOK_DISP_WIDTH;
    lv_vnd_config.disp_height = KLOK_DISP_HEIGHT;
    frame_buffer_size = KLOK_DISP_WIDTH * KLOK_DISP_HEIGHT;
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        lv_vnd_config.frame_buffer[i] = bk_frame_buffer_malloc(
            (i % 2) ? MEM_SLAB_HEAP_UNCODED : MEM_SLAB_HEAP_CODED,
            frame_buffer_size);
    }
    lv_vnd_config.args = lcd_handle;
    lv_vnd_config.flush_cb = bk_ui_flush_cb;

    lv_vendor_init(&lv_vnd_config);
    if (klok_mv_render_init(lcd_handle) != BK_OK) {
        bk_printf("Klok UI: MV render init failed\r\n");
    }

#if (CONFIG_TP)
    /* SCH-7259 V1.0: TP shares LCD FPC power (P45_LCD_PCTRL). Soft-I2C does not
     * gpio_dev_unmap itself, so detach IO-matrix funcs before drv_tp_open. */
    gpio_dev_unmap(CONFIG_TP_RST_PIN);
    gpio_dev_unmap(CONFIG_TP_INT_PIN);
    gpio_dev_unmap(CONFIG_TP_I2C_SDA_PIN);
    gpio_dev_unmap(CONFIG_TP_I2C_SCL_PIN);
    rtos_delay_milliseconds(50);
    if (drv_tp_open(lv_vnd_config.width, lv_vnd_config.height, TP_MIRROR_NONE) != BK_OK) {
        bk_printf("Klok UI: drv_tp_open failed\r\n");
    }
#endif

    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();

    lv_vendor_start();
    s_klok_ui_ready = true;
    s_klok_current_page = KLOK_UI_PAGE_HOME;
}

int main(void)
{
    bk_init();
    board_3v3_e_power_enable();
    media_service_init();

    bk_printf("Klok main running...\r\n");

    camera_board_config_t camera_board = {0};
    display_board_config_t display_board = {0};
    gpu_board_config_t gpu_board = {0};

    display_board.mipi.enable = true;
    display_board.mipi.pin_reset = GPIO_42;
    display_board.mipi.pin_backlight = GPIO_46;
    display_board.mipi.panel = &KLOK_MIPI_PANEL_DEVICE;
    display_board.dpu_video.enable = true;
    display_board.dpu_video.decompress = true;
    display_board.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;

    gpu_board.flexa.enable = false;

    bk_auxldo_enable();
    bk_frame_buffer_init();

#if CONFIG_BK_DECODER && CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
    h264_hw_decoder_prewarm();
#endif

    app_camera_board_config_set(&camera_board);
    app_display_board_config_set(&display_board);
    app_gpu_board_config_set(&gpu_board);

    avdk_monitor_init();
    avdk_monitor_start();

    devices_mgmt_init();

    klok_player_adapter_init();
    cli_video_player_init();
    cli_klok_init();
    (void)sdcard_mtp_init();
    lvgl_ui_init();

    return 0;
}
