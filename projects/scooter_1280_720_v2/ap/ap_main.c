#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include "cli.h"
#include "components/media_types.h"
#include "sys_driver.h"
#include "media_service.h"
#include <components/bk_frame_buffer.h>
#include "tp_sensor_devices.h"

#include "components/bluetooth/bk_dm_bluetooth.h"
#include "dm_gatt.h"
#include "dm_gatts.h"
#include "wifi_boarding/wifi_boarding_demo_service.h"
#if CONFIG_BK_BLE_PROVISIONING || CONFIG_BK_NETWORK_PROVISIONING
#include "bk_network_provisioning.h"
#endif
#include "network_provisioning.h"
#include "media_msg.h"
#include "media_network_transfer.h"
#include "ntwk_sdp.h"
#include "bk_rtos_debug.h"
#include "components/bluetooth/bk_dm_gatts.h"
#include "bt_manager.h"
#include <modules/wifi.h>
#include <components/netif.h>
#include <driver/gpio.h>
#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
#include "bsc_api.h"
#endif
#if CONFIG_BUTTON
#include "key_app_service.h"
#endif

#include "media_devices.h"
#include "display_ui.h"
#include "boot_avi_play.h"
#include "boot_bg_preload.h"
#include "beken_ui.h"
#include "home_ui.h"
#include "dashcam_config.h"
#include "dashcam_storage.h"
#include "dashcam_app.h"
#include "sdkconfig.h"
#include "headset_user_config.h"
#include "dashcam_assitview.h"

#define TAG "scooter_1280_720_v2"


#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

extern void user_app_main(void);
extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern int bk_cli_init(void);

#define SYS_ANA_REG_BASE    (0x44010000)
#define LDO_ANA_REG         (0x69)
#define SYS_M55_BASE_ADDR   (0x48000000)

static beken_thread_t s_ap_bt_startup_task_handle = NULL;


/* ==================== LDO / Power ==================== */

static void bk_ldo_enable(void)
{
    uint32_t reg = REG_READ(SYS_ANA_REG_BASE + LDO_ANA_REG * 4);
    reg |= (0xF << 28) | (0x2 << 23) | (0x7 << 19) | (0x7 << 15);
    reg &= ~(0xF << 11);
    reg |= (0x8 << 11);
    REG_WRITE(SYS_ANA_REG_BASE + LDO_ANA_REG * 4, reg);

    reg = REG_READ(SYS_M55_BASE_ADDR + 0xA * 4);
    reg &= ~(1 << 2);  // usb hs clock
    reg &= ~(1 << 5);  // qspi0 clock
    reg &= ~(1 << 6);  // qspi1 clock
    reg &= ~(1 << 7);  // sdio0 clock
    reg &= ~(1 << 8);  // sdio1 clock
    reg &= ~(1 << 9);  // isp clock
    reg &= ~(1 << 10); // gpu clock
    reg &= ~(1 << 11); // h264e clock
    reg &= ~(1 << 12); // csi clock
    reg &= ~(1 << 13); // dsi clock
    reg &= ~(1 << 14); // dpu clock
    reg &= ~(1 << 15); // usb fs clock
    reg &= ~(1 << 22); // npu clock
    reg &= ~(0x3F << 26);
    REG_WRITE(SYS_M55_BASE_ADDR + 0xA * 4, reg);
}

/* ==================== Stub functions for unimplemented features ==================== */

__attribute__((weak)) int bk_rand(void) { return 0; }
// __attribute__((weak)) int manual_cal_get_macaddr_from_flash(void *a, void *b) { return 0; }
// __attribute__((weak)) int manual_cal_write_macaddr_to_flash(void *a, void *b) { return 0; }
// __attribute__((weak)) void bk_ble_gap_get_local_name_private(void *a, void *b) {}
// __attribute__((weak)) ble_err_t bk_ble_gatts_char_property_operation(bk_gatts_char_property_bit_mask_op_t op, uint16_t attr_handle, uint16_t *io) { return 0; }
__attribute__((weak)) void *f_ota_fun_ptr = (void *)0;

