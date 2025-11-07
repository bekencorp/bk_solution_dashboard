// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk_network_provisioning.h"
#include <components/event.h>
#include <components/netif.h>
#include "bk_wifi.h"
#include "bk_wifi_types.h"
#include "bk_cli.h"
#include "os/str.h"
#include "components/log.h"
#include "cli.h"
#include "cJSON.h"
#include <components/media_types.h>
#include "media_cmd.h"
#if CONFIG_MEDIA_RECEIVE_DEMO
#include "media_tcp_service.h"
#include "media_udp_service.h"
#endif

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define TAG "np_demo"

typedef enum
{
    BOARDING_OP_UNKNOWN = 0,
    BOARDING_OP_SYNC_PHONE_OS = 30,
    BOARDING_OP_CONFIG_WIFI_AP = 31,
    BOARDING_OP_GET_SCAN_RESULTS = 32,
    BOARDING_OP_CONFIG_WIFI_STA = 33,
    BOARDING_OP_CONFIG_WIFI_P2P = 34,
    BOARDING_OP_MAX
} boarding_opcode_t;

static uint8_t *demo_np_get_supported_mode(uint8_t os_code, uint8_t *len)
{
    cJSON *root = NULL;
    cJSON *type_array = NULL;
    uint8_t mac[6] = {0};
    char mac_str[18] = {0};  // 格式: "xx:xx:xx:xx:xx:xx"
    char p2p_device_name[32] = {0};  // P2P设备名称
    char *json_str = NULL;
    uint8_t *val = NULL;
    int i = 0;
    int array_len = (os_code == 1) ? 2 : 3;  // iOS只支持2种模式，Android支持3种

    // 创建JSON根对象
    root = cJSON_CreateObject();
    if (!root) {
        LOGE("Failed to create JSON object\n");
        *len = 0;
        return NULL;
    }

    // 创建type数组
    type_array = cJSON_CreateArray();
    if (!type_array) {
        LOGE("Failed to create type array\n");
        cJSON_Delete(root);
        *len = 0;
        return NULL;
    }

    // 填充type数组 [0, 1, 2] 或 [0, 1]
    for (i = 0; i < array_len; i++) {
        cJSON_AddItemToArray(type_array, cJSON_CreateNumber(i));
    }
    cJSON_AddItemToObject(root, "type", type_array);

    // 获取MAC地址
    if (bk_wifi_sta_get_mac(mac) == BK_OK) {
        // 格式化MAC地址为字符串 "c8:47:8c:11:22:33"
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        // 生成P2P设备名称 "bk_db_p2p_XXXXXX"
        snprintf(p2p_device_name, sizeof(p2p_device_name), "bk_db_p2p_%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
    } else {
        LOGW("Failed to get MAC address, using default\n");
        os_strcpy(mac_str, "00:00:00:00:00:00");
        os_strcpy(p2p_device_name, "bk_db_p2p_000000");
    }
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddStringToObject(root, "name", p2p_device_name);

    // 将JSON对象转换为字符串
    json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOGE("Failed to print JSON\n");
        cJSON_Delete(root);
        *len = 0;
        return NULL;
    }

    // 分配内存并复制JSON字符串
    *len = os_strlen(json_str);
    val = os_zalloc(*len + 1);
    if (val) {
        os_memcpy(val, json_str, *len);
        LOGI("Supported mode JSON: %s\n", json_str);
    } else {
        LOGE("Failed to allocate memory for JSON string\n");
        *len = 0;
    }

    // 清理资源
    cJSON_free(json_str);
    cJSON_Delete(root);

    return val;
}

/**
 * @brief Upload supported mode to phone OS
 * @param os_code: OS code, 0: Android, 1: iOS
 * @param supported_mode: supported mode, 0: STA, 1: AP, 2: P2P
 * @return void
 */
static void demo_np_upload_supported_mode(uint os_code)
{
    uint8_t *val = NULL;
    uint8_t len = 0;
    val = demo_np_get_supported_mode(os_code, &len);
    bk_ble_provisioning_event_notify_with_data(BOARDING_OP_SYNC_PHONE_OS, 0, (char *)val, len);
    os_printf("supported mode: %s\n", val);
    if (val)
        os_free(val);
}

