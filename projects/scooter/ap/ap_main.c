#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include "components/bluetooth/bk_dm_bluetooth.h"
#include <components/ate.h>
#include "cli.h"
#include "media_service.h"
#include "media_devices.h"
#include "bt_manager.h"
#include "gatt/dm_gatt.h"
#include "gatt/dm_gatts.h"
#include "hogpd/hogpd_demo.h"
#include "wifi_boarding//wifi_boarding_demo_service.h"
#include "modules/wifi.h"
#include "components/netif.h"
#if CONFIG_BK_BLE_PROVISIONING
#include "bk_network_provisioning.h"
#endif

#include "components/bk_display.h"
#include "lcd_panel_devices.h"
#include "gpio_driver.h"
#include <driver/pwr_clk.h>
#include "gpio_driver.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "bk_rtos_debug.h"
#include "media_manager.h"

#include "media_msg.h"
#include "media_network_transfer.h"


#if CONFIG_BK3515_OTA
    #include "download.h"
#endif

#if CONFIG_BUTTON
    #include "key_app_service.h"
#endif

#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
    #include "bsc_api.h"
#endif

#define AUTO_ENABLE_BLUETOOTH_DEMO 1
#define CLI_CMD_RSP_SUCCEED               "CMDRSP:OK\r\n"
#define CLI_CMD_RSP_ERROR                 "CMDRSP:ERROR\r\n"

#define TAG "ap_main"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#if 1//CONFIG_SYS_CPU0

typedef struct
{
    uint8_t enable : 1;
    uint32_t seqence;
    beken_semaphore_t sem;
    beken_thread_t thread;
} test_thread_t;

test_thread_t *s_app_test = NULL;
beken_thread_t media_demo_thread = NULL;

#if !CONFIG_BLUETOOTH_AUTO_ENABLE
    static beken_thread_t s_ap_bt_startup_task_handle = NULL;
#endif

#define DEFAULT_WIFI_SSID "bicycle"
#define DEFAULT_WIFI_KEY "12345678"
#define SSID_MAX_LEN 32
#define KEY_MAX_LEN  64
static char wifi_ssid[SSID_MAX_LEN] = DEFAULT_WIFI_SSID;
static char wifi_key[KEY_MAX_LEN] = DEFAULT_WIFI_KEY;
#define WLAN_DEFAULT_IP         "192.168.188.1"
#define WLAN_DEFAULT_GW         "192.168.188.1"
#define WLAN_DEFAULT_MASK       "255.255.255.0"
static char p2p_ssid[SSID_MAX_LEN] = "dashboard_p2p";

enum
{
    BK_SOFT_AP,  /**< Act as an access point, and other station can connect, 4 stations Max*/
    BK_STATION,   /**< Act as a station which can connect to an access point*/
    BK_P2P
} wlan_interface_type_t;

void media_receive_sta_demo_init(void)
{
    LOGD("+++start connect\n");
    wifi_sta_config_t sta_config = {0};
    os_strcpy(sta_config.ssid, wifi_ssid);
    os_strcpy(sta_config.password, wifi_key);
    bk_wifi_sta_set_config(&sta_config);
    bk_wifi_sta_start();
    LOGD("---connect ssid:%s, key:%s\n", wifi_ssid, wifi_key);

#if CONFIG_MEDIA_DEMO_MODE_TCP
    media_bk_network_transfer_init("tcp_service", NULL);
#else
    media_bk_network_transfer_init("udp_service", NULL);
#endif
}

