#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>

#include <modules/wifi.h>
#include <components/event.h>
#include <components/netif.h>
#include "cli.h"

#include "media_devices.h"
#include "media_network_transfer.h"
#include "network_type.h"
#include "network_transfer.h"
#include "media_msg.h"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define TAG "db-core"

typedef struct
{
    uint32_t enabled : 1;
    uint32_t service : 6;

    char *id;
    beken_thread_t thd;
    beken_queue_t queue;
} media_info_t;

media_info_t *media_info = NULL;

bk_err_t media_send_msg(media_msg_t *msg)
{
    bk_err_t ret = BK_OK;

    if (media_info == NULL || media_info->queue == NULL)
    {
        LOGE("%s, media_info or queue is NULL\n", __func__);
        return BK_FAIL;
    }

    ret = rtos_push_to_queue(&media_info->queue, msg, BEKEN_NO_WAIT);

    if (BK_OK != ret)
    {
        LOGE("%s failed\n", __func__);
        return BK_FAIL;
    }

    return ret;
}

bk_err_t media_send_msg_wait(media_msg_t *msg)
{
    bk_err_t ret;

    if (media_info == NULL || media_info->queue == NULL)
    {
        LOGE("%s, media_info or queue is NULL\n", __func__);
        return BK_FAIL;
    }

    ret = rtos_push_to_queue(&media_info->queue, msg, BEKEN_WAIT_FOREVER);
    if (BK_OK != ret)
    {
        LOGE("%s failed\n", __func__);
        return BK_FAIL;
    }

    return BK_OK;
}

static void media_cast_lan_session_teardown(uint32_t reason)
{
    bk_err_t ret;

    LOGI("%s reason=%u\n", __func__, (unsigned)reason);

    media_bk_net_cast_video_alloc_gate(0);

    ret = av_server_jpeg_decode_manager_turn_off();
    if (ret != BK_OK)
    {
        LOGE("turn off jpeg_decode_manager failed\n");
    }

#if CONFIG_LCD_PANEL_USE_800X480
    lvgl_app_exit_navigation();
#endif
    (void)reason;

    if (media_info->service == MEDIA_SERVICE_LAN_UDP)
    {
        ntwk_sdp_start("media-udp", NTWK_TRANS_CMD_PORT, NTWK_TRANS_UDP_VIDEO_PORT, NTWK_TRANS_UDP_AUDIO_PORT);
    }
    else if (media_info->service == MEDIA_SERVICE_LAN_TCP)
    {
        ntwk_sdp_start("media-tcp", NTWK_TRANS_CMD_PORT, NTWK_TRANS_TCP_VIDEO_PORT, NTWK_TRANS_TCP_AUDIO_PORT);
    }
}

static void media_message_handle(void)
{
    bk_err_t ret = BK_OK;
    media_msg_t msg;

    while (1)
    {
        ret = rtos_pop_from_queue(&media_info->queue, &msg, BEKEN_WAIT_FOREVER);

        if (BK_OK == ret)
        {
            LOGD("######%s, event:%d\n", __func__, msg.event);
            switch (msg.event)
            {
                case MEDIA_EVT_UDP_SERVICE_START:
                {
                    LOGD("MEDIA_EVT_UDP_SERVICE_START\n");
                    media_info->service = MEDIA_SERVICE_LAN_UDP;
                    ntwk_sdp_start("media-udp", NTWK_TRANS_CMD_PORT, NTWK_TRANS_UDP_VIDEO_PORT, NTWK_TRANS_UDP_AUDIO_PORT);
                }
                break;

                case MEDIA_EVT_TCP_SERVICE_START:
                {
                    LOGD("MEDIA_EVT_TCP_SERVICE_START\n");
                    media_info->service = MEDIA_SERVICE_LAN_TCP;

                    ret = av_server_devices_init();

                    if (ret != BK_OK)
                    {
                        LOGE("av_server_devices_init failed\n");
                        break;
                    }

                    ntwk_sdp_start("media-tcp", NTWK_TRANS_CMD_PORT, NTWK_TRANS_TCP_VIDEO_PORT, NTWK_TRANS_TCP_AUDIO_PORT);
                }
                break;

                case MEDIA_EVT_REMOTE_DEVICE_CONNECTED:
                {
                    LOGI("MEDIA_EVT_REMOTE_DEVICE_CONNECTED: start jpeg decode + display path\n");
                    #if 0
                    ret = video_data_process_open(480, 272, IMAGE_MJPEG);

                    if (ret != BK_OK)
                    {
                        LOGE("turn on camera failed\n");
                        break;
                    }
                    #endif

                    ret = av_server_jpeg_decode_manager_turn_on();
                    if (ret != BK_OK)
                    {
                        LOGE("turn on cast jpeg pipeline failed\n");
                        break;
                    }

                    #if CONFIG_LCD_PANEL_USE_800X480
                        lvgl_app_enter_navigation();
                    #endif

                    if (media_info->service == MEDIA_SERVICE_LAN_UDP)
                    {
                        ntwk_sdp_reload(60000);
                    }
                    else if (media_info->service == MEDIA_SERVICE_LAN_TCP)
                    {
                        ntwk_sdp_reload(60000);
                    }

                }
                break;

                case MEDIA_EVT_CAST_SESSION_TEARDOWN:
                {
                    LOGD("MEDIA_EVT_CAST_SESSION_TEARDOWN param=%u\n", (unsigned)msg.param);
                    media_cast_lan_session_teardown(msg.param);
                }
                break;

                case MEDIA_EVT_EXIT:
                    goto exit;
                    break;

                default:
                    break;
            }
        }
    }

exit:
    ntwk_sdp_stop();
    /* delate msg queue */
    ret = rtos_deinit_queue(&media_info->queue);

    if (ret != BK_OK)
    {
        LOGE("delate message queue fail\n");
    }

    media_info->queue = NULL;

    LOGE("delate message queue complete\n");

    /* delate task */
    rtos_delete_thread(NULL);

    media_info->thd = NULL;

    LOGE("delate task complete\n");
}


void media_msg_init(void)
{
    bk_err_t ret = BK_OK;

    if (media_info == NULL)
    {
        media_info = os_malloc(sizeof(media_info_t));

        if (media_info == NULL)
        {
            LOGE("%s, malloc media_info failed\n", __func__);
            goto error;
        }

        os_memset(media_info, 0, sizeof(media_info_t));
    }


    if (media_info->queue != NULL)
    {
        ret = BK_FAIL;
        LOGE("%s, media_info->queue allready init, exit!\n", __func__);
        goto error;
    }

    if (media_info->thd != NULL)
    {
        ret = BK_FAIL;
        LOGE("%s, media_info->thd allready init, exit!\n", __func__);
        goto error;
    }

    ret = rtos_init_queue(&media_info->queue,
                          "media_info->queue",
                          sizeof(media_msg_t),
                          10);

    if (ret != BK_OK)
    {
        LOGE("%s, create media message queue failed\n", __func__);
        goto error;
    }

    ret = rtos_create_thread(&media_info->thd,
                                    BEKEN_DEFAULT_WORKER_PRIORITY,
                                    "media_info->thd",
                                    (beken_thread_function_t)media_message_handle,
                                    1024 * 6,
                                    NULL);

    if (ret != BK_OK)
    {
        LOGE("create media major thread fail\n");
        rtos_deinit_queue(&media_info->queue);
        media_info->queue = NULL;
        goto error;
    }


    media_info->enabled = BK_TRUE;

    LOGD("%s success\n", __func__);

    return;

error:

    LOGE("%s fail\n", __func__);
}

