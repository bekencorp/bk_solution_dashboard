#include "ancs_client.h"

#include <components/log.h>
#include <os/os.h>
#include <os/str.h>
#include <stdio.h>
#include <string.h>

#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_dm_gattc.h"
#include "dm_gatt.h"
#include "dm_gattc.h"

#define TAG                             "ancs"
#define ANCS_ADV_INSTANCE               1
#define ANCS_ADV_TIMEOUT_MS             4000
#define ANCS_INVALID_CONN_ID            0xffff
#define ANCS_CCCD_NOTIFY_ENABLE         0x0001
#define ANCS_COMMAND_BUFFER_SIZE        32
#define ANCS_HID_APPEARANCE             0x03c0
#define ANCS_CONN_INTERVAL_MIN          6
#define ANCS_CONN_INTERVAL_MAX          16
#define ANCS_ADV_COMPANY_ID             0x05F0
#define ANCS_ADV_PROTO_VERSION          0x01

#define ANCS_LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define ANCS_LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define ANCS_LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define ANCS_LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

static const uint8_t s_ancs_service_uuid[16] =
{
    0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
    0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79
};

static const uint8_t s_notification_source_uuid[16] =
{
    0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c,
    0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f
};

static const uint8_t s_control_point_uuid[16] =
{
    0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98,
    0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69
};

static const uint8_t s_data_source_uuid[16] =
{
    0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe,
    0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22
};

static const uint8_t s_hid_service_uuid[16] =
{
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00
};

typedef struct
{
    bool initialized;
    bool active;
    bool advertising;
    bool ancs_found;
    bool ns_subscribed;
    bool ds_subscribed;
    bool pairing_pending;
    bk_gatt_if_t gattc_if;
    uint16_t conn_id;
    uint16_t mtu;
    uint8_t peer_addr[6];
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t notification_source_handle;
    uint16_t control_point_handle;
    uint16_t data_source_handle;
    uint16_t notification_source_cccd;
    uint16_t data_source_cccd;
    bool attr_pending;
    uint32_t pending_uid;
    uint32_t pending_attr_mask;
    uint16_t data_len;
    uint8_t data_buffer[ANCS_REASSEMBLY_SIZE];
    ancs_notification_callback_t notification_callback;
} ancs_client_env_t;

static ancs_client_env_t s_ancs;
static beken_semaphore_t s_adv_sema;
static volatile bk_ble_gap_cb_event_t s_adv_expected_event = BK_BLE_GAP_EVT_MAX;
static volatile bk_err_t s_adv_result = BK_FAIL;
static uint8_t s_device_type = 0x03;
static uint8_t s_fw_major = 1;
static uint8_t s_fw_minor = 0;
static uint8_t s_fw_patch = 0;

static bool ancs_uuid_equal(const bk_bt_uuid_t *uuid, const uint8_t expected[16])
{
    return uuid && uuid->len == BK_UUID_LEN_128 &&
           memcmp(uuid->uuid.uuid128, expected, BK_UUID_LEN_128) == 0;
}

static void ancs_reset_session(void)
{
    bool active = s_ancs.active;
    bool initialized = s_ancs.initialized;
    ancs_notification_callback_t notification_callback = s_ancs.notification_callback;

    memset(&s_ancs, 0, sizeof(s_ancs));
    s_ancs.conn_id = ANCS_INVALID_CONN_ID;
    s_ancs.mtu = 23;
    s_ancs.active = active;
    s_ancs.initialized = initialized;
    s_ancs.notification_callback = notification_callback;
}

static ancs_client_state_t ancs_state(void)
{
    if (s_ancs.ds_subscribed)
    {
        return ANCS_CLIENT_READY;
    }
    if (s_ancs.notification_source_cccd || s_ancs.data_source_cccd)
    {
        return ANCS_CLIENT_SUBSCRIBING;
    }
    if (s_ancs.conn_id != ANCS_INVALID_CONN_ID)
    {
        return s_ancs.ancs_found ? ANCS_CLIENT_DISCOVERING : ANCS_CLIENT_CONNECTED;
    }
    if (s_ancs.advertising)
    {
        return ANCS_CLIENT_ADVERTISING;
    }
    return ANCS_CLIENT_IDLE;
}