void media_receive_softap_demo_init(void)
{
    LOGD("---create ssid:%s, key:%s\n", wifi_ssid, wifi_key);
    wifi_ap_config_t ap_config = WIFI_DEFAULT_AP_CONFIG();
    netif_ip4_config_t ip4_config = {0};
    os_strcpy(ip4_config.ip, WLAN_DEFAULT_IP);
    os_strcpy(ip4_config.mask, WLAN_DEFAULT_MASK);
    os_strcpy(ip4_config.gateway, WLAN_DEFAULT_GW);
    os_strcpy(ip4_config.dns, WLAN_DEFAULT_GW);
    os_strcpy(ap_config.ssid, wifi_ssid);
    os_strcpy(ap_config.password, wifi_key);
    ap_config.channel = 13;
    bk_netif_set_ip4_config(NETIF_IF_AP, &ip4_config);
    bk_wifi_ap_set_config(&ap_config);
    bk_wifi_ap_start();
    LOGD("---connected\n");

#if CONFIG_MEDIA_DEMO_MODE_TCP
    media_bk_network_transfer_init("tcp_service", NULL);
#else
    media_bk_network_transfer_init("udp_service", NULL);
#endif
}

#if CONFIG_P2P
void media_receive_p2p_demo_init(void)
{
    LOGD("+++start p2p connect\n");
    BK_LOG_ON_ERR(bk_wifi_p2p_enable(p2p_ssid));
    BK_LOG_ON_ERR(bk_wifi_p2p_find());

#if CONFIG_MEDIA_DEMO_MODE_TCP
    media_bk_network_transfer_init("tcp_service", NULL);
#else
    media_bk_network_transfer_init("udp_service", NULL);
#endif
}
#endif

void media_receive_demo_entry(beken_thread_arg_t param)
{
    if ((uint32_t)param == BK_SOFT_AP)
    {
        media_receive_softap_demo_init();
    }
    else if ((uint32_t)param == BK_STATION)
    {
        media_receive_sta_demo_init();
    }
#if CONFIG_P2P
    else if ((uint32_t)param == BK_P2P)
    {
        media_receive_p2p_demo_init();
    }
#endif

    media_demo_thread = NULL;
    rtos_delete_thread(NULL);
}


void cli_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    LOGD("%s\r\n", __func__);

    avdk_err_t ret = AVDK_ERR_UNKNOWN;
    char *msg = NULL;

    if (os_strcmp(argv[1], "sta") == 0)
    {
        if (argc >= 3)
        {
            int wifi_ssid_len = os_strlen(argv[2]);

            if (wifi_ssid_len > 0)
            {
                if (wifi_ssid_len >= SSID_MAX_LEN)
                {
                    wifi_ssid_len = SSID_MAX_LEN - 1;
                }

                os_memcpy(wifi_ssid, argv[2], wifi_ssid_len);
                wifi_ssid[wifi_ssid_len] = '\0';
            }
        }
        else
        {
            os_memcpy(wifi_ssid, DEFAULT_WIFI_SSID, os_strlen(DEFAULT_WIFI_SSID));
            wifi_ssid[os_strlen(DEFAULT_WIFI_SSID)] = '\0';
        }

        if (argc >= 4)
        {
            int wifi_key_len = os_strlen(argv[3]);

            if (wifi_key_len > 0)
            {
                if (wifi_key_len >= KEY_MAX_LEN)
                {
                    wifi_key_len = KEY_MAX_LEN - 1;
                }

                os_memcpy(wifi_key, argv[3], wifi_key_len);
                wifi_key[wifi_key_len] = '\0';
            }
        }
        else
        {
            os_memcpy(wifi_key, DEFAULT_WIFI_KEY, os_strlen(DEFAULT_WIFI_KEY));
            wifi_key[os_strlen(DEFAULT_WIFI_KEY)] = '\0';
        }

        LOGI("%s, %d\n", __func__, __LINE__);

        if (media_demo_thread == NULL)
        {
            ret = rtos_create_thread(&media_demo_thread,
                                     BEKEN_APPLICATION_PRIORITY,
                                     "media_sta",
                                     (beken_thread_function_t)media_receive_demo_entry,
                                     1024 * 2,
                                     (beken_thread_arg_t)BK_STATION);
        }
        else
        {
            LOGD("already open wifi\n");
        }
    }
    else if (os_strcmp(argv[1], "ap") == 0)
    {
        if (argc >= 3)
        {
            int wifi_ssid_len = os_strlen(argv[2]);

            if (wifi_ssid_len > 0)
            {
                if (wifi_ssid_len >= SSID_MAX_LEN)
                {
                    wifi_ssid_len = SSID_MAX_LEN - 1;
                }

                os_memcpy(wifi_ssid, argv[2], wifi_ssid_len);
                wifi_ssid[wifi_ssid_len] = '\0';
            }
        }
        else
        {
            os_memcpy(wifi_ssid, DEFAULT_WIFI_SSID, os_strlen(DEFAULT_WIFI_SSID));
            wifi_ssid[os_strlen(DEFAULT_WIFI_SSID)] = '\0';
        }

        if (argc >= 4)
        {
            int wifi_key_len = os_strlen(argv[3]);

            if (wifi_key_len > 0)
            {
                if (wifi_key_len >= KEY_MAX_LEN)
                {
                    wifi_key_len = KEY_MAX_LEN - 1;
                }

                os_memcpy(wifi_key, argv[3], wifi_key_len);
                wifi_key[wifi_key_len] = '\0';
            }
        }
        else
        {
            os_memcpy(wifi_key, DEFAULT_WIFI_KEY, os_strlen(DEFAULT_WIFI_KEY));
            wifi_key[os_strlen(DEFAULT_WIFI_KEY)] = '\0';
        }

        if (media_demo_thread == NULL)
        {
            ret = rtos_create_thread(&media_demo_thread,
                                     BEKEN_APPLICATION_PRIORITY,
                                     "media_ap",
                                     (beken_thread_function_t)media_receive_demo_entry,
                                     1024 * 2,
                                     (beken_thread_arg_t)BK_SOFT_AP);
        }
        else
        {
            LOGD("already open wifi\n");
        }
    }
