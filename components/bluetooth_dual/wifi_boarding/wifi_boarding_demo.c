#include <common/sys_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include "wifi_boarding_demo.h"
#include "dm_gatts.h"

#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_gatt_types.h"
#include "components/bluetooth/bk_dm_gatts.h"
#include "components/bluetooth/bk_dm_bluetooth.h"


#if WIFI_BOARDING_DEMO_ENABLE

typedef struct
{
    uint8_t status; //0 idle 1 connected
    beken_semaphore_t server_sem;
    uint16_t send_notify_status;
} wifi_boarding_app_env_t;

#define PROFILE_ID 1
#define MIN_VALUE(x, y) (((x) < (y)) ? (x): (y))
#define BOARDING_MAX_SSID_LEN 32
#define BOARDING_MAX_PASSWORD_LEN 64

static ble_boarding_info_t *s_ble_boarding_info = NULL;

static uint16_t s_prop_cli_config;
static uint8_t s_ssid[BOARDING_MAX_SSID_LEN + 1];
static uint8_t s_password[BOARDING_MAX_PASSWORD_LEN + 1];
static uint8_t s_wifi_boarding_is_init;
static uint8_t s_db_init;
static uint8_t s_log_level = BOARDING_DEBUG_LEVEL_INFO;

enum
{
    BOARDING_IDX_SVC,
    BOARDING_IDX_CHAR1,
    BOARDING_IDX_CHAR1_DESC,
    BOARDING_IDX_CHAR_OPERATION,
    BOARDING_IDX_CHAR_SSID,
    BOARDING_IDX_CHAR_PASSWORD,
    BOARDING_IDX_NB,
};

static const bk_gatts_attr_db_t s_gatts_attr_db_service_boarding[] =
{
    {
        BK_GATT_PRIMARY_SERVICE_DECL(0xfa00),
    },

    {
        BK_GATT_CHAR_DECL(0xea01,
                          0, NULL,
                          BK_GATT_CHAR_PROP_BIT_NOTIFY,
                          BK_GATT_PERM_READ,
                          BK_GATT_RSP_BY_APP),
    },
    {
        BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG,
                               sizeof(s_prop_cli_config), (uint8_t *)&s_prop_cli_config,
                               BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                               BK_GATT_RSP_BY_APP),
    },

    //operation
    {
        BK_GATT_CHAR_DECL(0xea02,
                          0, NULL,
                          BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                          BK_GATT_PERM_WRITE,
                          BK_GATT_RSP_BY_APP),
    },

    //ssid
    {
        BK_GATT_CHAR_DECL(0xea05,
                          sizeof(s_ssid), (uint8_t *)s_ssid,
                          BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE,
                          BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                          BK_GATT_AUTO_RSP),
    },

    //password
    {
        BK_GATT_CHAR_DECL(0xea06,
                          sizeof(s_password), (uint8_t *)s_password,
                          BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE,
                          BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                          BK_GATT_AUTO_RSP),
    },
};

static uint16_t s_boarding_attr_handle_list[sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0])];

int wifi_boarding_notify(uint8_t *data, uint16_t length)
{
    int16_t current_conn_id = bk_dm_prf_gap_get_current_conn_id();
    int32_t ret;

    if ((data == NULL) || (length == 0))
    {
        wboard_loge("invalid notify data %p len %u", data, length);
        return BK_ERR_PARAM;
    }

    if (current_conn_id < 0)
    {
        wboard_loge("BLE is disconnected, can not send data !!!");
        return BK_FAIL;
    }

    if ((s_prop_cli_config & 0x0001) == 0)
    {
        wboard_logw("notify disabled by peer");
        return BK_FAIL;
    }

    wboard_logv("notify len %u", length);
    ret = bk_ble_gatts_send_indicate(bk_dm_prf_gatts_get_current_if(),
                                     (uint16_t)current_conn_id,
                                     s_boarding_attr_handle_list[BOARDING_IDX_CHAR1],
                                     length, data, 0);
    if (ret != BK_OK)
    {
        wboard_loge("send notify failed %d handle %d len %u", ret, current_conn_id, length);
        return ret;
    }

    return BK_OK;
}

