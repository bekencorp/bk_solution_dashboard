#include "wifi_boarding_demo_service.h"
#include "wifi_boarding_demo.h"

#include "components/bluetooth/bk_dm_bluetooth.h"

#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <string.h>
#include "ble_ota.h"

static beken_thread_t s_boarding_thd = NULL;
static beken_queue_t s_boarding_queue = NULL;
static bool s_boarding_ready;

static bk_boarding_info_t *bk_boarding_info = NULL;

static f_ota_t *s_ble_ota = NULL;
static uint8_t *s_ota_data_ptr = NULL;
static beken2_timer_t s_ble_ota_tmr;
static void (*s_cmd_cb)(uint16_t op, uint8_t *data, uint32_t len);
static void (*s_ble_disconnect_cb)(void);

#define BOARDING_QUEUE_DEPTH 100
#define BOARDING_MAX_PAYLOAD 1019

static void ble_ota_timer_hdl(void *param1, void *param2)
{
    if (s_ble_ota == NULL)
    {
        return;
    }
    OTA_FREE(s_ble_ota->magic_code);
    f_ota_fun_ptr->deinit(s_ble_ota);
    OTA_FREE(s_ota_data_ptr);
    wboard_logw("ble disconnect need reboot  !\r\n");
    bk_reboot();
}

void ble_ota_start_timer(void)
{
    rtos_init_oneshot_timer(&s_ble_ota_tmr, CONFIG_BLE_OTA_WAIT_TIMEOUT, ble_ota_timer_hdl, 0, 0);
    rtos_start_oneshot_timer(&s_ble_ota_tmr);

    return;
}

void ble_ota_stop_timer(void)
{
    if (rtos_is_oneshot_timer_init(&s_ble_ota_tmr))
    {
        if (rtos_is_oneshot_timer_running(&s_ble_ota_tmr))
            rtos_stop_oneshot_timer(&s_ble_ota_tmr);
        rtos_deinit_oneshot_timer(&s_ble_ota_tmr);
    }

    return;
}

bk_err_t bk_boarding_event_notify(uint16_t opcode, int status)
{
    uint8_t data[] =
    {
        opcode & 0xFF, opcode >> 8,     /* opcode           */
                              status & 0xFF,                                                          /* status           */
                              0, 0,                                                                   /* payload length   */
    };

    wboard_logi("%d, %d", opcode, status);
    return wifi_boarding_notify(data, sizeof(data));
}

bk_err_t bk_boarding_event_notify_with_data(uint16_t opcode, int status, const char *payload, uint16_t length)
{
    uint8_t data[1024] =
    {
        opcode & 0xFF, opcode >> 8,     /* opcode           */
                              status & 0xFF,                  /* status           */
                              length & 0xFF, length >> 8,     /* payload length   */
                              0,
    };

    if ((length > 1024 - 5) || ((length > 0) && (payload == NULL)))
    {
        wboard_loge("invalid payload %p size %d", payload, length);
        return BK_ERR_PARAM;
    }

    if (length > 0)
    {
        os_memcpy(&data[5], payload, length);
    }

    wboard_logi("%d, %d", opcode, status);
    return wifi_boarding_notify(data, length + 5);
}

bk_err_t boarding_send_msg(boarding_msg_t *msg)
{
    bk_err_t ret = BK_OK;

    if (msg == NULL)
    {
        return BK_FAIL;
    }

    if (s_boarding_queue)
    {
        ret = rtos_push_to_queue(&s_boarding_queue, msg, BEKEN_NO_WAIT);

        if (BK_OK != ret)
        {
            wboard_loge("push queue failed %d", ret);
            return BK_FAIL;
        }

        return ret;
    }
    else
    {
        wboard_loge("queue NULL");
        return BK_FAIL;
    }

}

