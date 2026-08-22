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

#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_gatt_common.h"
#include "dm_gatt.h"

#include "media_msg.h"
#include "media_network_transfer.h"
#include "network_transfer.h"

#include "media_devices.h"
#include "media_navigation_transfer.h"
#include "network_provisioning.h"
#include "wifi_boarding_demo_service.h"
#include "wifi_boarding_demo.h"
#include "wifi_boarding_adv.h"
#if CONFIG_BLUETOOTH_ANCS_CLIENT
#include "ancs_client.h"
#endif

/* Firmware version advertised in the BLE provisioning core header. */
#define DASHBOARD_FW_MAJOR 1
#define DASHBOARD_FW_MINOR 0
#define DASHBOARD_FW_PATCH 0
#define TAG "np_demo"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef enum
{
    DEMO_NP_STATE_INACTIVE = 0,
    DEMO_NP_STATE_PREPARING,
    DEMO_NP_STATE_READY,
    DEMO_NP_STATE_CONNECTING,
} demo_np_state_t;

/*
 * Weak default network-ready hook. Projects that need to react when STA gets an
 * IP or a client connects to AP/P2P GO (e.g. scooter_1280_720_v2 starts the FTP
 * server in dashcam_storage.c) provide a strong override; everyone else links
 * this harmless no-op.
 */
__attribute__((weak)) void dashboard_network_ready_hook(void)
{
}

static bk_err_t (*s_send)(uint16_t opcode, int status);
static bk_err_t (*s_send_data)(uint16_t opcode, int status, const char *payload, uint16_t length);
static navigation_type_t navigation_type = NAVIGATION_TYPE_WIFI;
static bool s_casting_active = false;
static bool s_network_provisioned = false;
static volatile demo_np_state_t s_np_state = DEMO_NP_STATE_INACTIVE;
static volatile uint16_t s_pending_sta_opcode;
static volatile bool s_rearm_on_disconnect;

static bk_err_t demo_np_require_ready(void);

static void set_navigation_type(navigation_type_t type)
{
    navigation_type = type;
}

navigation_type_t get_navigation_type(void)
{
    return navigation_type;
}

/* Whether BLE-side navigation (casting) is currently active. */
bool bk_sl_np_is_navigating(void)
{
    return s_casting_active;
}

bool bk_sl_np_is_provisioned(void)
{
    return s_network_provisioned;
}

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
static void demo_np_upload_supported_mode(unsigned int os_code)
{
    uint8_t *val = NULL;
    uint8_t len = 0;
    val = demo_np_get_supported_mode(os_code, &len);
    if (val) {
        if(s_send_data) s_send_data(BOARDING_OP_SYNC_PHONE_OS, 0, (char *)val, len);
        os_printf("supported mode: %s\n", val);
        os_free(val);
    } else {
        LOGE("demo_np_get_supported_mode failed\n");
    }
}

static bool demo_np_parse_wifi_info(char *wifi_info, char **ssid, char **pwd)
{
    cJSON *root = NULL;
    cJSON *ssid_item = NULL;
    cJSON *password_item = NULL;
    bool valid = false;

    if (!wifi_info || !ssid || !pwd) {
        LOGE("Invalid parameters\n");
        return false;
    }

    // 初始化输出参数
    *ssid = NULL;
    *pwd = NULL;
    // 解析 JSON 字符串
    root = cJSON_Parse(wifi_info);
    if (!root) {
        LOGE("Failed to parse Wi-Fi JSON\n");
        return false;
    }

    // 获取 ssid 字段，使用 os_strdup 分配内存
    ssid_item = cJSON_GetObjectItem(root, "ssid");
    if (ssid_item && cJSON_IsString(ssid_item) && ssid_item->valuestring && (os_strlen(ssid_item->valuestring) <= 32)) {
        *ssid = os_strdup(ssid_item->valuestring);
        if (*ssid) {
            LOGI("Parsed ssid: %s\n", *ssid);
        } else {
            LOGE("Failed to duplicate ssid string\n");
        }
    } else {
        LOGE("Failed to get ssid from JSON\n");
        goto exit;
    }

    if (*ssid == NULL) {
        goto exit;
    }

    // Password is optional only when the field is absent (open network).
    password_item = cJSON_GetObjectItem(root, "password");
    if (password_item == NULL) 
    {
        valid = true;
    } else if (cJSON_IsString(password_item) && password_item->valuestring && (os_strlen(password_item->valuestring) <= 64)) 
    {
        *pwd = os_strdup(password_item->valuestring);
        if (*pwd) {
            LOGI("Parsed password len: %u\n", (unsigned int)os_strlen(*pwd));
            valid = true;
        } else {
            LOGE("Failed to duplicate password string\n");
        }
    } else {
        LOGE("Invalid password field in Wi-Fi JSON\n");
    }

exit:
    if (!valid) {
        os_free(*ssid);
        os_free(*pwd);
        *ssid = NULL;
        *pwd = NULL;
    }
    cJSON_Delete(root);
    return valid;
}