#if CONFIG_P2P
    else if (os_strcmp(argv[1], "p2p") == 0)
    {
        if (argc >= 3)
        {
            int p2p_ssid_len = os_strlen(argv[2]);
            if (p2p_ssid_len > 0)
            {
                if (p2p_ssid_len >= SSID_MAX_LEN)
                {
                    p2p_ssid_len = SSID_MAX_LEN - 1;
                }
                os_memcpy(p2p_ssid, argv[2], p2p_ssid_len);
                p2p_ssid[p2p_ssid_len] = '\0';
            }
        }
        else
        {
            os_memcpy(p2p_ssid, "dashboard_p2p", os_strlen("dashboard_p2p"));
            p2p_ssid[os_strlen("dashboard_p2p")] = '\0';
        }

        if (media_demo_thread == NULL)
        {
            ret = rtos_create_thread(&media_demo_thread,
                                     BEKEN_APPLICATION_PRIORITY,
                                     "media_p2p",
                                     (beken_thread_function_t)media_receive_demo_entry,
                                     1024 * 2,
                                     (beken_thread_arg_t)BK_P2P);
        }
        else
        {
            LOGD("already open wifi\n");
        }
    }
#endif
    else if (os_strcmp(argv[1], "uvc") == 0)
    {
        if (argc < 3)
        {
            LOGD("param error, %d\n", __LINE__);
            goto out;
        }

        if (os_strcmp(argv[2], "open") == 0)
        {
            ret = uvc_camera_open(pcWriteBuffer, xWriteBufferLen, argc, argv);
        }
        else if (os_strcmp(argv[2], "close") == 0)
        {
            ret = uvc_camera_close(pcWriteBuffer, xWriteBufferLen, argc, argv);
        }
        else
        {
            LOGD("param error, %d\n", __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "h264e") == 0)
    {
        if (argc < 3)
        {
            LOGD("param error, %d\n", __LINE__);
            goto out;
        }

        if (os_strcmp(argv[2], "open") == 0)
        {
            ret = h264_encode_open(pcWriteBuffer, xWriteBufferLen, argc, argv);
        }
        else if (os_strcmp(argv[2], "close") == 0)
        {
            ret = h264_encode_close(pcWriteBuffer, xWriteBufferLen, argc, argv);
        }
        else
        {
            LOGD("param error, %d\n", __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "storage") == 0)
    {
        if (os_strcmp(argv[2], "open") == 0)
        {
            if (argc < 3)
            {
                LOGD("param error, %d\n", __LINE__);
                goto out;
            }

#ifdef CONFIG_VFS
            ret = h264e_storage_open(argv[3], 0);
#endif
        }
        else if (os_strcmp(argv[2], "close") == 0)
        {
#ifdef CONFIG_VFS
            ret = h264e_storage_close();
#endif
        }
        else
        {
            LOGD("param error, %d\n", __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "switch") == 0)
    {
        if (argc < 3)
        {
            LOGD("param error, %d %d\n", __LINE__, argc);
            goto out;
        }

        if (os_strcmp(argv[2], "lvgl") == 0)
        {
            av_server_jpeg_decode_manager_suspend();
            ret = lvgl_app_resume_display();

        }
        else if (os_strcmp(argv[2], "wifi") == 0)
        {
            ret = lvgl_app_suspend_display();
            av_server_jpeg_decode_manager_resume();
        }
        else
        {
            LOGD("param error, %d\n", __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "video_server") == 0)
    {
        if (os_strcmp(argv[2], "open") == 0)
        {
            //ret = video_data_process_open(480, 272, IMAGE_MJPEG);
        }
        else if (os_strcmp(argv[2], "close") == 0)
        {
            //ret = video_data_process_close();
        }
        else
        {
            LOGD("param error, %d\n", __LINE__);
        }
    }
    else
    {
        LOGD("param error, %d\n", __LINE__);
    }

out:

    if (ret != AVDK_ERR_OK)
    {
        msg = CLI_CMD_RSP_ERROR;
    }
    else
    {
        msg = CLI_CMD_RSP_SUCCEED;
    }

    LOGI("%s ---complete, %d\n", __func__, __LINE__);

    os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
}

static const struct cli_command s_test_cmd_commands[] =
{
    {"test", "open", cli_test_cmd},
};


#define CMDS_COUNT  (sizeof(s_test_cmd_commands) / sizeof(struct cli_command))

int cli_test_cmd_init(void)
{
    return cli_register_commands(s_test_cmd_commands, CMDS_COUNT);
}

#endif

static int32_t app_bsc_init(void)
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

            .enable_sw_flow_ctrl = 1,
            .rts_gpio = CONFIG_BLUETOOTH_BSC_RTS_PIN,
            .cts_gpio = CONFIG_BLUETOOTH_BSC_CTS_PIN,
        },
    };

    return bk_cpn_bsc_init(&bsc_config);
}