static boarding_wifi_config_t *boarding_wifi_config_snapshot(const bk_boarding_info_t *source)
{
    boarding_wifi_config_t *copy;

    if (source == NULL)
    {
        return NULL;
    }

    copy = os_zalloc(sizeof(*copy));
    if (copy == NULL)
    {
        return NULL;
    }

    copy->channel = source->channel;
    if (source->boarding_info.ssid_value)
    {
        strncpy(copy->ssid, source->boarding_info.ssid_value,
                sizeof(copy->ssid) - 1);
    }

    if (source->boarding_info.password_value)
    {
        strncpy(copy->password, source->boarding_info.password_value,
                sizeof(copy->password) - 1);
    }

    return copy;
}

static void boarding_msg_release(boarding_msg_t *msg)
{
    if ((msg == NULL) || (msg->param == 0))
    {
        return;
    }

    if (msg->event == DBEVT_OTHER_EVT)
    {
        os_free((void *)msg->param);
    }

    msg->param = 0;
}

void bk_boarding_operation_handle(uint16_t opcode, uint16_t length, uint8_t *data)
{
    boarding_msg_t msg = {0};
    bk_err_t ret;

    wboard_logi("opcode: %04X, length: %u", opcode, length);

    switch (opcode)
    {
    case BOARDING_OP_STATION_START:
    case BOARDING_OP_SOFT_AP_START:
    {
        boarding_wifi_config_t *snapshot = boarding_wifi_config_snapshot(bk_boarding_info);
        if (snapshot == NULL)
        {
            wboard_loge("snapshot Wi-Fi credentials failed");
            bk_boarding_event_notify(opcode, EVT_STATUS_ERROR);
            return;
        }
        msg.event = DBEVT_OTHER_EVT;
        msg.sub_evt = opcode;
        msg.param = (uintptr_t)snapshot;
        msg.length = sizeof(*snapshot);
    }
    break;

    case BOARDING_OP_BLE_DISABLE:
    {
        msg.event = DBEVT_BLE_DISABLE;
        msg.param = 0;
    }
    break;

    case BOARDING_OP_SET_WIFI_CHANNEL:
    {
        if ((data == NULL) || (length < sizeof(uint16_t)))
        {
            wboard_loge("invalid channel payload len %u", length);
            return;
        }
        STREAM_TO_UINT16(bk_boarding_info->channel, data);

        wboard_logi("BOARDING_OP_SET_WIFI_CHANNEL: %u", bk_boarding_info->channel);
        return;
    }

    case BOARDING_OP_OTA_START_DOWNLOAD:
    {
        msg.event = DBEVT_OTA_START_DOWNLOAD;
        msg.length = length;
        OTA_MALLOC_WITHOUT_RETURN(s_ota_data_ptr, length);
        os_memcpy(s_ota_data_ptr, data, length);
        msg.param = (uintptr_t)s_ota_data_ptr;
    }
    break;

    case BOARDING_OP_OTA_DO_DOWNLOADING:
    {
        msg.event = DBEVT_OTA_DO_DOWNLOADING;
        msg.length = length;
        OTA_MALLOC_WITHOUT_RETURN(s_ota_data_ptr, length);
        os_memcpy(s_ota_data_ptr, data, length);
        msg.param = (uintptr_t)s_ota_data_ptr;
    }
    break;

    case BOARDING_OP_OTA_COMPLETE_DOWNLOAD:
    {
        msg.event = DBEVT_OTA_COMPLETE_DOWNLOAD;
        msg.length = length;
        OTA_MALLOC_WITHOUT_RETURN(s_ota_data_ptr, length);
        os_memcpy(s_ota_data_ptr, data, length);
        msg.param = (uintptr_t)s_ota_data_ptr;
    }
    break;

    default:
    {
        uint8_t *payload = NULL;

        wboard_logi("unsupported opcode: 0x%04X, try external call", opcode);
        if ((length > BOARDING_MAX_PAYLOAD) ||
            ((length > 0) && (data == NULL)))
        {
            bk_boarding_event_notify(opcode, EVT_STATUS_ERROR);
            return;
        }

        msg.event = DBEVT_OTHER_EVT;
        msg.sub_evt = opcode;
        msg.length = length;
        if (length > 0)
        {
            payload = os_zalloc((size_t)length + 1);
            if (payload == NULL)
            {
                bk_boarding_event_notify(opcode, EVT_STATUS_ERROR);
                return;
            }
            os_memcpy(payload, data, length);
            msg.param = (uintptr_t)payload;
        }
    }
    break;

    }

    ret = boarding_send_msg(&msg);
    if (ret != BK_OK)
    {
        boarding_msg_release(&msg);
        if (msg.event == DBEVT_OTHER_EVT)
        {
            bk_boarding_event_notify(opcode, EVT_STATUS_ERROR);
        }
    }
}