static bk_err_t demo_np_wifi_ap_start(char *ssid, char *pwd, uint8_t channel)
{
    wifi_ap_config_t ap_config = {0};
    netif_ip4_config_t ip4_config = {0};
    bk_err_t ret;

    if ((ssid == NULL) || (os_strlen(ssid) > 32) ||
        ((pwd != NULL) && (os_strlen(pwd) > 64))) {
        return BK_ERR_PARAM;
    }

    os_strcpy(ap_config.ssid, ssid);
    if (pwd) {
        os_strcpy(ap_config.password, pwd);
    }
    ap_config.channel = channel;

    os_strcpy(ip4_config.ip, WLAN_DEFAULT_IP);
    os_strcpy(ip4_config.mask, WLAN_DEFAULT_MASK);
    os_strcpy(ip4_config.gateway, WLAN_DEFAULT_GW);
    os_strcpy(ip4_config.dns, WLAN_DEFAULT_GW);
    ret = bk_netif_set_ip4_config(NETIF_IF_AP, &ip4_config);
    if (ret != BK_OK) {
        return ret;
    }
    ret = bk_wifi_ap_set_config(&ap_config);
    if (ret != BK_OK) {
        return ret;
    }
    return bk_wifi_ap_start();
}

//split packet to upload wifi scan result
bool enable_ble_split_pkt = false;

static bk_err_t demo_np_wifi_sta_connect(char *ssid, char *pwd)
{
    int ssid_len;
    int pwd_len = 0;
    bk_err_t ret;

    wifi_sta_config_t sta_config = {0};

    if (!ssid) {
        LOGW("ssid is NULL\r\n");
        return BK_FAIL;
    }

    ssid_len = os_strlen(ssid);
    if (pwd) {
        pwd_len = os_strlen(pwd);
    }

    if ((32 < ssid_len) || (64 < pwd_len))
    {
        LOGW("invalid Wi-Fi credential length\r\n");
        return BK_FAIL;
    }

    os_strcpy(sta_config.ssid, ssid);
    if (pwd) {
        os_strcpy(sta_config.password, pwd);
    } else {
        LOGW("pwd is NULL\r\n");
    }
#if CONFIG_STA_AUTO_RECONNECT
    sta_config.auto_reconnect_count = 5;
    sta_config.disable_auto_reconnect_after_disconnect = true;
#endif
    LOGI("connect STA ssid:%s\r\n", sta_config.ssid);
    ret = bk_wifi_sta_set_config(&sta_config);
    if (ret != BK_OK) {
        return ret;
    }

    return bk_wifi_sta_start();
}

#define BLE_SPLIT_PKT_LEN 400
static bk_err_t demo_np_wlan_scan_done_handler(void *arg, event_module_t event_module,
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
    LOGI("upload scan_rst %s, sent:%d, total:%d\r\n", payload, j, scan_result.ap_num);
    if ((j >= scan_result.ap_num) || (enable_ble_split_pkt == false))
    {
        if(s_send_data) s_send_data(BOARDING_OP_GET_SCAN_RESULTS, 0, payload, len);
    }
    else
    {
        if(s_send_data) s_send_data(BOARDING_OP_GET_SCAN_RESULTS, 1, payload, len);
        //rtos_delay_milliseconds(200);
        goto again;
    }

exit:
    bk_wifi_scan_free_result(&scan_result);

    return BK_OK;
}