static void demo_np_parse_wifi_info(char *wifi_info, char **ssid, char **pwd)
{
    cJSON *root = NULL;
    cJSON *ssid_item = NULL;
    cJSON *password_item = NULL;

    if (!wifi_info || !ssid || !pwd) {
        LOGE("Invalid parameters\n");
        return;
    }

    // 初始化输出参数
    *ssid = NULL;
    *pwd = NULL;
    os_printf("wifi_info: %s\n", wifi_info);
    // 解析 JSON 字符串
    root = cJSON_Parse(wifi_info);
    if (!root) {
        LOGE("Failed to parse JSON: %s\n", wifi_info);
        return;
    }

    // 获取 ssid 字段，使用 os_strdup 分配内存
    ssid_item = cJSON_GetObjectItem(root, "ssid");
    if (ssid_item && cJSON_IsString(ssid_item) && ssid_item->valuestring) {
        *ssid = os_strdup(ssid_item->valuestring);
        if (*ssid) {
            LOGI("Parsed ssid: %s\n", *ssid);
        } else {
            LOGE("Failed to duplicate ssid string\n");
        }
    } else {
        LOGE("Failed to get ssid from JSON\n");
    }

    // 获取 password 字段，使用 os_strdup 分配内存
    password_item = cJSON_GetObjectItem(root, "password");
    if (password_item && cJSON_IsString(password_item) && password_item->valuestring) {
        *pwd = os_strdup(password_item->valuestring);
        if (*pwd) {
            LOGI("Parsed password: %s\n", *pwd);
        } else {
            LOGE("Failed to duplicate password string\n");
        }
    } else {
        LOGE("Failed to get password from JSON\n");
    }

    // 释放 cJSON 对象
    cJSON_Delete(root);
}

static int demo_np_wifi_ap_start(char *ssid, char *pwd)
{
    wifi_ap_config_t ap_config = {0};

    if (ssid) {
        os_strcpy(ap_config.ssid, ssid);
    }
    if (pwd) {
        os_strcpy(ap_config.password, pwd);
    }

    BK_LOG_ON_ERR(bk_wifi_ap_set_config(&ap_config));
    return bk_wifi_ap_start();
}

//split packet to upload wifi scan result
bool enable_ble_split_pkt = false;

static int demo_np_wifi_sta_connect(char *ssid, char *pwd)
{
    int ssid_len, key_len;

    wifi_sta_config_t sta_config = {0};

    ssid_len = os_strlen(ssid);

    if (32 < ssid_len)
    {
        LOGW("ssid name more than 32 Bytes\r\n");
        return BK_FAIL;
    }

    if (ssid) {
        os_strcpy(sta_config.ssid, ssid);
    } else {
        LOGW("ssid is NULL\r\n");
        return BK_FAIL;
    }
    if (pwd) {
        os_strcpy(sta_config.password, pwd);
    } else {
        LOGW("pwd is NULL\r\n");
    }
#if CONFIG_STA_AUTO_RECONNECT
    sta_config.auto_reconnect_count = 5;
    sta_config.disable_auto_reconnect_after_disconnect = true;
#endif
    LOGI("ssid:%s key:%s\r\n", sta_config.ssid, sta_config.password);
    BK_LOG_ON_ERR(bk_wifi_sta_set_config(&sta_config));
    BK_LOG_ON_ERR(bk_wifi_sta_start());

    return BK_OK;
}

#define BLE_SPLIT_PKT_LEN 400
static int demo_np_wlan_scan_done_handler(void *arg, event_module_t event_module,
								  int event_id, void *event_data)
{
    wifi_scan_result_t scan_result = {0};
    char payload[BLE_SPLIT_PKT_LEN];
    uint16_t len = 0;
    int i = 0, j = 0;

    BK_LOG_ON_ERR(bk_wifi_scan_get_result(&scan_result));
    if (scan_result.ap_num == 0)
        goto exit;

again:
    os_memset(payload, 0, BLE_SPLIT_PKT_LEN);
    len = os_snprintf(payload, BLE_SPLIT_PKT_LEN, "[");
    for (i = j; i < scan_result.ap_num; i++) {
        if (!os_strlen(scan_result.aps[i].ssid))
            continue;
        if ((len + 5 + os_strlen(scan_result.aps[i].ssid)) > BLE_SPLIT_PKT_LEN) {
            j = i;
            break;
        }
        if ((i != 0) && (len != 1))
            len += os_snprintf(payload+len, BLE_SPLIT_PKT_LEN, ",");
        len += os_snprintf(payload+len, BLE_SPLIT_PKT_LEN, "\"%s\"", scan_result.aps[i].ssid);
        j = i + 1;
    }
    len += os_snprintf(payload+len, BLE_SPLIT_PKT_LEN, "]");
    LOGI("upload scan_rst %s, sended:%d, total:%d\r\n", payload, j, scan_result.ap_num);
    if ((j >= scan_result.ap_num) || (enable_ble_split_pkt == false))
        bk_ble_provisioning_event_notify_with_data(BOARDING_OP_GET_SCAN_RESULTS, 0, payload, len);
    else {
        bk_ble_provisioning_event_notify_with_data(BOARDING_OP_GET_SCAN_RESULTS, 1, payload, len);
        //rtos_delay_milliseconds(200);
        goto again;
    }

exit:
    bk_wifi_scan_free_result(&scan_result);

    return BK_OK;
}

