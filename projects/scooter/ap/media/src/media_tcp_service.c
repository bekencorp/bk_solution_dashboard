#include <common/bk_include.h>
//#include "cli.h"
#include <os/mem.h>
#include <os/str.h>

//#include <driver/int.h>
#include <common/bk_err.h>
#include <getopt.h>

#include "lwip/udp.h"
#include "net.h"
#include "string.h"
#include <components/netif.h>
#include <common/bk_generic.h>
#include "media_comm.h"
#include "media_tcp_service.h"
#include "media_transmission.h"
#include "media_devices.h"
#include "media_data_process.h"
#include "lwip/sockets.h"
#include <components/event.h>
#include <modules/wifi_types.h>
#include "wifi_transfer.h"

#include "media_msg.h"

#define TAG "db-tcp"

// TCP keepalive and heartbeat configuration
#define TCP_KEEPALIVE_IDLE_TIME    10   // Start keepalive probe after 10 seconds of no data
#define TCP_KEEPALIVE_INTERVAL     5    // Send probe packet every 5 seconds
#define TCP_KEEPALIVE_COUNT        3    // Consider connection broken after 3 failed probes

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef struct
{
	beken_thread_t img_thd;
	struct sockaddr_in img_remote;
	struct sockaddr_in img_socket;
	int img_fd;
	int img_server_fd;
	uint16_t running : 1;
	uint16_t img_status : 1;
	uint16_t rotate;
	db_channel_t *img_channel;
} db_tcp_service_t;

db_tcp_service_t *db_tcp_service = NULL;
static struct sockaddr_in tcp_video_sender;

int av_server_tcp_img_send_packet(uint8_t *data, uint32_t len)
{
	if (!db_tcp_service->img_status)
	{
		return -1;
	}

	return av_server_socket_sendto(&db_tcp_service->img_fd, (struct sockaddr *)&tcp_video_sender, data, len, -sizeof(db_trans_head_t));
}

int av_server_tcp_img_send_prepare(uint8_t *data, uint32_t length)
{
	av_server_transmission_pack(db_tcp_service->img_channel, data, length);

	return 0;
}

void *av_server_tcp_img_get_tx_buf(void)
{
	if (db_tcp_service == NULL)
	{
		LOGE("%s, service null\n",__func__);
		return NULL;
	}

	if (db_tcp_service->img_channel == NULL)
	{
		LOGE("%s, img_channel null\n",__func__);
		return NULL;
	}

	LOGI("%s, tbuf %p\n", __func__,db_tcp_service->img_channel->tbuf);

	return db_tcp_service->img_channel->tbuf + 1;
}

int av_server_tcp_img_get_tx_size(void)
{
	if (db_tcp_service == NULL)
	{
		LOGE("%s, service null\n",__func__);
		return 0;
	}

	if (db_tcp_service->img_channel == NULL)
	{
		LOGE("%s, img_channel null\n",__func__);
		return 0;
	}

	return db_tcp_service->img_channel->tsize - sizeof(db_trans_head_t);
}

static media_transfer_cb_t av_server_tcp_img_channel =
{
	.send = av_server_tcp_img_send_packet,
	.prepare = av_server_tcp_img_send_prepare,
	.get_tx_buf = av_server_tcp_img_get_tx_buf,
	.get_tx_size = av_server_tcp_img_get_tx_size,
};

static inline void av_server_tcp_video_receiver(db_channel_t *channel, uint16_t sequence, uint16_t flags, uint32_t timestamp, uint8_t sequences, uint8_t *data, uint16_t length)
{
	//wifi_transfer_data_check(data,length);
	video_data_receive_complete(data, length, TVIDEO_SND_TCP);
}