static int32_t wifi_boarding_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *comm_param)
{
    ble_err_t ret = 0;
    dm_gatt_app_env_t *common_env_tmp = NULL;
    wifi_boarding_app_env_t *app_env_tmp = NULL;

    switch (event)
    {
    case BK_GATTS_CONNECT_EVT:
    {
        struct gatts_connect_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_CONNECT_EVT %d role %d %02X:%02X:%02X:%02X:%02X:%02X", param->conn_id, param->link_role,
                    param->remote_bda[5],
                    param->remote_bda[4],
                    param->remote_bda[3],
                    param->remote_bda[2],
                    param->remote_bda[1],
                    param->remote_bda[0]);

        common_env_tmp = dm_ble_alloc_profile_data_by_addr(PROFILE_ID, param->remote_bda, sizeof(*app_env_tmp), (uint8_t **)&app_env_tmp);

        if (!common_env_tmp)
        {
            wboard_loge("alloc profile data err !!!!");
            break;
        }

        app_env_tmp->status = 1;
        s_prop_cli_config = 0;

        ble_ota_stop_timer();
    }
    break;

    case BK_GATTS_DISCONNECT_EVT:
    {
        struct gatts_disconnect_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_DISCONNECT_EVT %02X:%02X:%02X:%02X:%02X:%02X",
                    param->remote_bda[5],
                    param->remote_bda[4],
                    param->remote_bda[3],
                    param->remote_bda[2],
                    param->remote_bda[1],
                    param->remote_bda[0]);
        s_prop_cli_config = 0;

        boarding_msg_t msg = {
            .event = DBEVT_BLE_DISCONNECTED,
        };
        if (boarding_send_msg(&msg) != BK_OK)
        {
            wboard_loge("queue BLE disconnect failed");
        }

        common_env_tmp = dm_ble_find_app_env_by_addr(param->remote_bda);

        if (!common_env_tmp)
        {
            wboard_loge("cant find app env");
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))dm_ble_find_profile_data_by_profile_id(common_env_tmp, PROFILE_ID);

        if (app_env_tmp)
        {
            if (app_env_tmp->server_sem)
            {
                rtos_deinit_semaphore(&app_env_tmp->server_sem);
                app_env_tmp->server_sem = NULL;
            }
        }

        ble_ota_start_timer();
    }
    break;

    case BK_GATTS_CONF_EVT:
    {
        struct gatts_conf_evt_param *param = (typeof(param))comm_param;
        if (param->status != BK_GATT_OK)
        {
            wboard_loge("notify confirmation failed status=%d handle=%u",
                        param->status, param->handle);
        }
        else
        {
            wboard_logv("notify confirmed handle=%u", param->handle);
        }
    }
    break;

    case BK_GATTS_RESPONSE_EVT:
    {
        wboard_logi("BK_GATTS_RESPONSE_EVT");
    }
    break;

    case BK_GATTS_READ_EVT:
    {
        struct gatts_read_evt_param *param = (typeof(param))comm_param;
        bk_gatt_rsp_t rsp;
        uint16_t final_len = 0;

        memset(&rsp, 0, sizeof(rsp));
        wboard_logi("read attr handle %d need rsp %d", param->handle, param->need_rsp);

        uint8_t *tmp_buff = NULL;
        uint16_t buff_size = 0;
        uint8_t valid = 1;

        if (bk_ble_gatts_get_attr_value(param->handle, &buff_size, &tmp_buff))
        {
            wboard_loge("can't get attr %d buff !!!", param->handle);
            valid = 0;
        }

        if (param->handle < s_boarding_attr_handle_list[1] || param->handle > s_boarding_attr_handle_list[sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0]) - 1])
        {
            wboard_loge("attr handle %d app invalid", param->handle);
            valid = 0;
        }

        wboard_logi("attr handle %d size %d buff %p", param->handle, buff_size, tmp_buff);

        if (param->need_rsp)
        {
            if ((tmp_buff == NULL) || (param->offset > buff_size))
            {
                valid = 0;
                final_len = 0;
            }
            else
            {
                final_len = buff_size - param->offset;
            }

            rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
            rsp.attr_value.handle = param->handle;
            rsp.attr_value.offset = param->offset;

            if (tmp_buff && valid)
            {
                rsp.attr_value.len = final_len;
                rsp.attr_value.value = tmp_buff + param->offset;
            }
            else
            {
                rsp.attr_value.len = 0;
                rsp.attr_value.value = NULL;
            }

            ret = bk_ble_gatts_send_response(gatts_if, param->conn_id, param->trans_id,
                                             (tmp_buff && valid ? BK_GATT_OK : BK_GATT_INSUF_RESOURCE), &rsp);
        }
    }
    break;

    case BK_GATTS_WRITE_EVT:
    {
        struct gatts_write_evt_param *param = (typeof(param))comm_param;
        bk_gatt_rsp_t rsp;
        uint16_t final_len = 0;

        memset(&rsp, 0, sizeof(rsp));

        wboard_logi("write attr handle %d len %d offset %d need rsp %d", param->handle, param->len, param->offset, param->need_rsp);

        uint8_t *tmp_buff = NULL;
        uint16_t buff_size = 0;
        uint8_t valid = 1;
        bool is_operation = (param->handle == s_boarding_attr_handle_list[BOARDING_IDX_CHAR_OPERATION]);

        if (is_operation)
        {
            if ((param->value == NULL) || (param->len < 2) || (param->len == 3))
            {
                valid = 0;
            }
            else if (param->len >= 4)
            {
                uint16_t declared_len =
                    (uint16_t)param->value[2] | ((uint16_t)param->value[3] << 8);
                if (declared_len != (uint16_t)(param->len - 4))
                {
                    valid = 0;
                }
            }
        }

        if (!is_operation && bk_ble_gatts_get_attr_value(param->handle, &buff_size, &tmp_buff))
        {
            wboard_loge("can't get attr %d buff !!!", param->handle);
            valid = 0;
        }

        if (param->handle < s_boarding_attr_handle_list[1] || param->handle > s_boarding_attr_handle_list[sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0]) - 1])
        {
            wboard_loge("attr handle %d app invalid", param->handle);
            valid = 0;
        }

        wboard_logi("handle %d size %d buff %p", param->handle, buff_size, tmp_buff);

        if (param->handle == s_boarding_attr_handle_list[BOARDING_IDX_CHAR1_DESC])
        {
            if ((param->offset != 0) || (param->len != sizeof(s_prop_cli_config)))
            {
                wboard_loge("invalid CCCD write offset %u len %u", param->offset, param->len);
                valid = 0;
            }
            else
            {
                s_prop_cli_config = (uint16_t)param->value[0] |
                                    ((uint16_t)param->value[1] << 8);
            }
        }
        else if (param->handle == s_boarding_attr_handle_list[BOARDING_IDX_CHAR_OPERATION])
        {
            wboard_logi("write boarding op char");
        }
        else if (param->handle == s_boarding_attr_handle_list[BOARDING_IDX_CHAR_SSID])
        {
            if ((param->offset != 0) || (param->len > BOARDING_MAX_SSID_LEN))
            {
                wboard_loge("invalid SSID offset %u len %u", param->offset, param->len);
                valid = 0;
                goto send_write_response;
            }

            if (s_ble_boarding_info->ssid_value)
            {
                os_free(s_ble_boarding_info->ssid_value);
                s_ble_boarding_info->ssid_value = NULL;
                s_ble_boarding_info->ssid_length = 0;
            }

            s_ble_boarding_info->ssid_length = param->len;
            s_ble_boarding_info->ssid_value = os_malloc(param->len + 1);

            if (!s_ble_boarding_info->ssid_value)
            {
                wboard_loge("alloc ssid err");
                valid = 0;
            }
            else
            {
                os_memset(s_ble_boarding_info->ssid_value, 0, param->len + 1);
                os_memcpy((uint8_t *)s_ble_boarding_info->ssid_value, param->value, param->len);

                wboard_logi("received SSID len %u", param->len);
            }
        }
        else if (param->handle == s_boarding_attr_handle_list[BOARDING_IDX_CHAR_PASSWORD])
        {
            if ((param->offset != 0) || (param->len > BOARDING_MAX_PASSWORD_LEN))
            {
                wboard_loge("invalid password offset %u len %u", param->offset, param->len);
                valid = 0;
                goto send_write_response;
            }

            if (s_ble_boarding_info->password_value)
            {
                os_free(s_ble_boarding_info->password_value);
                s_ble_boarding_info->password_value = NULL;
                s_ble_boarding_info->password_length = 0;
            }

            s_ble_boarding_info->password_length = param->len;
            s_ble_boarding_info->password_value = os_malloc(param->len + 1);

            if (!s_ble_boarding_info->password_value)
            {
                wboard_loge("alloc password err");
                valid = 0;
            }
            else
            {
                os_memset(s_ble_boarding_info->password_value, 0, param->len + 1);
                os_memcpy((uint8_t *)s_ble_boarding_info->password_value, param->value, param->len);
                wboard_logi("received password len %u", param->len);
            }
        }
        else
        {
            wboard_loge("invalid write handle %d", param->handle);
            valid = 0;
        }