static bk_err_t ancs_start_discovery(void)
{
    bk_err_t ret;

    if (s_ancs.conn_id == ANCS_INVALID_CONN_ID)
    {
        return BK_FAIL;
    }

    s_ancs.ancs_found = false;
    s_ancs.ns_subscribed = false;
    s_ancs.ds_subscribed = false;
    s_ancs.service_start_handle = 0;
    s_ancs.service_end_handle = 0;
    s_ancs.notification_source_handle = 0;
    s_ancs.control_point_handle = 0;
    s_ancs.data_source_handle = 0;
    s_ancs.notification_source_cccd = 0;
    s_ancs.data_source_cccd = 0;

    ret = bk_ble_gattc_discover(s_ancs.gattc_if, s_ancs.conn_id, BK_GATT_AUTH_REQ_NONE);
    if (ret != BK_OK)
    {
        ANCS_LOGE("start discovery failed: %d\n", ret);
    }
    else
    {
        ANCS_LOGI("discovering iPhone GATT database\n");
    }
    return ret;
}

static bk_err_t ancs_write_cccd(uint16_t handle, uint16_t value)
{
    return bk_ble_gattc_write_char_descr(s_ancs.gattc_if, s_ancs.conn_id, handle, sizeof(value), (uint8_t *)&value, BK_GATT_WRITE_TYPE_RSP, BK_GATT_AUTH_REQ_NONE);
}

static bk_err_t ancs_subscribe_next(void)
{
    bk_err_t ret;

    if (!s_ancs.ns_subscribed)
    {
        ret = ancs_write_cccd(s_ancs.notification_source_cccd, ANCS_CCCD_NOTIFY_ENABLE);
        ANCS_LOGI("subscribe Notification Source cccd=%u ret=%d\n", s_ancs.notification_source_cccd, ret);
        return ret;
    }

    if (!s_ancs.ds_subscribed)
    {
        ret = ancs_write_cccd(s_ancs.data_source_cccd, ANCS_CCCD_NOTIFY_ENABLE);
        ANCS_LOGI("subscribe Data Source cccd=%u ret=%d\n", s_ancs.data_source_cccd, ret);
        return ret;
    }

    return BK_OK;
}

static void ancs_handle_notification_source(const uint8_t *data, uint16_t len)
{
    ancs_notification_t notification;

    if (ancs_protocol_parse_notification(data, len, &notification) != BK_OK)
    {
        ANCS_LOGW("invalid Notification Source packet len=%u\n", len);
        return;
    }

    ANCS_LOGI("notification event=%s flags=0x%02x category=%s count=%u uid=0x%08x\n", ancs_event_name(notification.event_id), notification.event_flags, ancs_category_name(notification.category_id), notification.category_count, (unsigned)notification.notification_uid);

    if (notification.event_id == ANCS_EVENT_NOTIFICATION_REMOVED)
    {
        return;
    }

    ancs_client_request_attrs(notification.notification_uid);
}

static void ancs_handle_data_source(const uint8_t *data, uint16_t len)
{
    ancs_notification_attrs_t attributes;
    int ret;

    if (!s_ancs.attr_pending)
    {
        ANCS_LOGW("ignore Data Source packet without pending request\n");
        return;
    }

    if (!data || len > (sizeof(s_ancs.data_buffer) - s_ancs.data_len))
    {
        ANCS_LOGE("Data Source overflow current=%u incoming=%u max=%u\n", s_ancs.data_len, len, (unsigned)sizeof(s_ancs.data_buffer));
        s_ancs.attr_pending = false;
        s_ancs.pending_uid = 0;
        s_ancs.data_len = 0;
        return;
    }

    memcpy(&s_ancs.data_buffer[s_ancs.data_len], data, len);
    s_ancs.data_len += len;

    ret = ancs_protocol_parse_attrs(s_ancs.data_buffer, s_ancs.data_len, s_ancs.pending_attr_mask, &attributes);
    if (ret == ANCS_PROTOCOL_MORE)
    {
        return;
    }
    if (ret != BK_OK)
    {
        ANCS_LOGE("invalid Data Source response ret=%d len=%u\n", ret, s_ancs.data_len);
        s_ancs.attr_pending = false;
        s_ancs.pending_uid = 0;
        s_ancs.pending_attr_mask = 0;
        s_ancs.data_len = 0;
        return;
    }

    if (attributes.notification_uid != s_ancs.pending_uid)
    {
        ANCS_LOGW("attribute uid mismatch expected=0x%08x actual=0x%08x\n", (unsigned)s_ancs.pending_uid, (unsigned)attributes.notification_uid);
    }

    ANCS_LOGI("attrs uid=0x%08x app=%s title=%s subtitle=%s message=%s date=%s\n", (unsigned)attributes.notification_uid, attributes.app_identifier, attributes.title, attributes.subtitle, attributes.message, attributes.date);

    s_ancs.attr_pending = false;
    s_ancs.pending_uid = 0;
    s_ancs.pending_attr_mask = 0;
    s_ancs.data_len = 0;
    if (s_ancs.notification_callback)
    {
        s_ancs.notification_callback(&attributes);
    }
}

