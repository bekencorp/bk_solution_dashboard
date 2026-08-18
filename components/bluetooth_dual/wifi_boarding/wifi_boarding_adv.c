#include "wifi_boarding_adv.h"

#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "dm_gatt.h"

#define TAG "board_adv"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define WIFI_BOARDING_ADV_HANDLE 0
#define WIFI_BOARDING_COMPANY_ID 0x05F0
#define WIFI_BOARDING_SERVICE_DATA_UUID 0xFE01
#define WIFI_BOARDING_PROTO_VERSION 0x01
#define WIFI_BOARDING_ADV_TIMEOUT_MS 4000
#define WIFI_BOARDING_ADV_NAME_MAX 32

static beken_semaphore_t s_adv_sema;
static volatile bool s_adv_started;
static volatile bk_ble_gap_cb_event_t s_expected_event = BK_BLE_GAP_EVT_MAX;
static volatile bk_err_t s_adv_async_result = BK_FAIL;
static uint8_t s_device_type = 0x03;
static uint8_t s_fw_major = 1;
static uint8_t s_fw_minor;
static uint8_t s_fw_patch;

static int32_t wifi_boarding_adv_gap_cb(bk_ble_gap_cb_event_t event,
                                         bk_ble_gap_cb_param_t *param)
{
    if (event == BK_BLE_GAP_ADV_TERMINATED_EVT)
    {
        struct ble_adv_terminate_param *terminated = (typeof(terminated))param;
        if (terminated->adv_instance == WIFI_BOARDING_ADV_HANDLE)
        {
            s_adv_started = false;
        }
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
    }

    if (event != s_expected_event)
    {
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
    }

    switch (event)
    {
        case BK_BLE_GAP_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT:
        case BK_BLE_GAP_EXT_ADV_PARAMS_SET_COMPLETE_EVT:
        case BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT:
        case BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        case BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT:
        case BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT:
            s_adv_async_result = (*(bk_bt_status_t *)param == BK_BT_STATUS_SUCCESS) ? BK_OK : BK_FAIL;
            if (event == BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT)
            {
                s_adv_started = (s_adv_async_result == BK_OK);
            }
            else if ((event == BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT) && (s_adv_async_result == BK_OK))
            {
                s_adv_started = false;
            }
            if (s_adv_sema)
            {
                rtos_set_semaphore(&s_adv_sema);
            }
            return DM_BLE_GAP_APP_CB_RET_PROCESSED;

        default:
            break;
    }

    return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
}

static void wifi_boarding_adv_prepare_wait(bk_ble_gap_cb_event_t expected_event)
{
    while (rtos_get_semaphore(&s_adv_sema, 0) == BK_OK)
    {
    }
    s_adv_async_result = BK_FAIL;
    s_expected_event = expected_event;
}

static bk_err_t wifi_boarding_adv_wait_command(bk_err_t command_ret, const char *step)
{
    if (command_ret != BK_OK)
    {
        s_expected_event = BK_BLE_GAP_EVT_MAX;
        LOGE("%s command failed %d\n", step, command_ret);
        return command_ret;
    }

    bk_err_t ret = rtos_get_semaphore(&s_adv_sema, WIFI_BOARDING_ADV_TIMEOUT_MS);

    s_expected_event = BK_BLE_GAP_EVT_MAX;
    if (ret != BK_OK)
    {
        LOGE("wait %s failed %d\n", step, ret);
        return ret;
    }

    if (s_adv_async_result != BK_OK)
    {
        LOGE("%s completed with controller error\n", step);
    }
    return s_adv_async_result;
}

bk_err_t wifi_boarding_adv_init(void)
{
    bk_err_t ret;

    if (s_adv_sema != NULL)
    {
        return BK_OK;
    }

    ret = rtos_init_semaphore(&s_adv_sema, 1);
    if (ret != BK_OK)
    {
        LOGE("init semaphore failed %d\n", ret);
        return ret;
    }

    ret = bk_dm_prf_gap_add_gap_callback(wifi_boarding_adv_gap_cb);
    if (ret != BK_OK)
    {
        LOGE("register GAP callback failed %d\n", ret);
        rtos_deinit_semaphore(&s_adv_sema);
        s_adv_sema = NULL;
        return ret;
    }

    return BK_OK;
}

void wifi_boarding_adv_set_device_info(uint8_t device_type,
                                        uint8_t fw_major,
                                        uint8_t fw_minor,
                                        uint8_t fw_patch)
{
    s_device_type = device_type;
    s_fw_major = fw_major;
    s_fw_minor = fw_minor;
    s_fw_patch = fw_patch;
}