send_write_response:
        if (param->need_rsp)
        {
            if (is_operation)
            {
                final_len = 0;
            }
            else if ((tmp_buff != NULL) && (param->offset <= buff_size))
            {
                final_len = MIN_VALUE(param->len, buff_size - param->offset);
            }
            else
            {
                final_len = 0;
                valid = 0;
            }

            if (!is_operation && tmp_buff && valid)
            {
                os_memcpy(tmp_buff + param->offset, param->value, final_len);
            }

            rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
            rsp.attr_value.handle = param->handle;
            rsp.attr_value.offset = param->offset;

            if (is_operation)
            {
                rsp.attr_value.len = 0;
                rsp.attr_value.value = NULL;
            }
            else if (tmp_buff && valid)
            {
                rsp.attr_value.len = final_len;
                rsp.attr_value.value = tmp_buff + param->offset;
            }

            ret = bk_ble_gatts_send_response(gatts_if, param->conn_id, param->trans_id, valid ? BK_GATT_OK : BK_GATT_INSUF_RESOURCE, &rsp);
        }

        if (valid && (param->handle == s_boarding_attr_handle_list[BOARDING_IDX_CHAR_OPERATION]))
        {
            uint16_t opcode = 0;
            uint16_t length = 0;
            uint8_t *data = NULL;

            if ((param->value == NULL) || (param->len < 2) || (param->len == 3))
            {
                wboard_loge("invalid operation frame len %u", param->len);
                break;
            }

            opcode = param->value[0] | param->value[1] << 8;
            if (param->len == 2)
            {
                length = 0;
            }
            else
            {
                length = param->value[2] | param->value[3] << 8;
                if (length != (uint16_t)(param->len - 4))
                {
                    wboard_loge("operation length mismatch declared %u actual %u",
                                length, (uint16_t)(param->len - 4));
                    break;
                }
            }

            data = (length > 0) ? &param->value[4] : NULL;

            if (s_ble_boarding_info && s_ble_boarding_info->cb)
            {
                s_ble_boarding_info->cb(opcode, length, data);
            }
            else
            {
                wboard_loge("invalid s_ble_boarding_info");
                break;
            }

#if 0
            uint8_t test_data[20] = {0};
            uint16_t test_data_len = sizeof(test_data) - 2 - 1 - 2;
            os_memcpy(test_data, &opcode, sizeof(opcode));
            test_data[2] = 0;
            os_memcpy(test_data + 3, &test_data_len, sizeof(test_data_len));

            wifi_boarding_notify(test_data, sizeof(test_data));
#endif
        }
    }
    break;

    case BK_GATTS_EXEC_WRITE_EVT:
    {
        struct gatts_exec_write_evt_param *param = (typeof(param))comm_param;
        wboard_logi("exec write");
    }
    break;

    default:
        break;
    }

    return 0;
}