static int32_t ancs_gattc_cb(bk_gattc_cb_event_t event, bk_gatt_if_t gattc_if, bk_ble_gattc_cb_param_t *param)
{
    if (!s_ancs.initialized || !param)
    {
        return 0;
    }

    switch (event)
    {
        case BK_GATTC_CONNECT_EVT:
            if (param->connect.link_role != 1 || !s_ancs.active)
            {
                ANCS_LOGD("ignore connection conn_id=%u role=%u active=%u\n", param->connect.conn_id, param->connect.link_role, s_ancs.active);
                break;
            }
            ancs_reset_session();
            s_ancs.gattc_if = gattc_if;
            s_ancs.conn_id = param->connect.conn_id;
            memcpy(s_ancs.peer_addr, param->connect.remote_bda, sizeof(s_ancs.peer_addr));
            s_ancs.advertising = false;
            ANCS_LOGI("connected conn_id=%u peer=%02x:%02x:%02x:%02x:%02x:%02x\n", s_ancs.conn_id, s_ancs.peer_addr[5], s_ancs.peer_addr[4], s_ancs.peer_addr[3], s_ancs.peer_addr[2], s_ancs.peer_addr[1], s_ancs.peer_addr[0]);
            if (bk_ble_gattc_send_mtu_req(gattc_if, s_ancs.conn_id) != BK_OK)
            {
                ancs_start_discovery();
            }
            break;

        case BK_GATTC_CFG_MTU_EVT:
            if (param->cfg_mtu.conn_id == s_ancs.conn_id)
            {
                s_ancs.mtu = param->cfg_mtu.mtu;
                ANCS_LOGI("MTU=%u status=0x%x\n", s_ancs.mtu, param->cfg_mtu.status);
                ancs_start_discovery();
            }
            break;

        case BK_GATTC_DIS_RES_SERVICE_EVT:
            if (param->dis_res_service.conn_id != s_ancs.conn_id)
            {
                break;
            }
            for (uint32_t i = 0; i < param->dis_res_service.count; i++)
            {
                if (ancs_uuid_equal(&param->dis_res_service.array[i].srvc_id.uuid, s_ancs_service_uuid))
                {
                    s_ancs.ancs_found = true;
                    s_ancs.service_start_handle = param->dis_res_service.array[i].start_handle;
                    s_ancs.service_end_handle = param->dis_res_service.array[i].end_handle;
                    ANCS_LOGI("ANCS service handles=%u~%u\n", s_ancs.service_start_handle, s_ancs.service_end_handle);
                }
            }
            break;

        case BK_GATTC_DIS_RES_CHAR_EVT:
            if (param->dis_res_char.conn_id != s_ancs.conn_id)
            {
                break;
            }
            for (uint32_t i = 0; i < param->dis_res_char.count; i++)
            {
                uint16_t value_handle = param->dis_res_char.array[i].char_value_handle;
                const bk_bt_uuid_t *uuid = &param->dis_res_char.array[i].uuid.uuid;

                if (value_handle < s_ancs.service_start_handle ||
                    value_handle > s_ancs.service_end_handle)
                {
                    continue;
                }

                if (ancs_uuid_equal(uuid, s_notification_source_uuid))
                {
                    s_ancs.notification_source_handle = value_handle;
                }
                else if (ancs_uuid_equal(uuid, s_control_point_uuid))
                {
                    s_ancs.control_point_handle = value_handle;
                }
                else if (ancs_uuid_equal(uuid, s_data_source_uuid))
                {
                    s_ancs.data_source_handle = value_handle;
                }
            }
            break;

        case BK_GATTC_DIS_RES_CHAR_DESC_EVT:
            if (param->dis_res_char_desc.conn_id != s_ancs.conn_id)
            {
                break;
            }
            for (uint32_t i = 0; i < param->dis_res_char_desc.count; i++)
            {
                bk_bt_uuid_t *uuid = &param->dis_res_char_desc.array[i].uuid.uuid;
                uint16_t char_handle = param->dis_res_char_desc.array[i].char_handle;

                if (uuid->len != BK_UUID_LEN_16 ||
                    uuid->uuid.uuid16 != BK_GATT_UUID_CHAR_CLIENT_CONFIG)
                {
                    continue;
                }

                if (char_handle == s_ancs.notification_source_handle)
                {
                    s_ancs.notification_source_cccd = param->dis_res_char_desc.array[i].desc_handle;
                }
                else if (char_handle == s_ancs.data_source_handle)
                {
                    s_ancs.data_source_cccd = param->dis_res_char_desc.array[i].desc_handle;
                }
            }
            break;

        case BK_GATTC_DIS_SRVC_CMPL_EVT:
            if (param->dis_srvc_cmpl.conn_id != s_ancs.conn_id)
            {
                break;
            }
            ANCS_LOGI("discovery complete status=0x%x ns=%u cp=%u ds=%u ns_cccd=%u ds_cccd=%u\n", param->dis_srvc_cmpl.status, s_ancs.notification_source_handle, s_ancs.control_point_handle, s_ancs.data_source_handle, s_ancs.notification_source_cccd, s_ancs.data_source_cccd);
            if (!s_ancs.ancs_found ||
                !s_ancs.notification_source_handle ||
                !s_ancs.control_point_handle ||
                !s_ancs.data_source_handle ||
                !s_ancs.notification_source_cccd ||
                !s_ancs.data_source_cccd)
            {
                ANCS_LOGW("ANCS is unavailable or incomplete\n");
                break;
            }
            ancs_subscribe_next();
            break;

        case BK_GATTC_WRITE_DESCR_EVT:
            if (param->write.conn_id != s_ancs.conn_id)
            {
                break;
            }
            if (param->write.status == BK_GATT_INSUF_AUTHENTICATION)
            {
                s_ancs.pairing_pending = true;
                ANCS_LOGI("ANCS authorization required, start bonding\n");
                bk_dm_prf_gap_create_bond(s_ancs.peer_addr);
                break;
            }
            if (param->write.status != BK_GATT_OK)
            {
                ANCS_LOGE("CCCD write failed handle=%u status=0x%x\n", param->write.handle, param->write.status);
                break;
            }
            if (param->write.handle == s_ancs.notification_source_cccd)
            {
                s_ancs.ns_subscribed = true;
                ANCS_LOGI("Notification Source subscribed\n");
                ancs_subscribe_next();
            }
            else if (param->write.handle == s_ancs.data_source_cccd)
            {
                s_ancs.ds_subscribed = true;
                ANCS_LOGI("ANCS ready\n");
            }
            break;

        case BK_GATTC_WRITE_CHAR_EVT:
            if (param->write.conn_id == s_ancs.conn_id &&
                param->write.handle == s_ancs.control_point_handle &&
                param->write.status != BK_GATT_OK)
            {
                ANCS_LOGE("Control Point write failed status=0x%x\n", param->write.status);
                s_ancs.attr_pending = false;
                s_ancs.pending_uid = 0;
                s_ancs.data_len = 0;
            }
            break;

        case BK_GATTC_NOTIFY_EVT:
            if (param->notify.conn_id != s_ancs.conn_id)
            {
                break;
            }
            if (param->notify.handle == s_ancs.notification_source_handle)
            {
                ancs_handle_notification_source(param->notify.value, param->notify.value_len);
            }
            else if (param->notify.handle == s_ancs.data_source_handle)
            {
                ancs_handle_data_source(param->notify.value, param->notify.value_len);
            }
            break;

        case BK_GATTC_DISCONNECT_EVT:
            if (param->disconnect.conn_id == s_ancs.conn_id)
            {
                ANCS_LOGI("disconnected reason=0x%x\n", param->disconnect.reason);
                ancs_reset_session();
                s_ancs.active = false;
            }
            break;

        default:
            break;
    }

    return 0;
}