static void handle_transfer_file_control_msg(uint8_t *data_ptr, uint16_t length)
{
    bk_err_t ret = BK_OK;

    /* The BLE provisioning queue frees data_ptr after this function returns */
    if ((data_ptr == NULL) || (length < sizeof(file_transfer_control_t)))
    {
        LOGE("%s %d invalid payload (ptr=%p, len=%u)\n", __func__, __LINE__, data_ptr, length);
        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
        return;
    }

    file_transfer_control_t *ctrl = (file_transfer_control_t *)data_ptr;

    LOGV("%s %d version=%u controller=%u\n", __func__, __LINE__, ctrl->version, ctrl->controller);

    if ((ctrl->version < FILE_TRANSFER_PROTOCOL_VERSION_MIN) || (ctrl->version > FILE_TRANSFER_PROTOCOL_VERSION_MAX))
    {
        LOGE("%s %d unsupported version %u (valid %u-%u)\n", __func__, __LINE__,
             ctrl->version, FILE_TRANSFER_PROTOCOL_VERSION_MIN, FILE_TRANSFER_PROTOCOL_VERSION_MAX);
        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
        return;
    }

    switch (ctrl->version)
    {
        case FILE_TRANSFER_PROTOCOL_VERSION_1:
        {
            switch (ctrl->controller)
            {
                case FILE_CONTROL_STOP:
                {
                    LOGI("%s %d stop received, cancelling transfer\n", __func__, __LINE__);
                    ret = media_navigation_transfer_cancel();
                    if (ret != BK_OK)
                    {
                        LOGE("%s %d cancel failed (%d)\n", __func__, __LINE__, ret);
                        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
                        return;
                    }
                    break;
                }

                case FILE_CONTROL_COMPLETE:
                {
                    LOGI("%s %d complete received\n", __func__, __LINE__);
                    if (media_navigation_transfer_is_active())
                    {
                        LOGW("%s %d transfer still active after COMPLETE\n", __func__, __LINE__);
                    }
                    break;
                }

                case FILE_CONTROL_START:
                {
                    size_t base_len = offsetof(file_transfer_control_v1_t, file_name);

                    if (length < base_len)
                    {
                        LOGE("%s %d start payload too short (%u)\n", __func__, __LINE__, length);
                        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
                        return;
                    }

                    file_transfer_control_v1_t *v1_ctrl = (file_transfer_control_v1_t *)data_ptr;
                    size_t available_name_len = length - base_len;
                    if (available_name_len > sizeof(v1_ctrl->file_name))
                    {
                        available_name_len = sizeof(v1_ctrl->file_name);
                    }

                    LOGV("%s %d start len=%u crc=0x%04X packets=%u operation=%u type=%u\n", __func__, __LINE__,
                         v1_ctrl->all_data_length, v1_ctrl->crc, v1_ctrl->packet_all_count,
                         v1_ctrl->file_operation, v1_ctrl->file_type);

                    media_navigation_transfer_cfg_t cfg = {
                        .version = v1_ctrl->version,
                        .total_length = v1_ctrl->all_data_length,
                        .packet_count = v1_ctrl->packet_all_count,
                        .expected_crc = v1_ctrl->crc,
                        .file_operation = v1_ctrl->file_operation,
                        .file_type = v1_ctrl->file_type,
                        .file_name = (available_name_len > 0) ? v1_ctrl->file_name : NULL,
                        .file_name_length = (uint32_t)available_name_len,
                    };

                    ret = media_navigation_transfer_begin(&cfg);
                    if (ret != BK_OK)
                    {
                        LOGE("%s %d begin failed (%d)\n", __func__, __LINE__, ret);
                        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
                        return;
                    }
                    break;
                }

                default:
                    LOGE("%s %d unknown controller %u\n", __func__, __LINE__, ctrl->controller);
                    if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
                    return;
            }
            break;
        }

        default:
            LOGE("%s %d handler missing for version %u\n", __func__, __LINE__, ctrl->version);
            if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_ERROR);
            return;
    }

    if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_CONTROL, EVT_STATUS_OK);
}

