#include <common/bk_include.h>
#include "cli.h"
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>
#include <getopt.h>

#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "net.h"
#include "string.h"
#include <components/netif.h>

#include <common/bk_generic.h>

#include "media_comm.h"
#include "media_cmd.h"
#include "media_msg.h"

#include "cli.h"

#define TAG "media-cmd"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)


typedef struct
{
    uint32_t server_state : 1;
    struct sockaddr_in socket;
    beken_thread_t thread;
    int server_fd;
    int client_fd;
    beken_mutex_t tx_lock;
    beken_timer_t timer;
    uint32_t intval_ms;
    in_addr_t remote_address;
} media_cmd_info_t;


media_cmd_info_t *media_cmd_info = NULL;

static void media_cmd_server_thread(beken_thread_arg_t data)
{
    int rcv_len = 0;
    //  struct sockaddr_in server;
    bk_err_t ret = BK_OK;
    u8 *rcv_buf = NULL;
    fd_set watchfd;

    LOGI("%s entry\n", __func__);
    (void)(data);

    rcv_buf = (u8 *) os_malloc((MEDIA_CMD_BUFFER + 1) * sizeof(u8));
    if (!rcv_buf)
    {
        LOGE("tcp os_malloc failed\n");
        goto out;
    }

    // for data transfer
    media_cmd_info->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (media_cmd_info->server_fd == -1)
    {
        LOGE("socket failed\n");
        goto out;
    }

    media_cmd_info->socket.sin_family = AF_INET;
    media_cmd_info->socket.sin_port = htons(AV_SERVER_CMD_PORT);
    media_cmd_info->socket.sin_addr.s_addr = inet_addr("0.0.0.0");

    if (bind(media_cmd_info->server_fd, (struct sockaddr *)&media_cmd_info->socket, sizeof(struct sockaddr_in)) == -1)
    {
        LOGE("bind failed\n");
        goto out;
    }

    if (listen(media_cmd_info->server_fd, 0) == -1)
    {
        LOGE("listen failed\n");
        goto out;
    }

    LOGI("%s: start listen \n", __func__);

    while (1)
    {
        FD_ZERO(&watchfd);
        FD_SET(media_cmd_info->server_fd, &watchfd);

        LOGI("waiting for a new connection\n");

        ret = select(media_cmd_info->server_fd + 1, &watchfd, NULL, NULL, NULL);

        if (ret <= 0)
        {
            LOGE("select ret:%d\n", ret);
            continue;
        }
        else
        {
            // is new connection
            if (FD_ISSET(media_cmd_info->server_fd, &watchfd))
            {
                struct sockaddr_in client_addr;
                socklen_t cliaddr_len = 0;

                cliaddr_len = sizeof(client_addr);

                media_cmd_info->client_fd = accept(media_cmd_info->server_fd, (struct sockaddr *)&client_addr, &cliaddr_len);

                if (media_cmd_info->client_fd < 0)
                {
                    LOGE("accept return fd:%d\n", media_cmd_info->client_fd);
                    break;
                }

                uint8_t *src_ipaddr = (UINT8 *)&client_addr.sin_addr.s_addr;

                LOGI("cmd accept a new connection fd:%d, %d.%d.%d.%d\n", media_cmd_info->client_fd, src_ipaddr[0], src_ipaddr[1],
                     src_ipaddr[2], src_ipaddr[3]);

                media_cmd_info->remote_address = client_addr.sin_addr.s_addr;

                if (media_cmd_info->server_state == BK_FALSE)
                {
                    media_msg_t msg;

                    media_cmd_info->server_state = BK_TRUE;

                    msg.event = MEDIA_EVT_REMOTE_DEVICE_CONNECTED;
                    msg.param = media_cmd_info->remote_address;
                    media_send_msg(&msg);
                }

                while (media_cmd_info->server_state == BK_TRUE)
                {
                    rcv_len = recv(media_cmd_info->client_fd, rcv_buf, MEDIA_CMD_BUFFER, 0);
                    if (rcv_len > 0)
                    {
                        //bk_net_send_data(rcv_buf, rcv_len, TVIDEO_SND_TCP);
                        LOGI("%s, got length: %d\n", __func__, rcv_len);
                    }
                    else
                    {
                        // close this socket
                        LOGD("%s, recv close fd:%d, rcv_len:%d, error:%d\n", __func__, media_cmd_info->client_fd, rcv_len, errno);
                        close(media_cmd_info->client_fd);
                        media_cmd_info->client_fd = -1;

                        if (media_cmd_info->server_state == BK_TRUE)
                        {
                            media_msg_t msg;

                            media_cmd_info->server_state = BK_FALSE;

                            msg.event = MEDIA_EVT_REMOTE_DEVICE_DISCONNECTED;
                            msg.param = BK_OK;
                            media_send_msg(&msg);
                        }
                        break;
                    }

                }
            }
        }
    }
out:

    LOGE("%s exit %d\n", __func__, media_cmd_info->server_state);

    if (rcv_buf)
    {
        os_free(rcv_buf);
        rcv_buf = NULL;
    }

    if (media_cmd_info->server_fd != -1)
    {
        close(media_cmd_info->server_fd);
        media_cmd_info->server_fd = -1;
    }

    media_cmd_info->server_state = BK_FALSE;

    media_cmd_info->thread = NULL;
    rtos_delete_thread(NULL);
}

in_addr_t media_cmd_get_socket_address(void)
{
    return media_cmd_info->remote_address;
}

void av_server_cmd_server_init(void)
{
    bk_err_t ret;

    media_cmd_info = os_malloc(sizeof(media_cmd_info_t));

    if (media_cmd_info == NULL)
    {
        LOGE("malloc media_cmd_info\n");
        return;
    }

    os_memset(media_cmd_info, 0, sizeof(media_cmd_info_t));

    rtos_init_mutex(&media_cmd_info->tx_lock);

    if (!media_cmd_info->thread)
    {
        ret = rtos_create_thread(&media_cmd_info->thread,
                                 4,
                                 "media_cmd_srv",
                                 (beken_thread_function_t)media_cmd_server_thread,
                                 1024 * 3,
                                 (beken_thread_arg_t)NULL);
        if (ret != kNoErr)
        {
            LOGE("Error: failed to create doorbell cmd server: %d\n", ret);
        }
    }
}