static bk_err_t av_server_wifi_event_cb(void *arg, event_module_t event_module,
					  int event_id, void *event_data)
{
	wifi_event_sta_disconnected_t *sta_disconnected;
	wifi_event_sta_connected_t *sta_connected;
	wifi_event_ap_disconnected_t *ap_disconnected;
	wifi_event_ap_connected_t *ap_connected;
	db_tcp_service_t *tcp_service = (db_tcp_service_t *)arg;

	LOGW("event_id: %d, %d\n", event_id, __LINE__);

	switch (event_id) {
	case EVENT_WIFI_STA_CONNECTED:
		sta_connected = (wifi_event_sta_connected_t *)event_data;
		LOGW("BK STA connected %s\n", sta_connected->ssid);
		break;

	case EVENT_WIFI_STA_DISCONNECTED:
		if (tcp_service && tcp_service->img_status)
		{
			LOGW("BK STA disconnected, close img_fd %d, %d\n", tcp_service->img_fd, __LINE__);
			tcp_service->img_status = false;
			if (tcp_service->img_fd >= 0)
			{
				int fd = tcp_service->img_fd;
				// 触发阻塞 recv 立即返回
				shutdown(fd, SHUT_RDWR);
			}
		}
		sta_disconnected = (wifi_event_sta_disconnected_t *)event_data;
		LOGW("BK STA disconnected, reason(%d)%s\n", sta_disconnected->disconnect_reason,
			sta_disconnected->local_generated ? ", local_generated" : "");
		break;

	case EVENT_WIFI_AP_CONNECTED:
		ap_connected = (wifi_event_ap_connected_t *)event_data;
		LOGW(BK_MAC_FORMAT" connected to BK AP\n", BK_MAC_STR(ap_connected->mac));
		break;

	case EVENT_WIFI_AP_DISCONNECTED:
		ap_disconnected = (wifi_event_ap_disconnected_t *)event_data;
		LOGW(BK_MAC_FORMAT" disconnected from BK AP\n", BK_MAC_STR(ap_disconnected->mac));
		break;

	default:
		LOGW("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}

// Set socket keepalive parameters
static int av_server_tcp_set_keepalive(int sockfd)
{
	int keepalive = 1;
	int keepidle = TCP_KEEPALIVE_IDLE_TIME;
	int keepintvl = TCP_KEEPALIVE_INTERVAL;
	int keepcnt = TCP_KEEPALIVE_COUNT;
	int ret;

	// Enable SO_KEEPALIVE
	ret = setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
	if (ret < 0)
	{
		LOGE("setsockopt SO_KEEPALIVE failed: %d\n", errno);
		return ret;
	}

	// Set keepalive idle time
	ret = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
	if (ret < 0)
	{
		LOGW("setsockopt TCP_KEEPIDLE failed: %d\n", errno);
	}

	// Set keepalive probe interval
	ret = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
	if (ret < 0)
	{
		LOGW("setsockopt TCP_KEEPINTVL failed: %d\n", errno);
	}

	// Set keepalive probe count
	ret = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
	if (ret < 0)
	{
		LOGW("setsockopt TCP_KEEPCNT failed: %d\n", errno);
	}

	LOGI("TCP keepalive enabled: idle=%ds, interval=%ds, count=%d\n", 
		keepidle, keepintvl, keepcnt);

	return BK_OK;
}

static void av_server_image_server_thread(beken_thread_arg_t data)
{
	int rcv_len = 0;
	//	struct sockaddr_in server;
	bk_err_t ret = BK_OK;
	u8 *rcv_buf = NULL;
	fd_set watchfd;

	LOGI("%s entry\n", __func__);
	db_tcp_service_t *tcp_service = (db_tcp_service_t *)data;

	rcv_buf = (u8 *) os_malloc((AV_SERVER_TCP_BUFFER + 1) * sizeof(u8));

	if (!rcv_buf)
	{
		LOGE("tcp os_malloc failed\n");
		goto out;
	}

	// for data transfer
	tcp_service->img_server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_service->img_server_fd == -1)
	{
		LOGE("socket failed\n");
		goto out;
	}

	tcp_service->img_socket.sin_family = AF_INET;
	tcp_service->img_socket.sin_port = htons(AV_SERVER_TCP_IMG_PORT);
	tcp_service->img_socket.sin_addr.s_addr = inet_addr("0.0.0.0");

	tcp_video_sender.sin_family = AF_INET;
	tcp_video_sender.sin_port = htons(AV_SERVER_TCP_IMG_PORT);
	tcp_video_sender.sin_addr.s_addr = inet_addr("192.168.188.100");

	if (bind(tcp_service->img_server_fd, (struct sockaddr *)&tcp_service->img_socket, sizeof(struct sockaddr_in)) == -1)
	{
		LOGE("bind failed\n");
		goto out;
	}

	if (listen(tcp_service->img_server_fd, 0) == -1)
	{
		LOGE("listen failed\n");
		goto out;
	}

	LOGI("%s: start listen \n", __func__);

	while (1)
	{
		FD_ZERO(&watchfd);
		FD_SET(tcp_service->img_server_fd, &watchfd);

		{
			media_msg_t msg;
			msg.event = MEDIA_EVT_TCP_SERVICE_START;
			msg.param = 0;
			media_send_msg(&msg);
		}

		LOGI("waiting for a new connection\n");

		ret = select(tcp_service->img_server_fd + 1, &watchfd, NULL, NULL, NULL);
		if (ret <= 0)
		{
			LOGE("select ret:%d\n", ret);
			continue;
		}
		else
		{
			// is new connection
			if (FD_ISSET(tcp_service->img_server_fd, &watchfd))
			{
				struct sockaddr_in client_addr;
				socklen_t cliaddr_len = 0;

				cliaddr_len = sizeof(client_addr);

				tcp_service->img_fd = accept(tcp_service->img_server_fd, (struct sockaddr *)&client_addr, &cliaddr_len);

				if (tcp_service->img_fd < 0)
				{
					LOGE("accept return fd:%d\n",tcp_service->img_fd);
					break;
				}

				LOGI("accept a new connection fd:%d\n", tcp_service->img_fd);

				// Solution 1: Set TCP keepalive
				av_server_tcp_set_keepalive(tcp_service->img_fd);

				{
					media_msg_t msg;
					msg.event = MEDIA_EVT_REMOTE_DEVICE_CONNECTED;
					msg.param = client_addr.sin_addr.s_addr;
					media_send_msg(&msg);
				}

				ret = video_data_process_open(480, 272, IMAGE_MJPEG);

				if (ret != BK_OK)
				{
					LOGE("turn on camera failed\n");
					goto out;
				}

				ret = av_server_jpeg_decode_manager_turn_on();
				if (ret != BK_OK)
				{
					LOGE("turn on jpeg_decode_manager failed\n");
					goto out;
				}

				lvgl_app_suspend_display();

				tcp_service->img_status = BK_TRUE;

				while (tcp_service->img_status == BK_TRUE)
				{
					rcv_len = recv(tcp_service->img_fd, rcv_buf, AV_SERVER_TCP_BUFFER, 0);
					if (rcv_len > 0)
					{
						LOGV("got video length: %d\n", rcv_len);

						av_server_transmission_unpack(tcp_service->img_channel, rcv_buf, rcv_len, av_server_tcp_video_receiver);
					}
					else
					{
						// Connection closed or error occurred
						int errno_temp = errno;
						// close this socket
						LOGI("vid recv close fd:%d, rcv_len:%d, error_code:%d\n", tcp_service->img_fd, rcv_len, errno);
						close(tcp_service->img_fd);
						tcp_service->img_fd = -1;
	
						ret = av_server_jpeg_decode_manager_turn_off();
						if (ret != BK_OK)
						{
							LOGE("turn off jpeg_decode_manager failed\n");
						}
	
						lvgl_app_resume_display();
	
						ret = video_data_process_close();
	
						if (ret != BK_OK)
						{
							LOGE("turn off camera failed\n");
						}
	
						if(tcp_service->img_channel)
						{
							LOGI("TCP server clear old ccount %d\r\n",tcp_service->img_channel->ccount);
							tcp_service->img_channel->ccount = 0;
						}
	
						media_msg_t msg;
						if (errno_temp == ENOTCONN)
						{
							msg.event = MEDIA_EVT_IMAGE_TCP_SERVICE_DISCONNECTED;
						}
						else
						{
							msg.event = MEDIA_EVT_REMOTE_DEVICE_DISCONNECTED;
						}
						msg.param = BK_OK;
						media_send_msg(&msg);
						break;
					}
				}
			}
		}
	}

out:

	LOGE("%s exit %d\n", __func__, tcp_service->img_status);

	av_server_tcp_service_deinit();

	if (rcv_buf)
	{
		os_free(rcv_buf);
		rcv_buf = NULL;
	}

	if (db_tcp_service->img_server_fd != -1)
	{
		close(db_tcp_service->img_server_fd);
		db_tcp_service->img_server_fd = -1;
	}

	db_tcp_service->img_status = BK_FALSE;

	db_tcp_service->img_thd = NULL;
	rtos_delete_thread(NULL);
}

bk_err_t av_server_tcp_image_server_start(void)
{
	int ret = BK_FAIL;

	LOGD("%s, %d\n", __func__, __LINE__);

	BK_LOG_ON_ERR(bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL, av_server_wifi_event_cb, db_tcp_service));

	if (!db_tcp_service->img_thd)
	{
		ret = rtos_create_thread(&db_tcp_service->img_thd,
								 5,
								 "db_tcp_img_srv",
								 (beken_thread_function_t)av_server_image_server_thread,
								 1024 * 3,
								 (beken_thread_arg_t)db_tcp_service);
	}

	return ret;
}


bk_err_t av_server_tcp_service_init(uint16_t rotate)
{
	int ret;

	if (db_tcp_service != NULL)
	{
		LOGE("db_tcp_service already init\n");
		return BK_FAIL;
	}

	ret = av_server_devices_init();

	if (ret != BK_OK)
	{
		LOGE("av_server_devices_init failed\n");
		goto error;
	}

	db_tcp_service = os_malloc(sizeof(db_tcp_service_t));

	if (db_tcp_service == NULL)
	{
		LOGE("db_udp_service malloc failed\n");
		goto error;
	}

	os_memset(db_tcp_service, 0, sizeof(db_tcp_service_t));

	db_tcp_service->img_channel = av_server_transmission_malloc(AV_SERVER_TCP_NET_BUFFER, AV_SERVER_TCP_NET_BUFFER);

	if (db_tcp_service->img_channel == NULL)
	{
		LOGE("img_channel malloc failed\n");
		goto error;
	}

	db_tcp_service->rotate = rotate;

	av_server_devices_set_camera_transfer_callback(&av_server_tcp_img_channel);

	ret = av_server_tcp_image_server_start();
	if (ret != BK_OK)
	{
		LOGE("failed to create av_server tcp img server %d\n",ret);
		goto error;
	}

	LOGI("db_tcp_service->img_channel %p\n", db_tcp_service->img_channel);

	return BK_OK;

error:

	av_server_tcp_service_deinit();

	return BK_FAIL;

}

void av_server_tcp_service_deinit(void)
{
	GLOBAL_INT_DECLARATION();

	LOGI("%s\n", __func__);

	if (db_tcp_service == NULL)
	{
		LOGE("%s service is NULL\n", __func__);
		return;
	}

	if (db_tcp_service->running)
	{
		video_data_process_close();
		//av_server_display_turn_off();
	}

	av_server_devices_deinit();

	if (db_tcp_service->img_channel)
	{
		os_free(db_tcp_service->img_channel);
		db_tcp_service->img_channel = NULL;
	}

	GLOBAL_INT_DISABLE();
	db_tcp_service->running == 0;
	GLOBAL_INT_RESTORE();

	while (db_tcp_service->img_thd)
	{
		rtos_delay_milliseconds(10);
	}

	os_free(db_tcp_service);
	db_tcp_service = NULL;
}