void ble_msg_handle_demo_cb(ble_prov_msg_t *msg)
{
    switch (msg->event)
    {
        case BOARDING_OP_SYNC_PHONE_OS:
        {
            uint8_t os_code = (uint8_t)msg->param;
            demo_np_upload_supported_mode(os_code);
        }
        break;

        case BOARDING_OP_CONFIG_WIFI_AP:
        {
            char *ssid = NULL, *pwd = NULL;
            demo_np_parse_wifi_info((char *)msg->param, &ssid, &pwd);
            demo_np_wifi_ap_start(ssid, pwd);
            if (ssid) {
                os_free(ssid);
            }
            if (pwd) {
                os_free(pwd);
            }
            // 发送成功状态码 0 给手机APP
            static const uint8_t success_status = 0;
            bk_ble_provisioning_event_notify_with_data(BOARDING_OP_CONFIG_WIFI_AP, BK_OK, 
                                                       (char *)&success_status, sizeof(success_status));
        }
        break;

        case BOARDING_OP_CONFIG_WIFI_STA:
        {
            char *ssid = NULL, *pwd = NULL;
            demo_np_parse_wifi_info((char *)msg->param, &ssid, &pwd);
            demo_np_wifi_sta_connect(ssid, pwd);
            if (ssid) {
                os_free(ssid);
            }
            if (pwd) {
                os_free(pwd);
            }
            bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                                        demo_np_wlan_scan_done_handler);
        }
        break;

        case BOARDING_OP_GET_SCAN_RESULTS:
        {
            LOGI("BOARDING_OP_GET_SCAN_RESULTS\n");
            if (msg->param) {
                if (*(uint8_t *)msg->param == 1)
                    enable_ble_split_pkt = true;
                else
                    enable_ble_split_pkt = false;
            }
            bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                        demo_np_wlan_scan_done_handler, NULL);
            BK_LOG_ON_ERR(bk_wifi_scan_start(NULL));
        }
        break;

        case BOARDING_OP_CONFIG_WIFI_P2P:
        {
            uint8_t mac[6] = {0};
            char p2p_name[32] = {0};  // bk_db_p2p_XXXXXX 格式

            // 获取 MAC 地址并生成 P2P 设备名称
            if (bk_wifi_sta_get_mac(mac) == BK_OK) {
                // 使用 MAC 地址的后3个字节，格式: bk_db_p2p_112233
                snprintf(p2p_name, sizeof(p2p_name), "bk_db_p2p_%02x%02x%02x",
                         mac[3], mac[4], mac[5]);
                LOGI("P2P device name: %s\n", p2p_name);
            } else {
                LOGW("Failed to get MAC address, using default P2P name\n");
                os_strcpy(p2p_name, "bk_db_p2p_000000");
            }

            bk_wifi_p2p_enable(p2p_name);
            bk_wifi_p2p_find();

            // 发送成功状态码 0 给手机APP
            static const uint8_t success_status = 0;
            bk_ble_provisioning_event_notify_with_data(BOARDING_OP_CONFIG_WIFI_P2P, BK_OK,
                                                       (char *)&success_status, sizeof(success_status));
        }
        break;

        default:
        {
            LOGI("%s %d, do nothing\r\n", __func__, msg->event);
        }
        break;
    }
}

