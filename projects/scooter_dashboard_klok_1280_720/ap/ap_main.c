#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/bk_frame_buffer.h>
#include <components/shell_task.h>
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <driver/mipi_dsi.h>
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
#include "driver/tp_types.h"
#include "event_runtime.h"
#include "gpio_driver.h"
#include "key_app_service.h"
#include "klok_mv_render.h"
#include "klok_player_adapter.h"
#include "lv_vendor.h"
#include "media_service.h"
#include "sdcard_mtp.h"
#include "video_play_engine_api.h"
#include "video_player_cli.h"
#include "music_player_ui.h"

/* BK7259V2 uses its primary on-chip Bluetooth controller. */
#define KLOK_BLUETOOTH_ENABLE 1
#define KLOK_LCD_VPG_TEST     0

#if CONFIG_BT
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "bt_manager.h"
#include "headset_user_config.h"
#include "a2dp_sink_demo.h"
#include "a2dp_sink_audio.h"
#if CONFIG_HFP_HF_DEMO
#include "hfp_hf_demo.h"
#include "hfp_hf_audio.h"
#endif
#if CONFIG_PBAP_CONTACTS
#include "pbap_contacts.h"
#endif
#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
#include "bsc_api.h"
#endif
#endif

#include <lcd/lcd_mipi_er68576b_720x1280.h>
#include "klok_board_profile.h"

#define KLOK_MIPI_PANEL_DEVICE lcd_device_er68576b_mipi_720x1280

#if CONFIG_BK_DECODER && CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
#include "components/bk_decode/bk_h264_decode_ctlr.h"
#include "components/bk_decode/bk_h264_decode_types.h"
#endif

#define SYS_ANA_REG_BASE    (0x44010000)
#define LDO_ANA_REG         (0x69)
#define SYS_M55_BASE_ADDR   (0x48000000)

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
    KLOK_UI_PAGE_MUSIC,
} klok_ui_page_t;

static bool s_klok_ui_ready = false;
static klok_ui_page_t s_klok_current_page = KLOK_UI_PAGE_HOME;
static volatile bool s_music_open_pending = false;
static volatile bool s_display_backlight_enabled = false;

static void display_board_backlight_enable(void);

void klok_ui_open_music_async(void);

#if CONFIG_BT && KLOK_BLUETOOTH_ENABLE
static beken_thread_t s_bt_start_thread;

#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
static bk_err_t klok_bsc_init(void)
{
    /* SCH-7259 V1.0 onboard BK3515NS uses UART1 RX=P0, TX=P1. */
    bsc_config_t cfg = {
        .uart_id = CONFIG_BLUETOOTH_BSC_UART_ID,
        .reset_pin = CONFIG_BLUETOOTH_BSC_RESET_PIN,
        .init_baud = CONFIG_BLUETOOTH_BSC_INIT_BAUD,
        .nego_baud = CONFIG_BLUETOOTH_BSC_NEGO_BAUD,
        .uart_config = {
            .baud_rate = CONFIG_BLUETOOTH_BSC_INIT_BAUD,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_NONE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_FLOWCTRL_DISABLE,
            .src_clk = UART_SCLK_XTAL_26M,
            .enable_sw_flow_ctrl = 0,
            .rts_gpio = CONFIG_BLUETOOTH_BSC_RTS_PIN,
            .cts_gpio = CONFIG_BLUETOOTH_BSC_CTS_PIN,
        },
    };
    return bk_cpn_bsc_init(&cfg);
}
#endif

static void klok_bt_sync_base_mac_from_cp(void)
{
    extern bk_err_t bk_ap_get_mac(uint8_t *mac, mac_type_t type);
    extern bk_err_t bk_set_base_mac(const uint8_t *mac);
    uint8_t dummy[6];
    uint8_t base_mac[6] = {0};

    /* Initialise the local MAC cache before replacing it with CP's base MAC. */
    (void)bk_get_mac(dummy, MAC_TYPE_BASE);
    if (bk_ap_get_mac(base_mac, MAC_TYPE_BASE) == BK_OK) {
        (void)bk_set_base_mac(base_mac);
        bk_printf("Klok BT base MAC synced from CP\r\n");
    } else {
        bk_printf("Klok BT base MAC sync failed\r\n");
    }
}