static void ap_bt_app_init(void)
{
    bt_manager_init();

#if AUTO_ENABLE_BLUETOOTH_DEMO
#if CONFIG_A2DP_SINK_DEMO
    extern int a2dp_sink_demo_init(uint8_t aac_supported);
    a2dp_sink_demo_init(0);
#endif

#if CONFIG_HFP_HF_DEMO
    extern int hfp_hf_demo_init(uint8_t msbc_supported);
    hfp_hf_demo_init(0);
#endif

#if 0//CONFIG_BLE
    cli_gatt_param_t param = {.rpa = 0, .p_rpa = &param.rpa, .pa = 0, .p_pa = &param.pa};

    dm_gatt_main(&param);
    dm_gatts_main(&param);
    hogpd_demo_init();
    wifi_boarding_demo_service_main();
#endif
#endif

#if CONFIG_BT
    extern int cli_headset_demo_init(void);
    cli_headset_demo_init();
#endif

#if CONFIG_BLE
    extern int cli_ble_gatt_demo_init(void);
    cli_ble_gatt_demo_init();
    extern int cli_ble_hogpd_demo_init(void);
    cli_ble_hogpd_demo_init();
    extern int cli_ble_wboarding_demo_init(void);
    cli_ble_wboarding_demo_init();
#endif
}