static int32_t ancs_gap_cb(bk_ble_gap_cb_event_t event, bk_ble_gap_cb_param_t *param)
{
    if (!s_ancs.initialized || !param)
    {
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
    }

    if (event == BK_BLE_GAP_AUTH_CMPL_EVT)
    {
        bk_ble_sec_t *security = &param->ble_security;

        if (memcmp(security->auth_cmpl.bd_addr, s_ancs.peer_addr, sizeof(s_ancs.peer_addr)) == 0)
        {
            ANCS_LOGI("bond %s reason=0x%x\n", security->auth_cmpl.success ? "success" : "failed", security->auth_cmpl.fail_reason);
            if (security->auth_cmpl.success && s_ancs.pairing_pending)
            {
                s_ancs.pairing_pending = false;
                ancs_subscribe_next();
            }
        }
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
    }

    if (event == BK_BLE_GAP_ADV_TERMINATED_EVT)
    {
        struct ble_adv_terminate_param *terminated = (typeof(terminated))param;
        if (terminated->adv_instance == ANCS_ADV_INSTANCE)
        {
            s_ancs.advertising = false;
            if (s_ancs.conn_id == ANCS_INVALID_CONN_ID)
            {
                s_ancs.active = false;
            }
        }
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
    }

    if (event != s_adv_expected_event)
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
            s_adv_result = (*(bk_bt_status_t *)param == BK_BT_STATUS_SUCCESS) ? BK_OK : BK_FAIL;
            if (event == BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT && s_adv_result == BK_OK)
            {
                s_ancs.advertising = true;
                s_ancs.active = true;
            }
            if (event == BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT && s_adv_result == BK_OK)
            {
                s_ancs.advertising = false;
                s_ancs.active = false;
            }
            if (s_adv_sema)
            {
                rtos_set_semaphore(&s_adv_sema);
            }
            return DM_BLE_GAP_APP_CB_RET_PROCESSED;

        default:
            return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
    }
}