static void klok_bt_start_task(void *arg)
{
    static char local_name[] = "蓝牙音乐";
    bt_manager_cfg_t manager_cfg = {
        .local_name = local_name,
        .device_class = COD_SOUNDBAR,
        .page_scan_interval = PAGE_SCAN_INTV,
        .page_scan_window = PAGE_SCAN_WIN,
        .page_timeout = CONFIG_PAGE_TIMEOUT,
        .reconnect_interval_ms = CONFIG_RECONN_INTERVAL,
        .max_reconnect_count = CONFIG_MAX_RECONN_COUNT,
        .io_capability = BK_BT_IO_CAP_NONE,
    };
    (void)arg;
    klok_bt_sync_base_mac_from_cp();
#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
    if (klok_bsc_init() != BK_OK) {
        bk_printf("Klok BSC init failed\r\n");
        goto out;
    }
#endif
    if (bk_bluetooth_init() != BK_OK) {
        bk_printf("Klok Bluetooth init failed\r\n");
        goto out;
    }
    bt_manager_init(&manager_cfg);
    {
        onboard_speaker_pa_ctrl_t bt_pa = DEFAULT_ONBOARD_SPEAKER_PA_CTRL();
        bt_pa.pa_ctrl_en   = true;
        bt_pa.pa_ctrl_gpio = 13;
        bt_pa.pa_on_level  = 1;

        if (a2dp_sink_demo_init(0, 1) != BK_OK) {
            bk_printf("Klok A2DP sink init failed\r\n");
            goto out;
        }
        a2dp_sink_audio_set_pa_ctrl(&bt_pa);
#if CONFIG_HFP_HF_DEMO
        if (hfp_hf_demo_init(0) != BK_OK) {
            bk_printf("Klok HFP HF init failed\r\n");
            goto out;
        }
        hfp_hf_audio_set_pa_ctrl(&bt_pa);
#endif
    }
#if CONFIG_PBAP_CONTACTS
    pbap_contacts_init();
#endif
    music_player_ui_register_bt_callback();
    bk_printf("Klok Bluetooth ready as %s (A2DP/AVRCP/HFP/PBAP)\r\n",
              local_name);
out:
    s_bt_start_thread = NULL;
    rtos_delete_thread(NULL);
}

static void klok_bt_start(void)
{
    if (rtos_create_thread(&s_bt_start_thread,
                           BEKEN_DEFAULT_WORKER_PRIORITY,
                           "klok_bt",
                           (beken_thread_function_t)klok_bt_start_task,
                           8192,
                           NULL) != BK_OK) {
        s_bt_start_thread = NULL;
        bk_printf("Klok Bluetooth startup task failed\r\n");
    }
}
#else
static void klok_bt_start(void) {}
#endif

static void bk_auxldo_enable(void)
{
    uint32_t reg = REG_READ(SYS_ANA_REG_BASE + LDO_ANA_REG * 4);
    reg |= (0xF << 28) | (0x2 << 23) | (0x7 << 19) | (0x7 << 15);
    reg &= ~(0xF << 11);
    reg |= (0x8 << 11);
    REG_WRITE(SYS_ANA_REG_BASE + LDO_ANA_REG * 4, reg);

    /* Match the proven scooter-dashboard board bring-up: release the
     * peripheral clock gates before DSI/DPU initialization. */
    reg = REG_READ(SYS_M55_BASE_ADDR + 0xA * 4);
    reg &= ~(1 << 2);  /* USB HS */
    reg &= ~(1 << 5);  /* QSPI0 */
    reg &= ~(1 << 6);  /* QSPI1 */
    reg &= ~(1 << 7);  /* SDIO0 */
    reg &= ~(1 << 8);  /* SDIO1 */
    reg &= ~(1 << 9);  /* ISP */
    reg &= ~(1 << 10); /* GPU */
    reg &= ~(1 << 11); /* H264E */
    reg &= ~(1 << 12); /* CSI */
    reg &= ~(1 << 13); /* DSI */
    reg &= ~(1 << 14); /* DPU */
    reg &= ~(1 << 15); /* USB FS */
    reg &= ~(1 << 22); /* NPU */
    reg &= ~(0x3F << 26);
    REG_WRITE(SYS_M55_BASE_ADDR + 0xA * 4, reg);
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

    int ret = bk_display_flush((bk_display_ctlr_handle_t)args, frame_buffer, cb);
    if (ret == BK_OK && !s_display_backlight_enabled) {
        s_display_backlight_enabled = true;
        display_board_backlight_enable();
    }
}