static int32_t wifi_boarding_demo_reg_db(void)
{
    int32_t ret = bk_dm_prf_gatts_reg_db((bk_gatts_attr_db_t *)s_gatts_attr_db_service_boarding,
                                  sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0]),
                                  s_boarding_attr_handle_list,
                                  wifi_boarding_gatts_cb, s_db_init ? 0 : 1);

    if (ret)
    {
        wboard_loge("reg db err");
        return ret;
    }

    for (int i = 0; i < sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0]); ++i)
    {
        wboard_logi("attr handle %d", s_boarding_attr_handle_list[i]);
    }

    return ret;
}

#endif

uint8_t wifi_boarding_demo_get_log_level(void)
{
    return s_log_level;
}

void wifi_boarding_demo_set_log_level(uint8_t level)
{
    s_log_level = level;
}

int32_t wifi_boarding_demo_main(ble_boarding_info_t *info)
{
#if WIFI_BOARDING_DEMO_ENABLE
    int32_t ret = 0;

    if (!bk_dm_prf_gatts_is_init())
    {
        wboard_loge("gatts is not init");
        return -1;
    }

    if (s_wifi_boarding_is_init)
    {
        wboard_loge("already init");
        return -1;
    }

    s_log_level = BOARDING_DEBUG_LEVEL_INFO;
    s_ble_boarding_info = info;
    ret = wifi_boarding_demo_reg_db();
    if (ret != BK_OK)
    {
        s_ble_boarding_info = NULL;
        return ret;
    }

    s_db_init = 1;
    s_wifi_boarding_is_init = 1;
    wboard_logi("done");
#else
    wboard_loge("wifi boarding demo not enable");
#endif
    return 0;
}