void demo_network_provisioning_status_cb(bk_network_provisioning_status_t status, void *user_data)
{
    LOGI("demo network provisioning status: %d\n", status);
    switch (status)
    {
        case BK_NETWORK_PROVISIONING_STATUS_IDLE:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RUNNING:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_SUCCEED:
            if (bk_network_provisioning_get_type() == BK_NETWORK_PROVISIONING_TYPE_BLE) {
                netif_if_t netif_idx = (netif_if_t)user_data;
#if 0
                netif_ip4_config_t ip4_config = {0};

                bk_netif_get_ip4_config(netif_idx, &ip4_config);
                LOGI("netif_idx:%d, ip: %s\n", netif_idx, ip4_config.ip);
                bk_ble_provisioning_event_notify_with_data(BOARDING_OP_CONFIG_WIFI_STA, BK_OK, ip4_config.ip, strlen(ip4_config.ip));
#else
                if (netif_idx == NETIF_IF_STA) {
                    // 发送成功状态码 0 给手机APP
                    static const uint8_t success_status = 0;
                    bk_ble_provisioning_event_notify_with_data(BOARDING_OP_CONFIG_WIFI_STA, BK_OK, 
                                                               (char *)&success_status, 1);
                }
#endif
            }
            break;
        case BK_NETWORK_PROVISIONING_STATUS_FAILED:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECTING:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_FAILED:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_SUCCEED:
            break;
        default:
            break;
    }
}

static void cli_network_provisioning(char *pcWriteBuffer, int xWriteBufferLen, int argC, char **argV)
{
    if (argC == 1) {
        bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
    } else if (argC == 2) {
        if (os_strcmp(argV[1], "ble") == 0) {
            bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
        } else if (os_strcmp(argV[1], "console") == 0) {
            bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_CONSOLE);
        }
    }
}

static void cli_erase_network_provisioning_info(char *pcWriteBuffer, int xWriteBufferLen, int argC, char **argV)
{
    erase_network_auto_reconnect_info();
}

int demo_netif_event_cb(void *arg, event_module_t event_module,
					   int event_id, void *event_data)
{
	netif_event_got_ip4_t *got_ip;

	switch (event_id) {
	case EVENT_NETIF_GOT_IP4:
		got_ip = (netif_event_got_ip4_t *)event_data;
		LOGD("%s got ip\n", got_ip->netif_if == NETIF_IF_STA ? "BK STA" : "unknown netif");
		break;
	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}

int demo_wifi_event_cb(void *arg, event_module_t event_module,
					  int event_id, void *event_data)
{
	wifi_event_sta_disconnected_t *sta_disconnected;
	wifi_event_sta_connected_t *sta_connected;
	wifi_event_ap_disconnected_t *ap_disconnected;
	wifi_event_ap_connected_t *ap_connected;
	wifi_event_network_found_t *network_found;

	switch (event_id) {
	case EVENT_WIFI_STA_CONNECTED:
		sta_connected = (wifi_event_sta_connected_t *)event_data;
		LOGD("BK STA connected %s\n", sta_connected->ssid);
		break;

	case EVENT_WIFI_STA_DISCONNECTED:
		sta_disconnected = (wifi_event_sta_disconnected_t *)event_data;
		LOGD("BK STA disconnected, reason(%d)%s\n", sta_disconnected->disconnect_reason,
			sta_disconnected->local_generated ? ", local_generated" : "");
		break;

	case EVENT_WIFI_AP_CONNECTED:
		ap_connected = (wifi_event_ap_connected_t *)event_data;
		LOGD(BK_MAC_FORMAT" connected to BK AP\n", BK_MAC_STR(ap_connected->mac));
		break;

	case EVENT_WIFI_AP_DISCONNECTED:
		ap_disconnected = (wifi_event_ap_disconnected_t *)event_data;
		LOGD(BK_MAC_FORMAT" disconnected from BK AP\n", BK_MAC_STR(ap_disconnected->mac));
		break;

	case EVENT_WIFI_NETWORK_FOUND:
		network_found = (wifi_event_network_found_t *)event_data;
		LOGD(" target AP: %s, bssid %pm found\n", network_found->ssid, network_found->bssid);
		break;

	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}
#define NP_CMD_COUNT (sizeof(s_network_provisioning_commands) / sizeof(s_network_provisioning_commands[0]))
static const struct cli_command s_network_provisioning_commands[] = {
    {"np", "np or np [ble]|[console]", cli_network_provisioning},
    {"np_erase", "np_erase", cli_erase_network_provisioning_info},
};

int cli_network_provisioning_init(void)
{
    //bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL, demo_wifi_event_cb, NULL);
    //bk_event_register_cb(EVENT_MOD_NETIF, EVENT_ID_ALL, demo_netif_event_cb, NULL);
#if CONFIG_MEDIA_RECEIVE_DEMO
    av_server_cmd_server_init();
#if CONFIG_MEDIA_DEMO_MODE_TCP
    av_server_tcp_service_init(ROTATE_NONE);
#else
    av_server_udp_service_init(ROTATE_NONE);
#endif
#endif
    return cli_register_commands(s_network_provisioning_commands, NP_CMD_COUNT);
}