static void klok_cli_usage(void)
{
    bk_printf("usage:\r\n");
    bk_printf("  klok page <home|main|list|play|music> [file]\r\n");
    bk_printf("  klok back\r\n");
    bk_printf("  klok play [file]\r\n");
    bk_printf("  klok full | fullscreen\r\n");
    bk_printf("  klok next | prev | replay | stop | pause | resume | playpause\r\n");
    bk_printf("  klok audio <0|1> | accompany | vocal\r\n");
    bk_printf("  klok mute | volup | voldown\r\n");
#if CONFIG_BT
    bk_printf("  klok bt_clear  (clear saved Bluetooth pairings, then reboot)\r\n");
#endif
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
        if (bk_lv_tool_ui.music_player != NULL && lv_obj_is_valid(bk_lv_tool_ui.music_player) &&
            active == bk_lv_tool_ui.music_player) {
            return KLOK_UI_PAGE_MUSIC;
        }
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
    klok_ui_page_t current_page;
    klok_ui_page_t target_page = s_klok_current_page;

    if (!s_klok_ui_ready) {
        bk_printf("Klok UI is not ready\r\n");
        return BK_FAIL;
    }
    if (os_strcmp(page, "music") == 0) {
        klok_ui_open_music_async();
        return BK_OK;
    }

    /*
     * Some generated page callbacks navigate directly and do not update
     * s_klok_current_page. Inspect the active LVGL screen before deciding
     * whether the Flexa player must be stopped; otherwise a bezel HOME press
     * can restore LVGL while the decoder is still driving the DPU.
     */
    lv_vendor_disp_lock();
    current_page = klok_ui_current_page_locked();
    lv_vendor_disp_unlock();
    const bool leaving_play =
        current_page == KLOK_UI_PAGE_PLAY &&
        os_strcmp(page, "play") != 0 &&
        os_strcmp(page, "mv") != 0 &&
        os_strcmp(page, "list") != 0 &&
        os_strcmp(page, "song") != 0;

    if (leaving_play) {
        (void)klok_player_stop();
    }
    if (current_page == KLOK_UI_PAGE_MUSIC) {
        /*
         * Stop before taking the display lock: the preview worker may need the
         * same lock to retire its final LVGL callback.
         */
        (void)klok_player_stop();
    }

    lv_vendor_disp_lock();

    if (klok_ui_current_page_locked() == KLOK_UI_PAGE_MUSIC) {
        music_player_ui_leave();
    }

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

static void klok_ui_open_music_finish(void *user_data)
{
    (void)user_data;

    klok_mv_render_leave_locked();
    /*
     * navigate_to_screen() only invokes its initializer for a missing page.
     * The music screen is retained after leaving, but its timers, background
     * player and A2DP user vote have already been stopped. Delete that stale
     * object and rebuild it so music_player_ui_enter() always runs.
     */
    if (bk_lv_tool_ui.music_player != NULL &&
        lv_obj_is_valid(bk_lv_tool_ui.music_player)) {
        lv_obj_delete(bk_lv_tool_ui.music_player);
        bk_lv_tool_ui.music_player = NULL;
    }
    init_page_music_player(&bk_lv_tool_ui);
    navigate_to_screen(&bk_lv_tool_ui.music_player,
                       LV_SCR_LOAD_ANIM_MOVE_LEFT,
                       220,
                       0,
                       false,
                       NULL);
    s_klok_current_page = KLOK_UI_PAGE_MUSIC;
    s_music_open_pending = false;
    bk_printf("Klok async music enter complete\r\n");
}

static void klok_ui_open_music_worker(void *user_data)
{
    (void)user_data;

    /*
     * Flexa's display worker may need the LVGL lock while stopping. Complete
     * that teardown first, then schedule all LVGL object changes on its task.
     */
    (void)klok_player_stop();

    lv_vendor_disp_lock();
    lv_result_t ret = lv_async_call(klok_ui_open_music_finish, NULL);
    lv_vendor_disp_unlock();
    if (ret != LV_RESULT_OK) {
        s_music_open_pending = false;
        bk_printf("Klok failed to schedule music enter\r\n");
    }
    rtos_delete_thread(NULL);
}

void klok_ui_open_music_async(void)
{
    if (!s_klok_ui_ready || s_music_open_pending) {
        return;
    }
    s_music_open_pending = true;

    beken_thread_t worker = NULL;
    if (rtos_create_thread(&worker,
                           BEKEN_DEFAULT_WORKER_PRIORITY,
                           "music_enter",
                           (beken_thread_function_t)klok_ui_open_music_worker,
                           4096,
                           NULL) != BK_OK) {
        s_music_open_pending = false;
        bk_printf("Klok failed to create music enter worker\r\n");
    }
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
    case KLOK_UI_PAGE_MUSIC:
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
    } else if (os_strcmp(argv[1], "prev") == 0 || os_strcmp(argv[1], "previous") == 0) {
        (void)klok_player_previous();
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
#if CONFIG_BT
    } else if (os_strcmp(argv[1], "bt_clear") == 0) {
        bt_manager_clean_bond();
        bk_printf("Klok Bluetooth pairings cleared; reboot and pair again\r\n");
#endif
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

static void board_3v3_e_power_enable(void)
{
    gpio_dev_unmap(GPIO_39);
    (void)bk_gpio_enable_output(GPIO_39);
    (void)bk_gpio_pull_up(GPIO_39);
    bk_gpio_set_capacity(GPIO_39, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_39);
    rtos_delay_milliseconds(10);
    bk_printf("SD profile: host=1 bus=1-bit pins=P14/P15/P16 power=P39\r\n");
}

/* SCH-7259 V1.0: P13 enables the external amplifier output. */
static void board_audio_amp_unmute(void)
{
    gpio_dev_unmap(GPIO_13);
    (void)bk_gpio_enable_output(GPIO_13);
    (void)bk_gpio_pull_up(GPIO_13);
    bk_gpio_set_capacity(GPIO_13, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_13);
    bk_printf("Audio amplifier unmuted: P13=%d\r\n", bk_gpio_get_output(GPIO_13));
}

static void display_board_panel_power_prepare(void)
{
    /*
     * P45 is panel power/control and must settle before RESET/DSI init. Keep
     * P46 low until a valid LVGL frame has been submitted, otherwise the panel
     * exposes uninitialized scanout memory during boot.
     */
    gpio_dev_unmap(GPIO_45);
    (void)bk_gpio_enable_output(GPIO_45);
    (void)bk_gpio_pull_up(GPIO_45);
    bk_gpio_set_capacity(GPIO_45, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_45);

    gpio_dev_unmap(GPIO_46);
    (void)bk_gpio_enable_output(GPIO_46);
    (void)bk_gpio_pull_down(GPIO_46);
    bk_gpio_set_capacity(GPIO_46, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_low(GPIO_46);
    s_display_backlight_enabled = false;

    rtos_delay_milliseconds(20);
    bk_printf("ER68576B power ready: P45=%d P46=%d\r\n",
              bk_gpio_get_output(GPIO_45), bk_gpio_get_output(GPIO_46));
}

static void display_board_backlight_enable(void)
{
    gpio_dev_unmap(GPIO_46);
    (void)bk_gpio_enable_output(GPIO_46);
    (void)bk_gpio_pull_up(GPIO_46);
    bk_gpio_set_capacity(GPIO_46, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(GPIO_46);
    bk_printf("ER68576B first frame ready: backlight P46=%d\r\n",
              bk_gpio_get_output(GPIO_46));
}

static void lvgl_ui_init(void)
{
    lv_vnd_config_t lv_vnd_config = {0};
    bk_display_ctlr_handle_t lcd_handle = NULL;
    uint32_t frame_buffer_size = 0;
    int ret;

    display_board_panel_power_prepare();
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
    bk_printf("TP profile: RST=P%d INT=P%d SDA=P%d SCL=P%d\r\n",
              CONFIG_TP_RST_PIN, CONFIG_TP_INT_PIN,
              CONFIG_TP_I2C_SDA_PIN, CONFIG_TP_I2C_SCL_PIN);
    gpio_dev_unmap(CONFIG_TP_RST_PIN);
    gpio_dev_unmap(CONFIG_TP_INT_PIN);
    gpio_dev_unmap(CONFIG_TP_I2C_SDA_PIN);
    gpio_dev_unmap(CONFIG_TP_I2C_SCL_PIN);
    rtos_delay_milliseconds(50);
    if (drv_tp_open(KLOK_TP_HOR_SIZE, KLOK_TP_VER_SIZE,
                    KLOK_TP_COORD_TRANSFORM) != BK_OK) {
        bk_printf("Klok UI: drv_tp_open failed\r\n");
    }
#endif

    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();

    lv_vendor_start();
#if KLOK_LCD_VPG_TEST
    rtos_delay_milliseconds(500);
    extern int dpu_frame_trigger(uint8_t value);
    dpu_frame_trigger(0);
    ret = mipi_dsi_panel_set_pattern(MIPI_DSI_PATTERN_BAR_VERTICAL);
    bk_printf("LCD POST-FLUSH VPG ACTIVE: DPU stopped, vertical color bars, ret=%d\r\n", ret);
#endif
    s_klok_ui_ready = true;
    s_klok_current_page = KLOK_UI_PAGE_HOME;
}

static bool klok_key_playback_active(void)
{
    return s_klok_ui_ready && klok_player_is_started();
}

static bool klok_play_page_active(void)
{
    return s_klok_ui_ready &&
           (s_klok_current_page == KLOK_UI_PAGE_PLAY ||
            klok_mv_render_is_active());
}

static volatile bool s_key_return_pending = false;

static void klok_key_return_on_ui_thread(void *user_data)
{
    (void)user_data;
    bk_printf("klok key return UI begin: started=%d pending=%d switching=%d\r\n",
              (int)klok_player_is_started(),
              (int)s_key_return_pending,
              (int)klok_player_is_switching());

    if (music_player_ui_is_active()) {
        music_player_ui_leave_to_home_async();
        s_klok_current_page = KLOK_UI_PAGE_HOME;
        s_key_return_pending = false;
        bk_printf("klok music key return scheduled\r\n");
        return;
    }

    if (!klok_play_page_active()) {
        s_key_return_pending = false;
        bk_printf("klok key return UI canceled: play page inactive\r\n");
        return;
    }

#if KLOK_VIDEO_FLEXA_DIRECT_MODE
    int prepared = -1;
    if (klok_player_is_started()) {
        prepared = klok_player_begin_output_switch(
            KLOK_PLAYER_OUTPUT_FRAME_PREVIEW);
    }
    if (prepared < 0) {
        /*
         * The engine may already be FINISHED even when the visual last frame
         * has only just appeared. A hot switch is invalid in that state, so
         * use a full stop as the reliable return fallback.
         */
        (void)klok_player_stop();
    }

    lv_vendor_disp_lock();
    klok_mv_render_leave_locked();
    navigate_to_screen(&bk_lv_tool_ui.song_list,
                       LV_SCR_LOAD_ANIM_NONE,
                       300,
                       0,
                       false,
                       init_page_song_list);
    if (prepared == 0) {
        (void)klok_player_complete_output_switch(NULL);
    }
    lv_vendor_disp_unlock();
#else
    lv_vendor_disp_lock();
    klok_mv_render_leave_locked();
    (void)video_play_engine_api_reassert_audio_format();
    navigate_to_screen(&bk_lv_tool_ui.song_list,
                       LV_SCR_LOAD_ANIM_NONE,
                       300,
                       0,
                       false,
                       init_page_song_list);
    lv_vendor_disp_unlock();
#endif
    s_klok_current_page = KLOK_UI_PAGE_LIST;
    s_key_return_pending = false;
    bk_printf("klok key return UI complete\r\n");
}

typedef enum {
    KLOK_KEY_ACTION_RETURN = 0,
    KLOK_KEY_ACTION_PAUSE_TOGGLE,
    KLOK_KEY_ACTION_AUDIO_TRACK_TOGGLE,
    KLOK_KEY_ACTION_REPLAY,
    KLOK_KEY_ACTION_NEXT,
    KLOK_KEY_ACTION_PLAYBACK_FINISHED,
    KLOK_KEY_ACTION_REBOOT,
} klok_key_action_t;

#define KLOK_KEY_CONTROL_QUEUE_DEPTH  (5U)
#define KLOK_KEY_CONTROL_STACK_SIZE   (8U * 1024U)

static beken_queue_t s_key_control_queue = NULL;
static beken_thread_t s_key_control_thread = NULL;

static void klok_playback_finished(const char *file_path, void *user_data)
{
    (void)user_data;
    bk_printf("klok playback finished callback: %s\r\n",
              file_path != NULL ? file_path : "(null)");

    if (s_key_control_queue == NULL) {
        return;
    }

    klok_key_action_t action = KLOK_KEY_ACTION_PLAYBACK_FINISHED;
    if (rtos_push_to_queue(&s_key_control_queue,
                           &action,
                           BEKEN_NO_WAIT) != BK_OK) {
        bk_printf("klok playback finished queue push failed\r\n");
    }
}

static void klok_key_action_process(klok_key_action_t action)
{
    if (action != KLOK_KEY_ACTION_PLAYBACK_FINISHED &&
        action != KLOK_KEY_ACTION_REBOOT &&
        !klok_key_playback_active() &&
        !klok_play_page_active()) {
        return;
    }

    switch (action) {
    case KLOK_KEY_ACTION_RETURN:
        /*
         * Flexa direct mode suspends LVGL's GPU/refresh path, so the LVGL task
         * is not a reliable executor for an asynchronous return callback. Follow the
         * same locking model as CLI navigation: serialize UI access with the
         * display mutex and perform the return on this control worker.
         */
        klok_key_return_on_ui_thread(NULL);
        break;
    case KLOK_KEY_ACTION_PAUSE_TOGGLE:
        if (music_player_ui_is_active()) {
            music_player_ui_key_toggle();
        } else {
            (void)klok_player_pause_toggle();
        }
        break;
    case KLOK_KEY_ACTION_AUDIO_TRACK_TOGGLE:
        if (music_player_ui_is_active()) {
            music_player_ui_key_toggle();
        } else {
            if (klok_player_is_accompany()) {
                (void)klok_player_vocal();
            } else {
                (void)klok_player_accompany();
            }
        }
        break;
    case KLOK_KEY_ACTION_REPLAY:
        if (music_player_ui_is_active()) {
            music_player_ui_key_prev();
        } else {
            (void)klok_player_replay();
        }
        break;
    case KLOK_KEY_ACTION_NEXT:
        if (music_player_ui_is_active()) {
            music_player_ui_key_next();
        } else {
            (void)klok_player_next();
        }
        break;
    case KLOK_KEY_ACTION_PLAYBACK_FINISHED:
        if (!klok_play_page_active()) {
            break;
        }
        /*
         * EOF leaves the engine in FINISHED, where decoder hot-switching is
         * no longer valid. Tear down the finished runtime, then restore LVGL
         * ownership of the display and return to the song list.
         */
        (void)klok_player_stop();
        lv_vendor_disp_lock();
        klok_mv_render_leave_locked();
        navigate_to_screen(&bk_lv_tool_ui.song_list,
                           LV_SCR_LOAD_ANIM_NONE,
                           300,
                           0,
                           false,
                           init_page_song_list);
        s_klok_current_page = KLOK_UI_PAGE_LIST;
        s_key_return_pending = false;
        lv_vendor_disp_unlock();
        bk_printf("klok playback finished: returned to song list\r\n");
        break;
    case KLOK_KEY_ACTION_REBOOT:
        /*
         * Do not reset from the GT911 polling/I2C context. Let the touch read
         * complete and perform the reset from the control worker instead.
         */
        bk_printf("bezel reboot executing\r\n");
        rtos_delay_milliseconds(50);
        bk_reboot();
        break;
    default:
        break;
    }
}

static void klok_key_control_thread(void *arg)
{
    (void)arg;

    while (true) {
        klok_key_action_t action;
        if (rtos_pop_from_queue(&s_key_control_queue,
                                &action,
                                BEKEN_WAIT_FOREVER) == BK_OK) {
            klok_key_action_process(action);
        }
    }
}

static void klok_key_schedule(klok_key_action_t action)
{
    const bool play_page_active = klok_play_page_active();
    if ((!klok_key_playback_active() && !play_page_active) ||
        s_key_control_queue == NULL) {
        return;
    }

    (void)rtos_push_to_queue(&s_key_control_queue, &action, BEKEN_NO_WAIT);
}

static void klok_key_return(void)
{
    const bool playback_active = klok_key_playback_active();
    const bool play_page_active = klok_play_page_active();
    const bool switching = klok_player_is_switching();
    if ((!playback_active && !play_page_active) ||
        s_key_control_queue == NULL ||
        s_key_return_pending ||
        switching) {
        bk_printf("klok key return ignored: active=%d play_page=%d queue=%p pending=%d switching=%d\r\n",
                  (int)playback_active,
                  (int)play_page_active,
                  s_key_control_queue,
                  (int)s_key_return_pending,
                  (int)switching);
        return;
    }
    s_key_return_pending = true;
    klok_key_action_t action = KLOK_KEY_ACTION_RETURN;
    if (rtos_push_to_queue(&s_key_control_queue,
                           &action,
                           BEKEN_NO_WAIT) != BK_OK) {
        s_key_return_pending = false;
        bk_printf("klok key return queue push failed\r\n");
    } else {
        bk_printf("klok key return queued\r\n");
    }
}

static void klok_key_pause_toggle(void)
{
    bk_printf("side key: middle/pause\r\n");
    klok_key_schedule(KLOK_KEY_ACTION_PAUSE_TOGGLE);
}

static void klok_key_audio_track_toggle(void)
{
    bk_printf("side key: up/audio\r\n");
    klok_key_schedule(KLOK_KEY_ACTION_AUDIO_TRACK_TOGGLE);
}

static void klok_key_replay(void)
{
    bk_printf("side key: left/replay\r\n");
    klok_key_schedule(KLOK_KEY_ACTION_REPLAY);
}

static void klok_key_next(void)
{
    bk_printf("side key: right/next\r\n");
    klok_key_schedule(KLOK_KEY_ACTION_NEXT);
}

static void klok_bezel_volume_down(void)
{
    int ret = klok_player_volume_down();
    bk_printf("bezel volume down: volume=%u ret=%d\r\n",
              klok_player_get_volume(), ret);
}

static void klok_bezel_volume_up(void)
{
    int ret = klok_player_volume_up();
    bk_printf("bezel volume up: volume=%u ret=%d\r\n",
              klok_player_get_volume(), ret);
}

static void klok_bezel_back(void)
{
    bk_printf("bezel back\r\n");
    if (klok_play_page_active()) {
        /*
         * Full-screen Flexa playback must switch back to frame-preview mode
         * before LVGL takes the DPU again. A direct klok_ui_back() only changes
         * screens and leaves the decoder driving the old output path.
         */
        klok_key_return();
    } else {
        (void)klok_ui_back();
    }
}

static void klok_bezel_home(void)
{
    bk_printf("bezel home/exit\r\n");
    (void)klok_ui_navigate("home");
}

static void klok_bezel_reboot(void)
{
    bk_printf("bezel reboot queued\r\n");
    if (s_key_control_queue == NULL) {
        bk_printf("bezel reboot unavailable: control queue not ready\r\n");
        return;
    }

    klok_key_action_t action = KLOK_KEY_ACTION_REBOOT;
    if (rtos_push_to_queue(&s_key_control_queue,
                           &action,
                           BEKEN_NO_WAIT) != BK_OK) {
        bk_printf("bezel reboot queue push failed\r\n");
    }
}

bool klok_tp_raw_event_filter(const tp_data_t *tp_data)
{
    /*
     * Raw Y increases in the opposite direction to the icon order visible on
     * the rotated panel. From low to high raw Y the physical keys are:
     * reboot, home, back, volume up, volume down.
     */
    static void (*const bezel_actions[])(void) = {
        klok_bezel_reboot,
        klok_bezel_home,
        klok_bezel_back,
        klok_bezel_volume_up,
        klok_bezel_volume_down,
    };
    uint8_t key_index;

    if (tp_data == NULL || tp_data->x_coordinate < 1200) {
        return false;
    }

    /*
     * Centers measured with the 1280x800 diagnostic configuration were
     * y={150,212,350,411,492}. Scale their midpoint boundaries back to the
     * production 1280x720 coordinate range.
     */
    if (tp_data->y_coordinate < 165) {
        key_index = 0;
    } else if (tp_data->y_coordinate < 255) {
        key_index = 1;
    } else if (tp_data->y_coordinate < 345) {
        key_index = 2;
    } else if (tp_data->y_coordinate < 410) {
        key_index = 3;
    } else {
        key_index = 4;
    }

    if (tp_data->event == TP_EVENT_TYPE_DOWN) {
        bk_printf("bezel touch key=%u raw=(%u,%u)\r\n",
                  key_index,
                  tp_data->x_coordinate,
                  tp_data->y_coordinate);
        bezel_actions[key_index]();
    }

    /* Never forward the out-of-display bezel strip into LVGL. */
    return true;
}

static void app_key_init(void)
{
#if CONFIG_BUTTON
    static const key_action_cfg_t key_actions[] = {
        { .pin_id = KEY_PIN_DOWN,   .short_callback = klok_key_return },
        { .pin_id = KEY_PIN_MIDDLE, .short_callback = klok_key_pause_toggle },
        { .pin_id = KEY_PIN_UP,     .short_callback = klok_key_audio_track_toggle },
        { .pin_id = KEY_PIN_LEFT,   .short_callback = klok_key_replay },
        { .pin_id = KEY_PIN_RIGHT,  .short_callback = klok_key_next },
    };

    if (rtos_init_queue(&s_key_control_queue,
                        "klok_key_ctrl_q",
                        sizeof(klok_key_action_t),
                        KLOK_KEY_CONTROL_QUEUE_DEPTH) != BK_OK) {
        s_key_control_queue = NULL;
        bk_printf("klok key control queue init failed\r\n");
        return;
    }

    if (rtos_create_thread(&s_key_control_thread,
                           BEKEN_DEFAULT_WORKER_PRIORITY,
                           "klok_key_ctrl",
                           (beken_thread_function_t)klok_key_control_thread,
                           KLOK_KEY_CONTROL_STACK_SIZE,
                           NULL) != BK_OK) {
        s_key_control_thread = NULL;
        rtos_deinit_queue(&s_key_control_queue);
        s_key_control_queue = NULL;
        bk_printf("klok key control thread init failed\r\n");
        return;
    }

    bk_key_service_init(key_actions, sizeof(key_actions) / sizeof(key_actions[0]));
#endif
}

int main(void)
{
    bk_init();
    board_3v3_e_power_enable();
    board_audio_amp_unmute();
    media_service_init();

    bk_printf("Klok main running...\r\n");

    camera_board_config_t camera_board = {0};
    display_board_config_t display_board = {0};
    gpu_board_config_t gpu_board = {0};

    display_board.mipi.enable = true;
    display_board.mipi.pin_reset = KLOK_LCD_RESET_PIN;
    /* The known-good board has two backlight controls (P7 and P28). */
    display_board.mipi.pin_backlight = -1;
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
    klok_bt_start();

    klok_player_adapter_init();
    cli_video_player_init();
    cli_klok_init();
    (void)sdcard_mtp_init();
    lvgl_ui_init();
    app_key_init();
    klok_player_set_finished_callback(klok_playback_finished, NULL);

    return 0;
}