static void handle_transfer_file_data_msg(uint8_t *data_ptr, uint16_t length)
{
    bk_err_t ret = BK_OK;

    /* The BLE provisioning queue frees data_ptr after this function returns */
    if ((data_ptr == NULL) || (length < sizeof(uint16_t)))
    {
        LOGE("%s %d invalid payload (ptr=%p, len=%u)\n", __func__, __LINE__, data_ptr, length);
        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_DATA, EVT_STATUS_ERROR);
        return;
    }

    file_transfer_data_t *transfer = (file_transfer_data_t *)data_ptr;
    const uint16_t header_size = (uint16_t)sizeof(((file_transfer_data_t *)0)->packet_num);

    uint16_t data_length = (length > header_size) ? (length - header_size) : 0;

    bool frame_done = false;

    ret = media_navigation_transfer_push(transfer->packet_num, transfer->data, data_length, &frame_done);
    if (ret != BK_OK)
    {
        /* Includes overflow / invalid packet number / length or CRC mismatch. */
        LOGE("%s %d push failed (%d)\n", __func__, __LINE__, ret);
        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_DATA, EVT_STATUS_ERROR);
        return;
    }

    /* Ack only once per frame: mid-frame packets are not acked to cut BLE
     * round-trips. The engine reports frame_done when the last packet has been
     * assembled, CRC-checked and pushed to the decode pipeline. */
    if (frame_done)
    {
        LOGV("%s %d frame complete\n", __func__, __LINE__);
        if(s_send) s_send(BOARDING_OP_TRANSFER_FILE_DATA, EVT_STATUS_OK);
    }
}

static void handle_navigation_control_msg(uint8_t *data_ptr, uint16_t length)
{
    bk_err_t ret = BK_OK;

    /* The BLE provisioning queue frees data_ptr after this function returns */
    if ((data_ptr == NULL) || (length < sizeof(navigation_control_t)))
    {
        LOGE("%s %d invalid payload (ptr=%p, len=%u)\n", __func__, __LINE__, data_ptr, length);
        if(s_send) s_send(BOARDING_OP_NAVIGATION_CONTROL, EVT_STATUS_ERROR);
        return;
    }

    navigation_control_t *nav_ctrl = (navigation_control_t *)data_ptr;

    LOGD("%s %d controller=%u\n", __func__, __LINE__, nav_ctrl->controller);

    switch (nav_ctrl->controller)
    {
        case NAVIGATION_CONTROL_START:
        {
            ret = av_server_jpeg_decode_manager_turn_on();
            if (ret != BK_OK)
            {
                LOGE("%s %d turn_on failed (%d)\n", __func__, __LINE__, ret);
                if(s_send) s_send(BOARDING_OP_NAVIGATION_CONTROL, EVT_STATUS_ERROR);
                return;
            }

            #if CONFIG_LCD_PANEL_USE_480X272
                lvgl_app_enter_navigation();
            #else
                ret = lvgl_app_suspend_display();
                if (ret != BK_OK)
                {
                    LOGE("%s %d suspend display failed (%d)\n", __func__, __LINE__, ret);
                    av_server_jpeg_decode_manager_turn_off();
                    if(s_send) s_send(BOARDING_OP_NAVIGATION_CONTROL, EVT_STATUS_ERROR);
                    return;
                }
            #endif

            s_casting_active = true;
            wifi_boarding_demo_set_log_level(BOARDING_DEBUG_LEVEL_WARNING);
            break;
        }

        case NAVIGATION_CONTROL_STOP:
        {
            wifi_boarding_demo_set_log_level(BOARDING_DEBUG_LEVEL_INFO);
            ret = av_server_jpeg_decode_manager_turn_off();
            if (ret != BK_OK)
            {
                LOGE("%s %d turn_off failed (%d)\n", __func__, __LINE__, ret);
                if(s_send) s_send(BOARDING_OP_NAVIGATION_CONTROL, EVT_STATUS_ERROR);
                return;
            }

            s_casting_active = false;
            #if CONFIG_LCD_PANEL_USE_480X272
                lvgl_app_exit_navigation();
            #else
                /* LVGL/DPU 恢复已在 av_server_jpeg_decode_manager_turn_off() 内完成，勿重复调用
                 * （否则会二次 bk_display_open，DPU layer 配置失败并出现 DC underflow 闪屏） */
            #endif

            ret = media_navigation_transfer_cancel();
            if (ret != BK_OK)
            {
                LOGW("%s %d cancel returned %d\n", __func__, __LINE__, ret);
            }
            break;
        }

        default:
            /* controller=2 etc. may be sent by app as streaming/ready; ignore and ack OK */
            LOGD("%s %d controller=%u (ignored)\n", __func__, __LINE__, nav_ctrl->controller);
            break;
    }

    if(s_send) s_send(BOARDING_OP_NAVIGATION_CONTROL, EVT_STATUS_OK);
}