static bk_err_t ancs_adv_command(bk_err_t command_ret, const char *name)
{
    bk_err_t ret;

    if (command_ret != BK_OK)
    {
        s_adv_expected_event = BK_BLE_GAP_EVT_MAX;
        ANCS_LOGE("%s command failed: %d\n", name, command_ret);
        return command_ret;
    }

    ret = rtos_get_semaphore(&s_adv_sema, ANCS_ADV_TIMEOUT_MS);
    s_adv_expected_event = BK_BLE_GAP_EVT_MAX;
    if (ret != BK_OK)
    {
        ANCS_LOGE("wait %s timeout: %d\n", name, ret);
        return ret;
    }
    return s_adv_result;
}

static void ancs_adv_prepare(bk_ble_gap_cb_event_t event)
{
    while (rtos_get_semaphore(&s_adv_sema, 0) == BK_OK)
    {
    }
    s_adv_result = BK_FAIL;
    s_adv_expected_event = event;
}

bk_err_t ancs_client_adv_start(void)
{
    ANCS_LOGI("start advertising\n");
    bk_ble_gap_ext_adv_params_t params =
    {
        .type = BK_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
        .interval_min = 256,
        .interval_max = 256,
        .channel_map = BK_ADV_CHNL_ALL,
        .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        .primary_phy = BK_BLE_GAP_PRI_PHY_1M,
        .secondary_phy = BK_BLE_GAP_PHY_1M,
        .sid = 1,
        .scan_req_notif = 0,
#if CONFIG_BLUETOOTH_CTKD_BT_TO_BLE || CONFIG_BLUETOOTH_CTKD_BLE_TO_BT
        // use pulic address or use rpa address distribute ID key.
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
#else
        .own_addr_type = BLE_ADDR_TYPE_RANDOM,
#endif
    };
    const bk_ble_gap_ext_adv_t ext_adv =
    {
        .instance = ANCS_ADV_INSTANCE,
        .duration = 0,
        .max_events = 0,
    };
    uint8_t adv_data[31] = {0};
    uint8_t scan_rsp[31] = {0};
    uint8_t local_addr[6] = {0};
    uint8_t random_addr[6] = {0};
    uint8_t identity_addr[6] = {0};
    char name[24] = {0};
    size_t index = 0;
    size_t name_len;
    bk_err_t ret;

    if (!s_ancs.initialized || s_ancs.advertising)
    {
        return s_ancs.advertising ? BK_OK : BK_FAIL;
    }
    if (s_ancs.conn_id != ANCS_INVALID_CONN_ID)
    {
        return BK_FAIL;
    }

    bk_dm_prf_gap_get_identity_addr(identity_addr);
    ret = bk_bluetooth_get_address(local_addr);
    if (ret != BK_OK)
    {
        ANCS_LOGE("get local address failed: %d\n", ret);
        return ret;
    }
    snprintf(name, sizeof(name), "BK_DASHBOARD_%02X%02X%02X", local_addr[0], local_addr[1], local_addr[2]);
    ret = bk_ble_gap_set_device_name(name);
    if (ret != BK_OK)
    {
        ANCS_LOGE("set device name failed: %d\n", ret);
        return ret;
    }

    adv_data[index++] = 2;
    adv_data[index++] = BK_BLE_AD_TYPE_FLAG;
    adv_data[index++] = 0x06;
    adv_data[index++] = 17;
    adv_data[index++] = BK_BLE_AD_TYPE_128SRV_CMPL;
    memcpy(&adv_data[index], s_hid_service_uuid, sizeof(s_hid_service_uuid));
    index += sizeof(s_hid_service_uuid);
    adv_data[index++] = 8;
    adv_data[index++] = BK_BLE_AD_TYPE_MANU;
    adv_data[index++] = ANCS_ADV_COMPANY_ID & 0xff;
    adv_data[index++] = ANCS_ADV_COMPANY_ID >> 8;
    adv_data[index++] = ANCS_ADV_PROTO_VERSION;
    adv_data[index++] = s_device_type;
    adv_data[index++] = s_fw_major;
    adv_data[index++] = s_fw_minor;
    adv_data[index++] = s_fw_patch;

    name_len = os_strlen(name);
    scan_rsp[0] = (uint8_t)(name_len + 1);
    scan_rsp[1] = BK_BLE_AD_TYPE_NAME_CMPL;
    memcpy(&scan_rsp[2], name, name_len);

    ancs_adv_prepare(BK_BLE_GAP_EXT_ADV_PARAMS_SET_COMPLETE_EVT);
    ret = ancs_adv_command(bk_ble_gap_set_adv_params(ANCS_ADV_INSTANCE, &params), "set adv params");
    if (ret != BK_OK)
    {
        ANCS_LOGE("set adv params failed: %d\n", ret);
        return ret;
    }

#if CONFIG_BLUETOOTH_CTKD_BT_TO_BLE || CONFIG_BLUETOOTH_CTKD_BLE_TO_BT
    // use pulic address or use rpa address distribute ID key.
    ancs_adv_prepare(BK_BLE_GAP_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT);
    ret = ancs_adv_command(bk_ble_gap_set_adv_rand_addr(ANCS_ADV_INSTANCE, identity_addr), "identity address");
    if (ret != BK_OK)
    {
        ANCS_LOGE("set identity address failed: %d\n", ret);
        return ret;
    }
#else
    memcpy(random_addr, identity_addr, sizeof(random_addr));
    random_addr[0]++;
    random_addr[5] |= 0xc0;
    ancs_adv_prepare(BK_BLE_GAP_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT);
    ret = ancs_adv_command(bk_ble_gap_set_adv_rand_addr(ANCS_ADV_INSTANCE, random_addr), "random address");
    if (ret != BK_OK)
    {
        ANCS_LOGE("set random address failed: %d\n", ret);
        return ret;
    }
#endif

    ancs_adv_prepare(BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT);
    ret = ancs_adv_command(bk_ble_gap_set_adv_data_raw(ANCS_ADV_INSTANCE, index, adv_data), "set adv data");
    if (ret != BK_OK)
    {
        ANCS_LOGE("set adv data failed: %d\n", ret);
        return ret;
    }

    ancs_adv_prepare(BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT);
    ret = ancs_adv_command(bk_ble_gap_set_scan_rsp_data_raw(ANCS_ADV_INSTANCE, name_len + 2, scan_rsp), "set scan response");
    if (ret != BK_OK)
    {
        ANCS_LOGE("set scan response failed: %d\n", ret);
        return ret;
    }

    ancs_adv_prepare(BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT);
    ret = ancs_adv_command(bk_ble_gap_adv_start(1, &ext_adv), "start advertising");
    if (ret == BK_OK)
    {
        ANCS_LOGI("advertising as %s\n", name);
    }
    else
    {
        ANCS_LOGE("start advertising failed: %d\n", ret);
    }
    return ret;
}