static void boarding_message_handle(void)
{
    bk_err_t ret = BK_OK;
    boarding_msg_t msg;

    while (1)
    {
        ret = rtos_pop_from_queue(&s_boarding_queue, &msg, BEKEN_WAIT_FOREVER);

        if (BK_OK == ret)
        {
            switch (msg.event)
            {
            case DBEVT_BLE_DISABLE:
            {
#if CONFIG_BLUETOOTH
                bk_bluetooth_deinit();
                wboard_logi("close bluetooth finish!\r\n");
#endif
            }
            break;

            case DBEVT_OTA_START_DOWNLOAD:
            {
                wboard_logw("start ota dl!\r\n");
                OTA_MALLOC_WITHOUT_RETURN(s_ble_ota, sizeof(f_ota_t));
                OTA_MALLOC_WITHOUT_RETURN(s_ble_ota->magic_code, OTA_START_MAGIC_LENGTH);
                if(msg.length == (OTA_START_MAGIC_LENGTH + OTA_STORE_ENTIRE_IMAGE_SIZE))
                {
                    os_memcpy(s_ble_ota->magic_code, (uint8_t *)(msg.param), OTA_START_MAGIC_LENGTH);
                    if(os_memcmp(s_ble_ota->magic_code, OTA_START_MAGIC, OTA_START_MAGIC_LENGTH) == 0)
                    {
                        os_memcpy(&(s_ble_ota->image_size), (uint8_t *)(msg.param + OTA_START_MAGIC_LENGTH), OTA_STORE_ENTIRE_IMAGE_SIZE);
                        wboard_logw("ota_image_size :0x%x!\r\n", s_ble_ota->image_size);
                        if(f_ota_fun_ptr->init(s_ble_ota) == BK_OK)
                        {
                            s_ble_ota->ota_type = OTA_TYPE_BLE;
                            bk_boarding_event_notify(BOARDING_OP_OTA_START_DOWNLOAD, F_OTA_COMM_OK);
                        }
                        else
                        {
                            bk_boarding_event_notify(BOARDING_OP_OTA_START_DOWNLOAD, F_OTA_COMM_DATA_ERROR);
                        }
                    }
                    else
                    {
                        wboard_loge("magic is error!\r\n");
                        bk_boarding_event_notify(BOARDING_OP_OTA_START_DOWNLOAD, F_OTA_START_MAGIC_ERROR);
                    }
                }
                else
                {
                    wboard_loge("length is error!\r\n");
                    bk_boarding_event_notify(BOARDING_OP_OTA_START_DOWNLOAD, F_OTA_COMM_LENGTH_ERROR);
                }
                wboard_logi("s_ble_ota->sequence_number :%d !\r\n",s_ble_ota->curr_sequence_number);
                OTA_FREE(s_ble_ota->magic_code);
                OTA_FREE(s_ota_data_ptr);
            }
            break;

            case DBEVT_OTA_DO_DOWNLOADING:
            {
                wboard_logi("start ota dling!\r\n");
                if(msg.length > 0)
                {
                    os_memcpy(&(s_ble_ota->new_sequence_number), (uint8_t *)msg.param, OTA_SEQUENCE_NUMBER);

                    os_memcpy(s_ble_ota->wr_tmp_buf, (uint8_t *)(msg.param + OTA_SEQUENCE_NUMBER), (msg.length - OTA_SEQUENCE_NUMBER));

                    if(f_ota_fun_ptr->data_process(s_ble_ota, (msg.length - OTA_SEQUENCE_NUMBER), s_ble_ota->ota_type, NULL) == BK_OK)
                    {
                        bk_boarding_event_notify_with_data(BOARDING_OP_OTA_DO_DOWNLOADING, F_OTA_COMM_OK, (char*)&s_ble_ota->curr_sequence_number, msg.length);
                    }
                    else
                    {
                        bk_boarding_event_notify_with_data(BOARDING_OP_OTA_DO_DOWNLOADING, F_OTA_COMM_DATA_ERROR, (char*)&s_ble_ota->new_sequence_number, msg.length);
                    }
                }
                else
                {
                    wboard_loge("dling length is error !\r\n");
                    bk_boarding_event_notify_with_data(BOARDING_OP_OTA_DO_DOWNLOADING, F_OTA_COMM_LENGTH_ERROR, (char*)&s_ble_ota->new_sequence_number, msg.length);
                }

                OTA_FREE(s_ota_data_ptr);
            }
            break;

            case DBEVT_OTA_COMPLETE_DOWNLOAD:
            {
                wboard_logw("complete ota dled!\r\n");
                uint32_t in_crc = 0;

                OTA_MALLOC_WITHOUT_RETURN(s_ble_ota->magic_code, OTA_COMPLETE_MAGIC_LENGTH);
                if(msg.length == (OTA_COMPLETE_MAGIC_LENGTH + OTA_CHECK_CRC_LENGTH))
                {
                    os_memcpy(s_ble_ota->magic_code, (uint8_t *)(msg.param), OTA_COMPLETE_MAGIC_LENGTH);
                    os_memcpy(&in_crc, (uint8_t *)(msg.param + OTA_COMPLETE_MAGIC_LENGTH), OTA_CHECK_CRC_LENGTH);
                    if(os_memcmp(s_ble_ota->magic_code, OTA_COMPLETE_MAGIC, OTA_COMPLETE_MAGIC_LENGTH) == 0)
                    {
                        if(f_ota_fun_ptr->crc(s_ble_ota, in_crc) != BK_OK)
                        {
                            bk_boarding_event_notify(BOARDING_OP_OTA_COMPLETE_DOWNLOAD, F_OTA_COMM_CRC_ERROR);
                            break;
                        }
                        OTA_FREE(s_ble_ota->magic_code);
                        f_ota_fun_ptr->deinit(s_ble_ota);
                        OTA_FREE(s_ble_ota);
                        OTA_FREE(s_ota_data_ptr);
                        bk_boarding_event_notify(BOARDING_OP_OTA_COMPLETE_DOWNLOAD, F_OTA_COMM_OK);
                        wboard_logw("ota success !\r\n");
                        rtos_delay_milliseconds(1000);
                        bk_reboot();
                    }
                    else
                    {
                        wboard_loge("finish magic is error !\r\n");
                        bk_boarding_event_notify(BOARDING_OP_OTA_COMPLETE_DOWNLOAD, F_OTA_START_MAGIC_ERROR);
                    }
                }
                else
                {
                    wboard_loge("length is error !\r\n");
                    bk_boarding_event_notify(BOARDING_OP_OTA_COMPLETE_DOWNLOAD, F_OTA_COMM_LENGTH_ERROR);
                }
                OTA_FREE(s_ble_ota->magic_code);
                OTA_FREE(s_ota_data_ptr);
            }
            break;

            case DBEVT_OTHER_EVT:
            {
                wboard_logi("unknown boarding cmd %d, call external %p", msg.sub_evt, s_cmd_cb);

                if(s_cmd_cb)
                {
                    s_cmd_cb(msg.sub_evt, (uint8_t *)msg.param, msg.length);
                }

                if(msg.param)
                {
                    os_free((void *)msg.param);
                    msg.param = 0;
                }
            }
            break;

            case DBEVT_BLE_DISCONNECTED:
            {
                wboard_logi("DBEVT_BLE_DISCONNECTED");
                if (s_ble_disconnect_cb)
                {
                    s_ble_disconnect_cb();
                }
            }
            break;

            case DBEVT_EXIT:
                goto exit;
                break;

            default:

                break;
            }

            boarding_msg_release(&msg);
        }
    }

exit:
    s_boarding_ready = false;
    while (rtos_pop_from_queue(&s_boarding_queue, &msg, 0) == BK_OK)
    {
        boarding_msg_release(&msg);
    }

    /* delete message queue */
    ret = rtos_deinit_queue(&s_boarding_queue);

    if (ret != BK_OK)
    {
        wboard_loge("delete message queue failed");
    }

    s_boarding_queue = NULL;

    wboard_loge("delete message queue complete");

    s_boarding_thd = NULL;
    wboard_loge("delete task complete");
    rtos_delete_thread(NULL);
}