static void handle_navigation_type_control_msg(uint8_t *data_ptr, uint16_t length)
{
    /* The BLE provisioning queue frees data_ptr after this function returns */
    if ((data_ptr == NULL) || (length < sizeof(navigation_type_control_t)))
    {
        LOGE("%s %d invalid payload (ptr=%p, len=%u)\n", __func__, __LINE__, data_ptr, length);
        if(s_send) s_send(BOARDING_OP_NAVIGATION_TYPE_CONTROL, EVT_STATUS_ERROR);
        return;
    }

    navigation_type_control_t *nav_type_ctrl = (navigation_type_control_t *)data_ptr;

    LOGI("%s %d type=%u\n", __func__, __LINE__, nav_type_ctrl->type);

    set_navigation_type(nav_type_ctrl->type);

    if(s_send) s_send(BOARDING_OP_NAVIGATION_TYPE_CONTROL, EVT_STATUS_OK);
}

static void bk_sl_np_ble_msg_handle(uint16_t event, uint8_t *param, uint16_t length)
{
    switch (event)
    {
        case BOARDING_OP_STATION_START:
        case BOARDING_OP_SOFT_AP_START:
        {
            const boarding_wifi_config_t *config =
                (const boarding_wifi_config_t *)param;
            bk_err_t ret = BK_ERR_PARAM;

            if ((config != NULL) && (length == sizeof(*config)) &&
                (config->ssid[0] != '\0'))
            {
                ret = demo_np_require_ready();
                if (ret == BK_OK)
                {
                    s_np_state = DEMO_NP_STATE_CONNECTING;
                    if (event == BOARDING_OP_STATION_START)
                    {
                        s_pending_sta_opcode = event;
                        ret = demo_np_wifi_sta_connect((char *)config->ssid,
                                                       (char *)config->password);
                    }
                    else
                    {
                        s_pending_sta_opcode = 0;
                        ret = demo_np_wifi_ap_start((char *)config->ssid,
                                                   (char *)config->password,
                                                   (uint8_t)config->channel);
                    }
                }
            }

            if (ret != BK_OK)
            {
                if (s_np_state == DEMO_NP_STATE_CONNECTING)
                {
                    s_np_state = DEMO_NP_STATE_READY;
                }
                if (event == BOARDING_OP_STATION_START)
                {
                    s_pending_sta_opcode = 0;
                }
                if (s_send)
                {
                    s_send(event, EVT_STATUS_ERROR);
                }
            }
            else if (event == BOARDING_OP_SOFT_AP_START)
            {
                s_np_state = DEMO_NP_STATE_READY;
                if (s_send)
                {
                    s_send(event, EVT_STATUS_OK);
                }
            }
        }
        break;

        case BOARDING_OP_SYNC_PHONE_OS:
        {
            uint8_t os_code = ((param != NULL) && (length >= 1)) ? param[0] : 0;
            demo_np_upload_supported_mode(os_code);
        }
        break;

        case BOARDING_OP_CONFIG_WIFI_AP:
        {
            char *ssid = NULL, *pwd = NULL;
            bk_err_t ret;
            bool credentials_valid;

            credentials_valid = demo_np_parse_wifi_info((char *)param, &ssid, &pwd);
            ret = credentials_valid ? demo_np_require_ready() : BK_ERR_PARAM;
            if (ret == BK_OK)
            {
                s_pending_sta_opcode = 0;
                s_np_state = DEMO_NP_STATE_CONNECTING;
                ret = demo_np_wifi_ap_start(ssid, pwd, 0);
            }
            else
            {
                ret = BK_FAIL;
            }
            if (ssid)
            {
                os_free(ssid);
            }
            if (pwd)
            {
                os_free(pwd);
            }
            if (ret != BK_OK)
            {
                if (s_np_state == DEMO_NP_STATE_CONNECTING)
                {
                    s_np_state = DEMO_NP_STATE_READY;
                }
                if (s_send)
                {
                    s_send(BOARDING_OP_CONFIG_WIFI_AP, EVT_STATUS_ERROR);
                }
            }
            else
            {
                s_np_state = DEMO_NP_STATE_READY;
                static const uint8_t success_status = 0;
                if (s_send_data)
                {
                    s_send_data(BOARDING_OP_CONFIG_WIFI_AP, EVT_STATUS_OK,
                                (const char *)&success_status,
                                sizeof(success_status));
                }
            }
        }
        break;

        case BOARDING_OP_CONFIG_WIFI_STA:
        {
            char *ssid = NULL, *pwd = NULL;
            bk_err_t ret;
            bool credentials_valid;

            credentials_valid = demo_np_parse_wifi_info((char *)param, &ssid, &pwd);
            ret = credentials_valid ? demo_np_require_ready() : BK_ERR_PARAM;
            if (ret == BK_OK)
            {
                s_pending_sta_opcode = BOARDING_OP_CONFIG_WIFI_STA;
                s_np_state = DEMO_NP_STATE_CONNECTING;
                ret = demo_np_wifi_sta_connect(ssid, pwd);
            }
            else
            {
                ret = BK_FAIL;
            }
            if (ssid)
            {
                os_free(ssid);
            }
            if (pwd)
            {
                os_free(pwd);
            }
            if (ret != BK_OK)
            {
                if (s_np_state == DEMO_NP_STATE_CONNECTING)
                {
                    s_np_state = DEMO_NP_STATE_READY;
                }
                s_pending_sta_opcode = 0;
                if (s_send)
                {
                    s_send(BOARDING_OP_CONFIG_WIFI_STA, EVT_STATUS_ERROR);
                }
            }
        }
        break;

        case BOARDING_OP_GET_SCAN_RESULTS:
        {
            LOGI("BOARDING_OP_GET_SCAN_RESULTS\n");
            if ((param != NULL) && (length > 0)) {
                if (param[0] == 1)
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
            bk_err_t ret = demo_np_require_ready();

            // 获取 MAC 地址并生成 P2P 设备名称
            if (bk_wifi_sta_get_mac(mac) == BK_OK)
            {
                // 使用 MAC 地址的后3个字节，格式: bk_db_p2p_112233
                snprintf(p2p_name, sizeof(p2p_name), "bk_db_p2p_%02x%02x%02x",
                         mac[3], mac[4], mac[5]);
                LOGI("P2P device name: %s\n", p2p_name);
            }
            else
            {
                LOGW("Failed to get MAC address, using default P2P name\n");
                os_strcpy(p2p_name, "bk_db_p2p_000000");
            }

#if CONFIG_P2P
            if (ret == BK_OK)
            {
                s_pending_sta_opcode = 0;
                s_np_state = DEMO_NP_STATE_CONNECTING;
                ret = bk_wifi_p2p_enable_with_intent(p2p_name, 15);
            }
            if (ret == BK_OK)
            {
                ret = bk_wifi_p2p_find();
            }
#else
            ret = BK_ERR_NOT_SUPPORT;
#endif

            if (ret != BK_OK)
            {
                if (s_np_state == DEMO_NP_STATE_CONNECTING)
                {
                    s_np_state = DEMO_NP_STATE_READY;
                }
                if (s_send)
                {
                    s_send(BOARDING_OP_CONFIG_WIFI_P2P, EVT_STATUS_ERROR);
                }
            }
            else
            {
                s_np_state = DEMO_NP_STATE_READY;
                static const uint8_t success_status = 0;
                if (s_send_data)
                {
                    s_send_data(BOARDING_OP_CONFIG_WIFI_P2P, EVT_STATUS_OK,
                                (const char *)&success_status,
                                sizeof(success_status));
                }
            }
        }
        break;

        case BOARDING_OP_TRANSFER_FILE_CONTROL:
        {
            handle_transfer_file_control_msg(param, length);
        }
        break;

        case BOARDING_OP_TRANSFER_FILE_DATA:
        {
            handle_transfer_file_data_msg(param, length);
        }
        break;

        case BOARDING_OP_NAVIGATION_CONTROL:
        {
            handle_navigation_control_msg(param, length);
        }
        break;

        case BOARDING_OP_NAVIGATION_TYPE_CONTROL:
        {
            handle_navigation_type_control_msg(param, length);
        }
        break;

        default:
        {
            LOGI("%s %u, do nothing\r\n", __func__, event);
        }
        break;
    }
}

static void bk_sl_np_ble_msg_handle_demo_low_layer_cb(uint16_t op, uint8_t *data, uint32_t len)
{
    if (len > UINT16_MAX)
    {
        LOGE("opcode %u payload too large %u\n", op, (unsigned int)len);
        return;
    }
    bk_sl_np_ble_msg_handle(op, data, (uint16_t)len);
}


#if CONFIG_P2P
static void demo_np_reload_sdp_for_p2p(netif_if_t netif_idx)
{
    if (netif_idx == NETIF_IF_P2P ||
        (netif_idx == NETIF_IF_AP && bk_wifi_is_p2p_enabled())) {
        ntwk_sdp_reload(1000);
    }
}
#endif

static bk_err_t demo_np_require_ready(void)
{
    if (s_np_state == DEMO_NP_STATE_READY)
    {
        return BK_OK;
    }

    LOGW("network provisioning is not ready, state=%d\n", s_np_state);
    return BK_ERR_BUSY;
}

bk_err_t bk_sl_np_start_provisioning(void)
{
    bk_err_t ret;

    if (s_np_state == DEMO_NP_STATE_PREPARING)
    {
        return BK_OK;
    }

    if (s_np_state == DEMO_NP_STATE_READY)
    {
        return wifi_boarding_adv_start();
    }

    s_np_state = DEMO_NP_STATE_PREPARING;
    s_pending_sta_opcode = 0;
    s_rearm_on_disconnect = false;
#if CONFIG_BLUETOOTH_ANCS_CLIENT
    BK_LOG_ON_ERR(ancs_client_adv_stop());
#endif
    BK_LOG_ON_ERR(wifi_boarding_adv_stop());

#if CONFIG_P2P
    if (bk_wifi_is_p2p_enabled())
    {
        ret = bk_wifi_p2p_disable();
        if (ret != BK_OK)
        {
            goto fail;
        }
    }
#endif

    ret = bk_wifi_ap_stop();
    if (ret != BK_OK)
    {
        goto fail;
    }

    ret = bk_wifi_sta_stop();
    if (ret != BK_OK)
    {
        goto fail;
    }

    ret = bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_CONSOLE);
    if (ret != BK_OK)
    {
        goto fail;
    }

    return BK_OK;

fail:
    s_np_state = DEMO_NP_STATE_INACTIVE;
    LOGE("prepare network provisioning failed %d\n", ret);
    return ret;
}

