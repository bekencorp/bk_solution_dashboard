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
#if CONFIG_BK_BLE_PROVISIONING
#include "ble_scheme/ble_provisioning.h"
#endif
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

/*
 * Weak default network-ready hook. Projects that need to react when STA gets an
 * IP or a client connects to AP/P2P GO (e.g. scooter_1280_720_v2 starts the FTP
 * server in dashcam_storage.c) provide a strong override; everyone else links
 * this harmless no-op.
 */
__attribute__((weak)) void dashboard_network_ready_hook(void)
{
}

//bk_ble_provisioning_event_notify_with_data
static void (*s_send)(uint16_t opcode, int status);
static void (*s_send_data)(uint16_t opcode, int status, char *payload, uint16_t length);
static navigation_type_t navigation_type = NAVIGATION_TYPE_WIFI;
static bool s_casting_active = false;
//static uint8_t s_reg_method;

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

static bk_err_t demo_np_wifi_ap_start(char *ssid, char *pwd)
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

static bk_err_t demo_np_wifi_sta_connect(char *ssid, char *pwd)
{
    int ssid_len;

    wifi_sta_config_t sta_config = {0};

    if (!ssid) {
        LOGW("ssid is NULL\r\n");
        return BK_FAIL;
    }

    ssid_len = os_strlen(ssid);

    if (32 < ssid_len)
    {
        LOGW("ssid name more than 32 Bytes\r\n");
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
    LOGI("ssid:%s key:%s\r\n", sta_config.ssid, sta_config.password);
    BK_LOG_ON_ERR(bk_wifi_sta_set_config(&sta_config));
    BK_LOG_ON_ERR(bk_wifi_sta_start());

    return BK_OK;
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
    LOGI("upload scan_rst %s, sended:%d, total:%d\r\n", payload, j, scan_result.ap_num);
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

            s_casting_active = true;

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

#if CONFIG_BK_BLE_PROVISIONING
#else
            wifi_boarding_demo_set_log_level(BOARDING_DEBUG_LEVEL_WARNING);
#endif
            break;
        }

        case NAVIGATION_CONTROL_STOP:
        {
            s_casting_active = false;

#if CONFIG_BK_BLE_PROVISIONING
#else
            wifi_boarding_demo_set_log_level(BOARDING_DEBUG_LEVEL_INFO);
#endif
            ret = av_server_jpeg_decode_manager_turn_off();
            if (ret != BK_OK)
            {
                LOGE("%s %d turn_off failed (%d)\n", __func__, __LINE__, ret);
                if(s_send) s_send(BOARDING_OP_NAVIGATION_CONTROL, EVT_STATUS_ERROR);
                return;
            }

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

static void bk_sl_np_ble_msg_handle_demo_cb(ble_prov_msg_t *msg)
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
            if(s_send_data) s_send_data(BOARDING_OP_CONFIG_WIFI_AP, BK_OK,
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

#if CONFIG_P2P
            bk_wifi_p2p_enable_with_intent(p2p_name, 15);
            bk_wifi_p2p_find();
#endif

            // 发送成功状态码 0 给手机APP
            static const uint8_t success_status = 0;
            if(s_send_data) s_send_data(BOARDING_OP_CONFIG_WIFI_P2P, BK_OK,
                                                       (char *)&success_status, sizeof(success_status));
        }
        break;

        case BOARDING_OP_TRANSFER_FILE_CONTROL:
        {
            handle_transfer_file_control_msg((uint8_t *)msg->param, (uint16_t)msg->length);
        }
        break;

        case BOARDING_OP_TRANSFER_FILE_DATA:
        {
            handle_transfer_file_data_msg((uint8_t *)msg->param, (uint16_t)msg->length);
        }
        break;

        case BOARDING_OP_NAVIGATION_CONTROL:
        {
            handle_navigation_control_msg((uint8_t *)msg->param, (uint16_t)msg->length);
        }
        break;

        case BOARDING_OP_NAVIGATION_TYPE_CONTROL:
        {
            handle_navigation_type_control_msg((uint8_t *)msg->param, (uint16_t)msg->length);
        }
        break;

        default:
        {
            LOGI("%s %d, do nothing\r\n", __func__, msg->event);
        }
        break;
    }
}

static void bk_sl_np_ble_msg_handle_demo_low_layer_cb(uint16_t op, uint8_t *data, uint32_t len)
{
    ble_prov_msg_t msg = {0};
    msg.event = op;
    msg.param = (typeof(msg.param))data;
    msg.length = len;

    bk_sl_np_ble_msg_handle_demo_cb(&msg);
}