bk_err_t wifi_boarding_demo_reg_external_cmd(void (*cb)(uint16_t op, uint8_t *data, uint32_t len))
{
    s_cmd_cb = cb;
    return BK_OK;
}

void wifi_boarding_demo_reg_ble_disconnect_cb(void (*cb)(void))
{
    s_ble_disconnect_cb = cb;
}

bk_err_t wifi_boarding_demo_service_main(void)
{
    bk_err_t ret = BK_OK;

    if (s_boarding_queue || s_boarding_thd)
    {
        wboard_logw("service already initialized");
        return s_boarding_ready ? BK_OK : BK_ERR_BUSY;
    }

    s_boarding_ready = false;
    ret = rtos_init_queue(&s_boarding_queue,
                          "boarding_queue",
                          sizeof(boarding_msg_t),
                          BOARDING_QUEUE_DEPTH);

    if (ret != BK_OK)
    {
        wboard_loge("create boarding message queue failed");
        return BK_FAIL;
    }

    ret = rtos_create_thread(&s_boarding_thd,
                             BEKEN_DEFAULT_WORKER_PRIORITY,
                             "boarding_thd",
                             (beken_thread_function_t)boarding_message_handle,
                             2560,
                             NULL);

    if (ret != BK_OK)
    {
        wboard_loge("create boarding major thread fail");
        rtos_deinit_queue(&s_boarding_queue);
        s_boarding_queue = NULL;
        return BK_FAIL;
    }

    if (bk_boarding_info == NULL)
    {
        bk_boarding_info = os_malloc(sizeof(bk_boarding_info_t));

        if (bk_boarding_info == NULL)
        {
            wboard_loge("bk_boarding_info malloc failed\n");
            boarding_msg_t exit_msg = {
                .event = DBEVT_EXIT,
            };
            boarding_send_msg(&exit_msg);
            return BK_FAIL;
        }

        os_memset(bk_boarding_info, 0, sizeof(bk_boarding_info_t));
    }

    bk_boarding_info->boarding_info.cb = bk_boarding_operation_handle;
    ret = wifi_boarding_demo_main(&bk_boarding_info->boarding_info);
    if (ret != BK_OK)
    {
        boarding_msg_t exit_msg = {
            .event = DBEVT_EXIT,
        };
        boarding_send_msg(&exit_msg);
        return ret;
    }

    s_boarding_ready = true;
    return BK_OK;
}

bool wifi_boarding_demo_service_is_ready(void)
{
    return s_boarding_ready;
}