static void demo_network_provisioning_status_cb(bk_network_provisioning_status_t status, void *user_data)
{
    netif_if_t netif_idx = (netif_if_t)(uintptr_t)user_data;

    LOGI("demo network provisioning status: %d\n", status);
    switch (status)
    {
        case BK_NETWORK_PROVISIONING_STATUS_IDLE:
            s_np_state = DEMO_NP_STATE_INACTIVE;
            s_rearm_on_disconnect = false;
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RUNNING:
            s_np_state = DEMO_NP_STATE_READY;
            s_rearm_on_disconnect = false;
            if (bk_dm_prf_gap_get_current_conn_id() < 0)
            {
                BK_LOG_ON_ERR(wifi_boarding_adv_start());
            }
            break;
        case BK_NETWORK_PROVISIONING_STATUS_SUCCEED:
            s_np_state = DEMO_NP_STATE_INACTIVE;
            s_network_provisioned = true;
            s_rearm_on_disconnect = false;
            BK_LOG_ON_ERR(wifi_boarding_adv_stop());
#if CONFIG_P2P
            demo_np_reload_sdp_for_p2p(netif_idx);
#endif
            if (s_pending_sta_opcode != 0)
            {
                static const uint8_t success_status = 0;
                if (s_pending_sta_opcode == BOARDING_OP_STATION_START)
                {
                    netif_ip4_config_t ip4_config = {0};
                    if ((bk_netif_get_ip4_config(NETIF_IF_STA, &ip4_config) == BK_OK) &&
                        s_send_data)
                    {
                        s_send_data(s_pending_sta_opcode, BK_OK,
                                    ip4_config.ip, os_strlen(ip4_config.ip));
                    }
                }
                else if (s_send_data)
                {
                    s_send_data(s_pending_sta_opcode, BK_OK,
                                (const char *)&success_status, 1);
                }
                s_pending_sta_opcode = 0;
            }
#if CONFIG_BLUETOOTH_ANCS_CLIENT
            BK_LOG_ON_ERR(ancs_client_adv_start());
#endif
            dashboard_network_ready_hook();
            break;
        case BK_NETWORK_PROVISIONING_STATUS_FAILED:
            s_np_state = DEMO_NP_STATE_INACTIVE;
            if (s_send && (s_pending_sta_opcode != 0))
            {
                s_send(s_pending_sta_opcode, EVT_STATUS_ERROR);
                s_pending_sta_opcode = 0;
            }
            if (bk_dm_prf_gap_get_current_conn_id() < 0)
            {
                BK_LOG_ON_ERR(bk_sl_np_start_provisioning());
            }
            else
            {
                s_rearm_on_disconnect = true;
            }
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECTING:
            s_np_state = DEMO_NP_STATE_INACTIVE;
            s_rearm_on_disconnect = false;
            BK_LOG_ON_ERR(wifi_boarding_adv_stop());
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_FAILED:
            s_np_state = DEMO_NP_STATE_INACTIVE;
            if (bk_dm_prf_gap_get_current_conn_id() < 0)
            {
                BK_LOG_ON_ERR(bk_sl_np_start_provisioning());
            }
            else
            {
                s_rearm_on_disconnect = true;
            }
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_SUCCEED:
            s_np_state = DEMO_NP_STATE_INACTIVE;
            s_network_provisioned = true;
            s_rearm_on_disconnect = false;
            BK_LOG_ON_ERR(wifi_boarding_adv_stop());
#if CONFIG_P2P
            demo_np_reload_sdp_for_p2p(netif_idx);
#endif
            dashboard_network_ready_hook();
            break;
        default:
            break;
    }
}