static bk_err_t wifi_boarding_adv_start_internal(void)
{
    bk_ble_gap_ext_adv_params_t adv_param = {
        .type = BK_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
        .interval_min = 120,
        .interval_max = 160,
        .channel_map = BK_ADV_CHNL_ALL,
        .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        .primary_phy = BK_BLE_GAP_PRI_PHY_1M,
        .secondary_phy = BK_BLE_GAP_PHY_1M,
        .sid = 0,
        .scan_req_notif = 0,
        .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    };
    const bk_ble_gap_ext_adv_t ext_adv = {
        .instance = WIFI_BOARDING_ADV_HANDLE,
        .duration = 0,
        .max_events = 0,
    };
    bk_bd_addr_t identity_addr = {0};
    bk_bd_addr_t random_addr = {0};
    bk_bd_addr_t display_addr = {0};
    uint8_t adv_data[31] = {0};
    uint8_t scan_rsp[31] = {0};
    char adv_name[WIFI_BOARDING_ADV_NAME_MAX] = {0};
    size_t adv_index = 0;
    size_t scan_index = 0;
    size_t name_len;
    bk_err_t ret;

    if (s_adv_started)
    {
        return BK_OK;
    }

    bk_dm_prf_gap_get_identity_addr(identity_addr);
    bk_bluetooth_get_address(display_addr);
    snprintf(adv_name, sizeof(adv_name), "BK_DASHBOARD_%02X%02X%02X", display_addr[0], display_addr[1], display_addr[2]);

    ret = bk_ble_gap_set_device_name(adv_name);
    if (ret != BK_OK)
    {
        LOGE("set device name failed %d\n", ret);
        return ret;
    }

    wifi_boarding_adv_prepare_wait(BK_BLE_GAP_EXT_ADV_PARAMS_SET_COMPLETE_EVT);
    ret = wifi_boarding_adv_wait_command(bk_ble_gap_set_adv_params(WIFI_BOARDING_ADV_HANDLE, &adv_param),
        "adv params");
    if (ret != BK_OK)
    {
        return ret;
    }

    os_memcpy(random_addr, identity_addr, sizeof(random_addr));
    random_addr[0]++;
    random_addr[5] |= 0xc0;
    wifi_boarding_adv_prepare_wait(BK_BLE_GAP_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT);
    ret = wifi_boarding_adv_wait_command(bk_ble_gap_set_adv_rand_addr(WIFI_BOARDING_ADV_HANDLE, random_addr), "random address");
    if (ret != BK_OK)
    {
        return ret;
    }

    adv_data[adv_index++] = 2;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_FLAG;
    adv_data[adv_index++] = 0x06;

    adv_data[adv_index++] = 3;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_SERVICE_DATA;
    adv_data[adv_index++] = WIFI_BOARDING_SERVICE_DATA_UUID & 0xff;
    adv_data[adv_index++] = WIFI_BOARDING_SERVICE_DATA_UUID >> 8;

    adv_data[adv_index++] = 8;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_MANU;
    adv_data[adv_index++] = WIFI_BOARDING_COMPANY_ID & 0xff;
    adv_data[adv_index++] = WIFI_BOARDING_COMPANY_ID >> 8;
    adv_data[adv_index++] = WIFI_BOARDING_PROTO_VERSION;
    adv_data[adv_index++] = s_device_type;
    adv_data[adv_index++] = s_fw_major;
    adv_data[adv_index++] = s_fw_minor;
    adv_data[adv_index++] = s_fw_patch;

    wifi_boarding_adv_prepare_wait(BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT);
    ret = wifi_boarding_adv_wait_command(bk_ble_gap_set_adv_data_raw(WIFI_BOARDING_ADV_HANDLE, adv_index, adv_data), "adv data");
    if (ret != BK_OK)
    {
        return ret;
    }

    name_len = strlen(adv_name);
    if (name_len > sizeof(scan_rsp) - 2)
    {
        name_len = sizeof(scan_rsp) - 2;
    }
    scan_rsp[scan_index++] = (uint8_t)(name_len + 1);
    scan_rsp[scan_index++] = BK_BLE_AD_TYPE_NAME_CMPL;
    os_memcpy(&scan_rsp[scan_index], adv_name, name_len);
    scan_index += name_len;

    wifi_boarding_adv_prepare_wait(BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT);
    ret = wifi_boarding_adv_wait_command(bk_ble_gap_set_scan_rsp_data_raw(WIFI_BOARDING_ADV_HANDLE,scan_index, scan_rsp), "scan response");
    if (ret != BK_OK)
    {
        return ret;
    }

    wifi_boarding_adv_prepare_wait(BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT);
    ret = wifi_boarding_adv_wait_command(bk_ble_gap_adv_start(1, &ext_adv), "adv start");
    if (ret != BK_OK)
    {
        return ret;
    }

    LOGI("advertising as %s\n", adv_name);
    return BK_OK;
}

static bk_err_t wifi_boarding_adv_stop_internal(void)
{
    const uint8_t instance[] = {WIFI_BOARDING_ADV_HANDLE};
    bk_err_t ret;

    if (!s_adv_started)
    {
        return BK_OK;
    }

    wifi_boarding_adv_prepare_wait(BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT);
    ret = wifi_boarding_adv_wait_command(bk_ble_gap_adv_stop(sizeof(instance), (uint8_t *)instance), "adv stop");
    if (ret != BK_OK)
    {
        return ret;
    }

    return BK_OK;
}

bk_err_t wifi_boarding_adv_start(void)
{
    bk_err_t ret = wifi_boarding_adv_init();

    if (ret != BK_OK)
    {
        return ret;
    }

    return wifi_boarding_adv_start_internal();
}

bk_err_t wifi_boarding_adv_stop(void)
{
    bk_err_t ret = wifi_boarding_adv_init();

    if (ret != BK_OK)
    {
        return ret;
    }

    return wifi_boarding_adv_stop_internal();
}