/* ==================== BSC Init (BK3515N) ==================== */

#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
static bk_err_t app_bsc_init(void)
{
    bsc_config_t bsc_config =
    {
        .uart_id = CONFIG_BLUETOOTH_BSC_UART_ID,
        .reset_pin = CONFIG_BLUETOOTH_BSC_RESET_PIN,
        .init_baud = CONFIG_BLUETOOTH_BSC_INIT_BAUD,
        .nego_baud = CONFIG_BLUETOOTH_BSC_NEGO_BAUD,
        .uart_config =
        {
            .baud_rate = bsc_config.init_baud,
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

    return bk_cpn_bsc_init(&bsc_config);
}
#endif

void bt_sync_base_mac_from_cp(void)
{
    extern bk_err_t bk_ap_get_mac(uint8_t *mac, mac_type_t type);
    extern bk_err_t bk_set_base_mac(const uint8_t *mac);

    /*
     * Force mac_init() to run first so s_mac_inited becomes true.
     * Otherwise bk_set_base_mac() below would be overwritten by a
     * later bk_get_mac() call that triggers mac_init() lazily.
     */
    uint8_t dummy[6], base_mac[6] = {0};
    bk_get_mac(dummy, MAC_TYPE_BASE);

    if (bk_ap_get_mac(base_mac, MAC_TYPE_BASE) == BK_OK) {
        bk_set_base_mac(base_mac);
        LOGI("synced base mac from CP: %02x:%02x:%02x:%02x:%02x:%02x\n",
             base_mac[0], base_mac[1], base_mac[2],
             base_mac[3], base_mac[4], base_mac[5]);
    } else {
        LOGW("failed to sync base mac from CP\n");
    }
}

/* ==================== BLE Provisioning ==================== */
static void ap_bt_app_init(void)
{
#if CONFIG_BT
    uint8_t bt_mac[6] = {0};
	char local_name[30] = {0};
	bk_err_t err = bk_bluetooth_get_address(bt_mac);
	if (err == BK_OK)
	{
		snprintf(local_name,
				 sizeof(local_name),
				 "%s_%02x%02x%02x",
				 LOCAL_NAME,
				 bt_mac[2],
				 bt_mac[1],
				 bt_mac[0]);
	}
	else
	{
		snprintf(local_name, sizeof(local_name), "%s", LOCAL_NAME);
	}

	bt_manager_cfg_t bt_manager_cfg =
	{
		.local_name = local_name,
		.device_class = COD_SOUNDBAR,
		.page_scan_interval = PAGE_SCAN_INTV,
		.page_scan_window = PAGE_SCAN_WIN,
		.page_timeout = CONFIG_PAGE_TIMEOUT,
		.reconnect_interval_ms = CONFIG_RECONN_INTERVAL,
		.max_reconnect_count = CONFIG_MAX_RECONN_COUNT,
		.io_capability = BK_BT_IO_CAP_NONE,
	};
    bt_manager_init(&bt_manager_cfg);

#if CONFIG_A2DP_SINK_DEMO
    int a2dp_sink_demo_init(uint8_t aac_supported, uint8_t auto_accept_conn);
    a2dp_sink_demo_init(0, 1);
#endif

#if CONFIG_HFP_HF_DEMO
    extern int hfp_hf_demo_init(uint8_t msbc_supported);
    hfp_hf_demo_init(0);
#endif

#if CONFIG_PBAP_CONTACTS
    extern void pbap_contacts_init(void);
    pbap_contacts_init();
#endif

    extern int cli_headset_demo_init(void);
    cli_headset_demo_init();
#endif

#if CONFIG_BLE
#if !CONFIG_BK_BLE_PROVISIONING
    cli_gatt_param_t param = {.rpa = 0, .p_rpa = &param.rpa, .pa = 0, .p_pa = &param.pa};

    dm_gatt_main(&param);
    dm_gatts_main(&param);
    wifi_boarding_demo_service_main();
#endif

    extern int cli_ble_gatt_demo_init(void);
    cli_ble_gatt_demo_init();
    extern int cli_ble_wboarding_demo_init(void);
    cli_ble_wboarding_demo_init();
#endif
}

static void ap_bt_startup_task(void *arg)
{
    int32_t ret = 0;
    LOGI("%s start\n", __func__);

    set_ap_startup_index(AP_ENTER_APP_BLE_INIT);

    extern void bt_sync_base_mac_from_cp(void);
    bt_sync_base_mac_from_cp();

#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
    app_bsc_init();
#endif

    ret = bk_bluetooth_init();
    if (ret)
    {
        LOGE("%s bk_bluetooth_init err %d\n", __func__, ret);
        goto end;
    }

    media_msg_init();

#if CONFIG_MEDIA_RECEIVE_DEMO
    /* Start SDP broadcast (UDP 52110) via SDK bk_network_transfer so app "搜索" can discover board IP */
    ntwk_sdp_start("media-tcp", 7100, 7150, 7140);
#endif

    ap_bt_app_init();

#if CONFIG_BK_BLE_PROVISIONING
    bk_sl_np_init(0);
#else
    bk_sl_np_init(1);
#endif

end:;
    LOGI("%s end\n", __func__);
    s_ap_bt_startup_task_handle = NULL;
    rtos_delete_thread(NULL);
}

/* ==================== CLI ==================== */

void cli_widgets_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    LOGD("%s %d\r\n", __func__, __LINE__);

    if ((argc >= 2) && (os_strcmp(argv[1], "np_erase") == 0))
    {
#if CONFIG_BK_BLE_PROVISIONING
        LOGI("erase saved network provisioning info\n");
        erase_network_auto_reconnect_info();

        if ((argc >= 3) && (os_strcmp(argv[2], "reboot") == 0))
        {
            LOGI("reboot after erase provisioning info\n");
            bk_reboot();
        }
#else
        LOGW("np_erase unsupported, enable CONFIG_BK_BLE_PROVISIONING\n");
#endif
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "np_start_advertise") == 0))
    {
#if CONFIG_BK_BLE_PROVISIONING
        LOGI("start advertise\n");
        extern int wifi_boarding_adv_stop(void);
        wifi_boarding_adv_stop();
        extern int wifi_boarding_adv_start(void);
        wifi_boarding_adv_start();
#endif
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam") == 0))
    {
        LOGI("navigate to dashcam page\n");
        beken_ui_nav_to_dashcam();
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "phone_book") == 0))
    {
        LOGI("navigate to phone_book page\n");
        beken_ui_nav_to_phone_book();
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "pet_toggle") == 0))
    {
        LOGI("simulate USER_PET_TOGGLE\n");
        home_menu_key_short_press();
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "pet_double") == 0))
    {
        LOGI("simulate USER_PET_DOUBLE\n");
        home_menu_key_double_press();
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "pet_enter") == 0))
    {
        LOGI("simulate USER_PET_ENTER\n");
        home_menu_key_long_press();
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_count") == 0))
    {
        uint32_t count = 0;
        uint32_t free_mb = 0;
        bk_err_t count_ret = dashcam_storage_count(&count);
        bk_err_t free_ret = dashcam_storage_free_mb(&free_mb);

        LOGI("dashcam_count: ret=%d dir=%s count=%u limit=%u release=%u allowed=%u free_ret=%d free_mb=%u\n",
             count_ret,
             DASHCAM_STORAGE_DIR,
             (unsigned)count,
             (unsigned)DASHCAM_DEV_MAX_FILES,
             (unsigned)DASHCAM_RELEASE_BUILD,
             (unsigned)((count_ret == BK_OK) &&
                        (DASHCAM_RELEASE_BUILD || count < DASHCAM_DEV_MAX_FILES)),
             free_ret,
             (unsigned)free_mb);
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_trim") == 0))
    {
        uint32_t count = 0;
        uint32_t target = (DASHCAM_DEV_MAX_FILES > 0) ? (DASHCAM_DEV_MAX_FILES - 1) : 0;
        uint32_t deleted = 0;

        if (argc >= 3)
        {
            target = (uint32_t)os_strtoul(argv[2], NULL, 10);
        }

        if (dashcam_storage_count(&count) != BK_OK)
        {
            LOGE("dashcam_trim: count failed\n");
            return;
        }

        while (count > target)
        {
            if (dashcam_storage_delete_oldest() != BK_OK)
            {
                LOGE("dashcam_trim: delete oldest failed at count=%u target=%u\n",
                     (unsigned)count, (unsigned)target);
                break;
            }
            deleted++;
            if (dashcam_storage_count(&count) != BK_OK)
            {
                LOGE("dashcam_trim: recount failed\n");
                break;
            }
        }

        LOGI("dashcam_trim: target=%u deleted=%u count=%u limit=%u allowed=%u\n",
             (unsigned)target,
             (unsigned)deleted,
             (unsigned)count,
             (unsigned)DASHCAM_DEV_MAX_FILES,
             (unsigned)(DASHCAM_RELEASE_BUILD || count < DASHCAM_DEV_MAX_FILES));
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_rec_start") == 0))
    {
        bk_err_t ret = dashcam_app_record_start();
        LOGI("dashcam_rec_start: ret=%d rec=%d\n", ret, (int)dashcam_app_rec_state());
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_rec_stop") == 0))
    {
        dashcam_app_record_stop();
        LOGI("dashcam_rec_stop: rec=%d\n", (int)dashcam_app_rec_state());
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_clean_idx") == 0))
    {
        uint32_t removed = 0;
        uint32_t failed = 0;
        bk_err_t ret = dashcam_storage_cleanup_orphan_idx(&removed, &failed);

        LOGI("dashcam_clean_idx: ret=%d removed=%u failed=%u\n",
             ret, (unsigned)removed, (unsigned)failed);
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_turn_left") == 0))
    {
        LOGI("turn dashcam left\n");
        dashcam_assitview_start();
    }
    else if ((argc >= 2) && (os_strcmp(argv[1], "dashcam_turn_right") == 0))
    {
        LOGI("turn dashcam right\n");
        // dashcam_video_turn_right();
    }
    else if((argc >= 2) && (os_strcmp(argv[1], "dashcam_turn_off") == 0))
    {
        LOGI("turn dashcam stop\n");
        dashcam_assitview_stop();
    }
    else
    {
        LOGI("usage: dashboard np_erase [reboot] | np_start_advertise | dashcam | pet_toggle | pet_double | pet_enter | dashcam_count | dashcam_trim [target] | dashcam_rec_start | dashcam_rec_stop | dashcam_clean_idx\n");
    }
}

/*
 * Register "dashboard" through the .cli_cmdtabl linker section instead of a
 * one-shot cli_register_commands() call at boot.
 *
 * bk_cli_init() rebuilds pCli->commands from built_ins + this section every
 * time the CLI core (re)initialises, and the CLI task frees pCli on exit. A
 * single early cli_register_commands() therefore gets dropped (observed: the
 * device reports "cmd NOT found: dashboard"). Anything placed in .cli_cmdtabl is
 * picked up by cli_commands_init() unconditionally, so the command survives.
 */
__attribute__((used, section(".cli_cmdtabl")))
static const struct cli_command s_widgets_cmd_export =
{
    "dashboard", "dashboard <command>", cli_widgets_cmd
};

/* ==================== Modular Init Functions ==================== */

static void app_board_init(void)
{
    LOGI("enabling LDO\n");
    bk_ldo_enable();

    LOGI("initializing frame buffer\n");
    bk_frame_buffer_init();

#if CONFIG_TP
    tp_sensor_devices_init();
#endif
}

static void app_display_init(void)
{
    if (display_ui_register_init_callback(beken_ui_init) != BK_OK)
    {
        LOGE("register UI init callback failed\n");
        return;
    }

#if defined(CONFIG_SCOOTER_LVGL_HOR_RES) && defined(CONFIG_SCOOTER_UI_CANVAS_WIDTH)
    LOGI("profile 1280_720: LVGL %dx%d rot=%d canvas %dx%d\n",
         CONFIG_SCOOTER_LVGL_HOR_RES, CONFIG_SCOOTER_LVGL_VER_RES,
#if defined(CONFIG_SCOOTER_UI_ROTATION)
         CONFIG_SCOOTER_UI_ROTATION,
#else
         -1,
#endif
         CONFIG_SCOOTER_UI_CANVAS_WIDTH, CONFIG_SCOOTER_UI_CANVAS_HEIGHT);
#endif

    LOGI("cast recovery: dpu_layer_config + display open after cast\n");

    if (display_ui_init_display_hw() != BK_OK) {
        LOGE("display hw init failed\n");
    } else {
        /* Load the home background into PSRAM on a worker thread while the boot
         * animation plays, so the UI can switch in instantly afterwards. */
        boot_bg_preload_start();
        rtos_delay_milliseconds(10);

        boot_avi_play();

        if (display_ui_start_lvgl() != BK_OK)
            LOGE("lvgl start failed\n");

        boot_bg_preload_finish();
    }

    display_ui_register_cast_hooks_once();
    av_server_devices_init();
}

static void app_bt_init(void)
{
    bk_err_t ret;

    LOGI("creating ap_bt_startup_task\n");
    ret = rtos_create_thread(&s_ap_bt_startup_task_handle,
                             4,
                             "ap_bt_startup",
                             (void *)ap_bt_startup_task,
                             1024 * 4,
                             (void *)0);
    if (ret != BK_OK) {
        LOGE("start ap_bt_startup_task err\n");
    }
}

static void app_key_config_network(void)
{
    LOGI("USER_CONFIG_NETWORK\r\n");
#if CONFIG_BK_NETWORK_PROVISIONING
    erase_network_auto_reconnect_info();
    bk_reboot();
#endif
}

static void app_key_erase_info(void)
{
    LOGI("USER_ERASE_INFO\r\n");
#if CONFIG_BK_NETWORK_PROVISIONING
    erase_network_auto_reconnect_info();
#endif
}

static void app_key_init(void)
{
#if CONFIG_BUTTON
    static const key_action_cfg_t s_key_actions[] =
    {
        { .pin_id = KEY_PIN_UP,     .short_callback = phone_key_answer,             .double_callback = phone_key_hangup, .long_callback = NULL },
        { .pin_id = KEY_PIN_LEFT,   .short_callback = app_key_config_network,        .double_callback = NULL,             .long_callback = NULL },
        { .pin_id = KEY_PIN_RIGHT,  .short_callback = app_key_erase_info,            .double_callback = NULL,             .long_callback = NULL },
        { .pin_id = KEY_PIN_MIDDLE, .short_callback = home_menu_key_short_press,    .double_callback = home_menu_key_double_press, .long_callback = home_menu_key_long_press },
    };
    bk_key_service_init(s_key_actions, sizeof(s_key_actions) / sizeof(s_key_actions[0]));
#endif
}

/* ==================== Main ==================== */

int main(void)
{
    bk_init();

    LOGI("ap is initializing\n");

    media_service_init();

    app_board_init();
    app_display_init();
    app_bt_init();
    app_key_init();

    return 0;
}