bk_err_t ancs_client_adv_stop(void)
{
    const uint8_t instance[] = {ANCS_ADV_INSTANCE};

    if (!s_ancs.advertising)
    {
        return BK_OK;
    }

    ancs_adv_prepare(BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT);
    return ancs_adv_command(bk_ble_gap_adv_stop(sizeof(instance), (uint8_t *)instance), "stop advertising");
}

bk_err_t ancs_client_request_attrs(uint32_t notification_uid)
{
    uint8_t command[ANCS_COMMAND_BUFFER_SIZE] = {0};
    uint16_t command_len = 0;
    bk_err_t ret;

    if (!s_ancs.ds_subscribed || !s_ancs.control_point_handle)
    {
        return BK_FAIL;
    }
    if (s_ancs.attr_pending)
    {
        ANCS_LOGW("attribute request busy uid=0x%08x\n", (unsigned)s_ancs.pending_uid);
        return BK_FAIL;
    }

    ret = ancs_protocol_build_get_attrs(notification_uid, ANCS_DEFAULT_ATTR_MASK, command, sizeof(command), &command_len);
    if (ret != BK_OK)
    {
        return ret;
    }

    s_ancs.attr_pending = true;
    s_ancs.pending_uid = notification_uid;
    s_ancs.pending_attr_mask = ANCS_DEFAULT_ATTR_MASK;
    s_ancs.data_len = 0;

    ret = bk_ble_gattc_write_char(s_ancs.gattc_if, s_ancs.conn_id, s_ancs.control_point_handle, command_len, command, BK_GATT_WRITE_TYPE_RSP, BK_GATT_AUTH_REQ_NONE);
    if (ret != BK_OK)
    {
        s_ancs.attr_pending = false;
        s_ancs.pending_uid = 0;
        s_ancs.pending_attr_mask = 0;
    }
    return ret;
}