static void bk_sl_np_ble_disconnect_cb(void)
{
    s_pending_sta_opcode = 0;

#if CONFIG_BLUETOOTH_ANCS_CLIENT
    if (s_network_provisioned && s_np_state == DEMO_NP_STATE_INACTIVE)
    {
        BK_LOG_ON_ERR(ancs_client_adv_start());
    }
#endif

    if ((s_np_state == DEMO_NP_STATE_READY) || (s_np_state == DEMO_NP_STATE_CONNECTING))
    {
        BK_LOG_ON_ERR(wifi_boarding_adv_start());
    }
    else if (s_rearm_on_disconnect)
    {
        s_rearm_on_disconnect = false;
        BK_LOG_ON_ERR(bk_sl_np_start_provisioning());
    }

    media_navigation_transfer_cancel();

    if (!s_casting_active)
    {
        return;
    }

    LOGW("BLE disconnected while casting active, stopping cast\n");
    s_casting_active = false;

    av_server_jpeg_decode_manager_turn_off();
#if CONFIG_LCD_PANEL_USE_480X272
    lvgl_app_exit_navigation();
#else
    /* 同上：turn_off 内已 resume，避免重复 */
#endif
}

bk_err_t bk_sl_np_init(void)
{
    bk_err_t ret = BK_OK;
    netif_if_t reconnect_netif = NETIF_IF_INVALID;

    s_send = bk_boarding_event_notify;
    s_send_data = bk_boarding_event_notify_with_data;

    wifi_boarding_demo_reg_ble_disconnect_cb(bk_sl_np_ble_disconnect_cb);
    ret = wifi_boarding_demo_reg_external_cmd(bk_sl_np_ble_msg_handle_demo_low_layer_cb);
    if (ret != BK_OK)
    {
        return ret;
    }

    ret = wifi_boarding_adv_init();
    if (ret != BK_OK)
    {
        return ret;
    }

    wifi_boarding_adv_set_device_info(0x03, DASHBOARD_FW_MAJOR,
                                      DASHBOARD_FW_MINOR, DASHBOARD_FW_PATCH);

    ret = bk_register_network_provisioning_status_cb(demo_network_provisioning_status_cb);
    if (ret != BK_OK)
    {
        return ret;
    }

    ret = bk_network_auto_reconnect_init(&reconnect_netif);
    if (ret != BK_OK)
    {
        return ret;
    }
    s_network_provisioned = (reconnect_netif != NETIF_IF_INVALID);

    if (reconnect_netif == NETIF_IF_INVALID)
    {
        ret = bk_sl_np_start_provisioning();
    }

#if CONFIG_MEDIA_RECEIVE_DEMO
#if CONFIG_MEDIA_DEMO_MODE_TCP
    media_bk_network_transfer_init("tcp_service", NULL);
#else
    media_bk_network_transfer_init("udp_service", NULL);
#endif
#endif

    return ret;
}