#if CONFIG_BK_BLE_PROVISIONING

#if CONFIG_P2P
static void demo_np_reload_sdp_for_p2p(netif_if_t netif_idx)
{
    if (netif_idx == NETIF_IF_P2P ||
        (netif_idx == NETIF_IF_AP && bk_wifi_is_p2p_enabled())) {
        ntwk_sdp_reload(1000);
    }
}
#endif

static void demo_network_provisioning_status_cb(bk_network_provisioning_status_t status, void *user_data)
{
    LOGI("demo network provisioning status: %d\n", status);
    switch (status)
    {
        case BK_NETWORK_PROVISIONING_STATUS_IDLE:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RUNNING:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_SUCCEED:
            if (bk_network_provisioning_get_type() == BK_NETWORK_PROVISIONING_TYPE_BLE)
            {
                netif_if_t netif_idx = (netif_if_t)user_data;
#if CONFIG_P2P
                demo_np_reload_sdp_for_p2p(netif_idx);
#endif
#if 0
                netif_ip4_config_t ip4_config = {0};

                bk_netif_get_ip4_config(netif_idx, &ip4_config);
                LOGI("netif_idx:%d, ip: %s\n", netif_idx, ip4_config.ip);
                if(s_send_data) s_send_data(BOARDING_OP_CONFIG_WIFI_STA, BK_OK, ip4_config.ip, strlen(ip4_config.ip));
#else
                if (netif_idx == NETIF_IF_STA) {
                    // 发送成功状态码 0 给手机APP
                    static const uint8_t success_status = 0;
                    if(s_send_data) s_send_data(BOARDING_OP_CONFIG_WIFI_STA, BK_OK,
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
#if CONFIG_P2P
            demo_np_reload_sdp_for_p2p((netif_if_t)user_data);
#endif
            break;
        default:
            break;
    }
}
#endif
// static void cli_network_provisioning(char *pcWriteBuffer, int xWriteBufferLen, int argC, char **argV)
// {
//     if (argC == 1) {
//         bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
//     } else if (argC == 2) {
//         if (os_strcmp(argV[1], "ble") == 0) {
//             bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
//         } else if (os_strcmp(argV[1], "console") == 0) {
//             bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_CONSOLE);
//         }
//     }
// }

// static void cli_erase_network_provisioning_info(char *pcWriteBuffer, int xWriteBufferLen, int argC, char **argV)
// {
//     erase_network_auto_reconnect_info();
// }

static bk_err_t demo_netif_event_cb(void *arg, event_module_t event_module,
					   int event_id, void *event_data)
{
	netif_event_got_ip4_t *got_ip;

	switch (event_id) {
	case EVENT_NETIF_GOT_IP4:
    {
		got_ip = (netif_event_got_ip4_t *)event_data;
		LOGI("%s got ip\n", got_ip->netif_if == NETIF_IF_STA ? "BK STA" : "unknown netif");
        uint8_t success_status = 0;
        if(s_send_data) s_send_data(BOARDING_OP_CONFIG_WIFI_STA, BK_OK, (char *)&success_status, sizeof(success_status));
        if (got_ip->netif_if == NETIF_IF_STA)
        {
            dashboard_network_ready_hook();
        }
    }
    break;

	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}

#if CONFIG_BK_BLE_PROVISIONING
/*
 * Netif event cb for the SDK provisioning path (reg_method==0). Unlike
 * demo_netif_event_cb() (wifi_boarding path), it does NOT send
 * BOARDING_OP_CONFIG_WIFI_STA to the phone on got-ip: the SDK path already
 * notifies the phone from demo_network_provisioning_status_cb() on SUCCEED.
 * It only fires the network-ready hook so FTP (scooter v2) starts once STA has
 * an IP.
 */
static bk_err_t demo_np_netif_event_cb(void *arg, event_module_t event_module,
					   int event_id, void *event_data)
{
	netif_event_got_ip4_t *got_ip;

	switch (event_id) {
	case EVENT_NETIF_GOT_IP4:
    {
		got_ip = (netif_event_got_ip4_t *)event_data;
		LOGI("%s got ip (np)\n", got_ip->netif_if == NETIF_IF_STA ? "BK STA" : "unknown netif");
        if (got_ip->netif_if == NETIF_IF_STA)
        {
            dashboard_network_ready_hook();
        }
    }
    break;

	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}
#endif

static bk_err_t demo_wifi_event_cb(void *arg, event_module_t event_module,
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
		dashboard_network_ready_hook();
		break;

	case EVENT_WIFI_AP_DISCONNECTED:
		ap_disconnected = (wifi_event_ap_disconnected_t *)event_data;
		LOGD(BK_MAC_FORMAT" disconnected from BK AP\n", BK_MAC_STR(ap_disconnected->mac));
		break;

	case EVENT_WIFI_NETWORK_FOUND:
		network_found = (wifi_event_network_found_t *)event_data;
		LOGD(" target AP: %s, bssid %pm found\n", network_found->ssid, network_found->bssid);
		break;

    case EVENT_WIFI_GO_CONNECTED:
        LOGD("WIFI_GO_CONNECTED\n");
        dashboard_network_ready_hook();
        break;

	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}


static void bk_sl_np_ble_disconnect_cb(void)
{
    if (!s_casting_active)
        return;

    LOGW("BLE disconnected while casting active, stopping cast\n");
    s_casting_active = false;

    av_server_jpeg_decode_manager_turn_off();
    media_navigation_transfer_cancel();
#if CONFIG_LCD_PANEL_USE_480X272
    lvgl_app_exit_navigation();
#else
    /* 同上：turn_off 内已 resume，避免重复 */
#endif
}

bk_err_t bk_sl_np_init(uint8_t reg_method) // 0 use avdk sdk np component, 1 use solution component)
{
    bk_err_t ret = BK_OK;

    wifi_boarding_demo_reg_ble_disconnect_cb(bk_sl_np_ble_disconnect_cb);

    if(reg_method == 1)
    {
        wifi_boarding_demo_reg_external_cmd(bk_sl_np_ble_msg_handle_demo_low_layer_cb);
        bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL, demo_wifi_event_cb, NULL);
        bk_event_register_cb(EVENT_MOD_NETIF, EVENT_ID_ALL, demo_netif_event_cb, NULL);
        s_send = bk_boarding_event_notify;
        s_send_data = bk_boarding_event_notify_with_data;
    }
#if CONFIG_BK_BLE_PROVISIONING
    else if(!reg_method)
    {
        bk_register_network_provisioning_status_cb(demo_network_provisioning_status_cb);
        bk_ble_provisioning_set_msg_handle_cb(bk_sl_np_ble_msg_handle_demo_cb);

        /* Advertise per the BLE provisioning adv spec: Local Name
         * "BK_DASHBOARD_<MAC3>" + the core header {proto_ver, device_type=
         * DASHBOARD, fw x3} in the ADV Manufacturer Specific Data. Set before
         * init so the very first advertisement already carries them. */
        {
            uint8_t mac[6] = {0};
            char adv_name[32] = {0};

            bk_bluetooth_get_address(mac);
            snprintf(adv_name, sizeof(adv_name), "BK_%s_%02X%02X%02X",
                     bk_ble_provisioning_dev_type_tag(BK_BLE_PROV_DEV_TYPE_DASHBOARD),
                     mac[0], mac[1], mac[2]);
            bk_ble_provisioning_set_adv_name(adv_name);
            bk_ble_provisioning_set_dev_info(BK_BLE_PROV_DEV_TYPE_DASHBOARD,
                                             DASHBOARD_FW_MAJOR, DASHBOARD_FW_MINOR, DASHBOARD_FW_PATCH);
        }

        bk_network_provisioning_init(BK_NETWORK_PROVISIONING_TYPE_BLE);
        /*
         * Register Wi-Fi and netif callbacks on the SDK path so the network-ready
         * hook fires for STA got-IP and AP/P2P GO client connections. The netif
         * callback does not re-notify the phone with BOARDING_OP_CONFIG_WIFI_STA
         * (already sent by demo_network_provisioning_status_cb() on SUCCEED).
         */
        bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL, demo_wifi_event_cb, NULL);
        bk_event_register_cb(EVENT_MOD_NETIF, EVENT_ID_ALL, demo_np_netif_event_cb, NULL);
        //cli_network_provisioning_init();
        s_send = bk_ble_provisioning_event_notify;
        s_send_data = bk_ble_provisioning_event_notify_with_data;
    }
#endif
    else
    {
        LOGE("%s invalid reg method %d\n", __func__, reg_method);
        return BK_FAIL;
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