bk_err_t ancs_client_disconnect(void)
{
    if (s_ancs.conn_id == ANCS_INVALID_CONN_ID)
    {
        return BK_OK;
    }
    return bk_dm_prf_gap_disconnect(s_ancs.peer_addr);
}

void ancs_client_get_status(ancs_client_status_t *status)
{
    if (!status)
    {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->state = ancs_state();
    memcpy(status->peer_addr, s_ancs.peer_addr, sizeof(status->peer_addr));
    status->conn_id = s_ancs.conn_id;
    status->mtu = s_ancs.mtu;
    status->notification_source_handle = s_ancs.notification_source_handle;
    status->control_point_handle = s_ancs.control_point_handle;
    status->data_source_handle = s_ancs.data_source_handle;
    status->notification_source_cccd = s_ancs.notification_source_cccd;
    status->data_source_cccd = s_ancs.data_source_cccd;
    status->pending_uid = s_ancs.pending_uid;
    status->attr_pending = s_ancs.attr_pending;
}

bk_err_t ancs_client_init(ancs_notification_callback_t callback)
{
    bk_err_t ret;

    if (s_ancs.initialized)
    {
        return BK_OK;
    }

    memset(&s_ancs, 0, sizeof(s_ancs));
    s_ancs.conn_id = ANCS_INVALID_CONN_ID;
    s_ancs.mtu = 23;
    s_ancs.notification_callback = callback;

    ret = rtos_init_semaphore(&s_adv_sema, 1);
    if (ret != BK_OK)
    {
        return ret;
    }

    s_ancs.initialized = true;
    ret = bk_dm_prf_gattc_add_gattc_callback(ancs_gattc_cb);
    if (ret != BK_OK)
    {
        goto fail;
    }

    ret = bk_dm_prf_gap_add_gap_callback(ancs_gap_cb);
    if (ret != BK_OK)
    {
        goto fail;
    }

    ret = ancs_cli_init();
    if (ret != BK_OK)
    {
        goto fail;
    }

    ANCS_LOGI("ANCS client initialized\n");
    return BK_OK;

fail:
    s_ancs.initialized = false;
    rtos_deinit_semaphore(&s_adv_sema);
    s_adv_sema = NULL;
    return ret;
}
