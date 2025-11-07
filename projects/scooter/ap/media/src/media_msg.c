#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>

#include <modules/wifi.h>
#include <components/event.h>
#include <components/netif.h>
#include "cli.h"

#include "media_udp_service.h"
#include "media_comm.h"
#include "media_sdp.h"
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

    if (media_info->queue)
    {
        ret = rtos_push_to_queue(&media_info->queue, msg, BEKEN_NO_WAIT);

        if (BK_OK != ret)
        {
            LOGE("%s failed\n", __func__);
            return BK_FAIL;
        }

        return ret;
    }

    return ret;
}

static void media_message_handle(void)
{
    bk_err_t ret = BK_OK;
    media_msg_t msg;

    while (1)
    {
        ret = rtos_pop_from_queue(&media_info->queue, &msg, BEKEN_WAIT_FOREVER);

        if (kNoErr == ret)
        {
            LOGD("######%s, event:%d\n", __func__, msg.event);
            switch (msg.event)
            {
                case MEDIA_EVT_UDP_SERVICE_START:
                {
                    LOGD("MEDIA_EVT_UDP_SERVICE_START\n");
                    media_info->service = MEDIA_SERVICE_LAN_UDP;

                    media_sdp_start("media-udp", AV_SERVER_CMD_PORT, AV_SERVER_UDP_IMG_PORT, AV_SERVER_UDP_AUD_PORT);
                }
                break;

                case MEDIA_EVT_TCP_SERVICE_START:
                {
                    LOGD("MEDIA_EVT_TCP_SERVICE_START\n");
                    media_info->service = MEDIA_SERVICE_LAN_TCP;
                    media_sdp_start("media-tcp", AV_SERVER_CMD_PORT, AV_SERVER_TCP_IMG_PORT, AV_SERVER_TCP_AUD_PORT);
                }
                break;

                case MEDIA_EVT_REMOTE_DEVICE_CONNECTED:
                {
                    LOGD("MEDIA_EVT_REMOTE_DEVICE_CONNECTED\n");

                    if (media_info->service == MEDIA_SERVICE_LAN_UDP)
                    {
                        media_udp_update_remote_address((in_addr_t)msg.param);
                        media_sdp_reload(60000);
                    }
                    else if (media_info->service == MEDIA_SERVICE_LAN_TCP)
                    {
                        media_sdp_reload(60000);
                    }
                }
                break;

                case MEDIA_EVT_REMOTE_DEVICE_DISCONNECTED:
                {
                    LOGD("MEDIA_EVT_REMOTE_DEVICE_DISCONNECTED\n");
                    if (media_info->service == MEDIA_SERVICE_LAN_UDP)
                    {
                        media_sdp_start("media-udp", AV_SERVER_CMD_PORT, AV_SERVER_UDP_IMG_PORT, AV_SERVER_UDP_AUD_PORT);
                    }
                    else if (media_info->service == MEDIA_SERVICE_LAN_TCP)
                    {
                        media_sdp_start("media-tcp", AV_SERVER_CMD_PORT, AV_SERVER_TCP_IMG_PORT, AV_SERVER_TCP_AUD_PORT);
                    }
                }
                break;

                case MEDIA_EVT_IMAGE_TCP_SERVICE_DISCONNECTED:
                {
                    LOGD("MEDIA_EVT_IMAGE_TCP_SERVICE_DISCONNECTED\n");

                    if (media_info->service == MEDIA_SERVICE_LAN_UDP)
                    {
                        media_sdp_start("media-udp", AV_SERVER_CMD_PORT, AV_SERVER_UDP_IMG_PORT, AV_SERVER_UDP_AUD_PORT);
                    }
                    else if (media_info->service == MEDIA_SERVICE_LAN_TCP)
                    {
                        media_sdp_start("media-tcp", AV_SERVER_CMD_PORT, AV_SERVER_TCP_IMG_PORT, AV_SERVER_TCP_AUD_PORT);
                    }
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

    media_sdp_stop();

    /* delate msg queue */
    ret = rtos_deinit_queue(&media_info->queue);

    if (ret != kNoErr)
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
        LOGE("%s, ceate media message queue failed\n");
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
        goto error;
    }


    media_info->enabled = BK_TRUE;

    LOGD("%s success\n", __func__);

    return;

error:

    LOGE("%s fail\n", __func__);
}