static void ap_bt_startup_task(void *arg)
{
    int32_t ret = 0;
    LOGI("%s start\n", __func__);
#if CONFIG_BK3515_OTA
    bk3515_ota_init();
    update_bk3515_firmware(CONFIG_BLUETOOTH_BSC_UART_ID);
#endif
    set_ap_startup_index(AP_ENTER_APP_BLE_INIT);

#if CONFIG_BLUETOOTH_MULTI_CONTROLLER
    app_bsc_init();
#endif

    ret = bk_bluetooth_init();

    if (ret)
    {
        LOGE("%s bk_bluetooth_init err %d\n", __func__, ret);
        goto end;
    }

    ap_bt_app_init();

    media_msg_init();

#if CONFIG_BK_BLE_PROVISIONING
extern void demo_network_provisioning_status_cb(bk_network_provisioning_status_t status, void *user_data);
extern void ble_msg_handle_demo_cb(ble_prov_msg_t *msg);
extern int cli_network_provisioning_init(void);
    //for user to receive network provisioning status change event
    bk_register_network_provisioning_status_cb(demo_network_provisioning_status_cb);
    //if default provisioning type is ble, then set msg handle cb
    bk_ble_provisioning_set_msg_handle_cb(ble_msg_handle_demo_cb);
    bk_network_provisioning_init(BK_NETWORK_PROVISIONING_TYPE_BLE);
    cli_network_provisioning_init();
#endif

end:;
    LOGI("%s end\n", __func__);
    s_ap_bt_startup_task_handle = NULL;
    rtos_delete_thread(NULL);
}

int main(void)
{
    int32_t ret = 0;

#if CONFIG_BLUETOOTH_AUTO_ENABLE && CONFIG_BLUETOOTH_MULTI_CONTROLLER
    app_bsc_init();
#endif

    bk_init();

    bk_wifi_capa_config(WIFI_CAPA_ID_RX_AMPDU_NUM, 2);

    media_service_init();

    bk_pm_module_vote_psram_ctrl(PM_POWER_PSRAM_MODULE_NAME_VIDP_JPEG_DE, PM_POWER_MODULE_STATE_ON);
    bk_pm_module_vote_cpu_freq(PM_DEV_ID_DECODER, PM_CPU_FRQ_480M);

#if CONFIG_BLUETOOTH_AUTO_ENABLE
    (void)ret;
#if CONFIG_BK3515_OTA
    bk3515_ota_init();
    update_bk3515_firmware(CONFIG_BLUETOOTH_BSC_UART_ID);
#endif
    ap_bt_app_init();
#else

    LOGI("%s before call rtos_create_thread ap_bt_startup_task\n", __func__);
    ret = rtos_create_thread(&s_ap_bt_startup_task_handle,
                             4,
                             "ap_bt_startup_task",
                             (void *)ap_bt_startup_task,
                             1024 * 3,
                             (void *)0);

    if (ret != 0)
    {
        LOGE("%s start ap_bt_startup_task err\n", __func__);
    }

    LOGI("%s after call rtos_create_thread ap_bt_startup_task\n", __func__);
#endif

    lvgl_app_init();

#if CONFIG_MEDIA_RECEIVE_DEMO
    //        media_receive_demo_init();
#endif

    cli_test_cmd_init();

    bk_wifi_set_wifi_media_mode(1);

    bk_wifi_set_video_quality(WIFI_VIDEO_QUALITY_SD);

#if CONFIG_MP3_PLAY_TEST
    extern int cli_mp3_play_init(void);
    cli_mp3_play_init();
#endif

#if CONFIG_BUTTON
    bk_key_service_init();
#endif
    return 0;
}
