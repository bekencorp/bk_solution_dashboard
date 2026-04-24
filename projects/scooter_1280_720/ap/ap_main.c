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
#include "gatt/dm_gatt.h"
#include "gatt/dm_gatts.h"
#include "wifi_boarding/wifi_boarding_demo_service.h"
#if CONFIG_BK_BLE_PROVISIONING
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
#include "sdkconfig.h"

#define TAG "scooter_1280_720"

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

#if !CONFIG_BLUETOOTH_AUTO_ENABLE
static beken_thread_t s_ap_bt_startup_task_handle = NULL;
#endif

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

/* ==================== BLE Provisioning ==================== */

static void ap_bt_app_init(void)
{
#if CONFIG_BT
    bt_manager_init();

#if CONFIG_A2DP_SINK_DEMO
    extern int a2dp_sink_demo_init(uint8_t aac_supported);
    a2dp_sink_demo_init(0);
#endif

#if CONFIG_HFP_HF_DEMO
    extern int hfp_hf_demo_init(uint8_t msbc_supported);
    hfp_hf_demo_init(0);
#endif

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
}

static const struct cli_command s_widgets_commands[] =
{
    {"widgets", "widgets", cli_widgets_cmd},
};

#define CMDS_COUNT  (sizeof(s_widgets_commands) / sizeof(struct cli_command))

bk_err_t cli_widgets_init(void)
{
    return cli_register_commands(s_widgets_commands, CMDS_COUNT);
}

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
        rtos_delay_milliseconds(10);
        boot_avi_play();
        if (display_ui_start_lvgl() != BK_OK)
            LOGE("lvgl start failed\n");
    }

    display_ui_register_cast_hooks_once();
    av_server_devices_init();
}

static void app_bt_init(void)
{
#if CONFIG_BLUETOOTH_AUTO_ENABLE
    ap_bt_app_init();
#else
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
#endif
}

static void app_cli_init(void)
{
    cli_widgets_init();

#if CONFIG_BUTTON
    bk_key_service_init();
#endif
}

/* ==================== Main ==================== */

int main(void)
{
#if CONFIG_BLUETOOTH_AUTO_ENABLE && CONFIG_BLUETOOTH_MULTI_CONTROLLER
    app_bsc_init();
#endif

    bk_init();

    LOGI("ap is initializing\n");

    media_service_init();

    app_board_init();
    app_display_init();
    app_bt_init();
    app_cli_init();

    return 0;
}