int32_t wifi_boarding_demo_deinit(uint8_t deinit_bluetooth_future)
{
#if WIFI_BOARDING_DEMO_ENABLE
    int32_t ret = 0;

    if (!s_wifi_boarding_is_init)
    {
        wboard_loge("already deinit");
        return -1;
    }

    ret = bk_dm_prf_gatts_unreg_db((bk_gatts_attr_db_t *)s_gatts_attr_db_service_boarding);
    if (ret != BK_OK)
    {
        wboard_loge("unregister db failed %d", ret);
        return ret;
    }

    if (deinit_bluetooth_future)
    {
        s_db_init = 0;
    }

    if (s_ble_boarding_info)
    {
        if (s_ble_boarding_info->ssid_value)
        {
            os_free(s_ble_boarding_info->ssid_value);
            s_ble_boarding_info->ssid_value = NULL;
        }
        if (s_ble_boarding_info->password_value)
        {
            os_free(s_ble_boarding_info->password_value);
            s_ble_boarding_info->password_value = NULL;
        }
    }

    s_ble_boarding_info = NULL;
    s_prop_cli_config = 0;
    s_wifi_boarding_is_init = 0;
#endif
    return 0;
}

int32_t wifi_boarding_demo_deinit_because_bluetooth_deinit_future(void)
{
#if WIFI_BOARDING_DEMO_ENABLE
    s_db_init = 0;
#endif
    return 0;
}
