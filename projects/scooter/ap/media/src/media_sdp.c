#include <common/bk_include.h>
#include "cli.h"
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>

#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "net.h"
#include "string.h"
#include <components/netif.h>

//#include "media_comm.h"
#include "media_sdp.h"

#define TAG "media-sdp"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef struct
{
	int sock;
	beken_timer_t timer;
	UINT32 intval_ms;
	struct sockaddr_in sock_remote;
	char *sdp_data;
	uint16_t sdp_length;
} media_sdp_t;

extern void ap_set_default_netif(void);
extern uint8_t uap_ip_start_flag;

media_sdp_t *media_sdp = NULL;

static int media_sdp_init_socket(int *fd_ptr, UINT16 local_port)
{
	struct sockaddr_in l_socket;
	int so_broadcast = 1, sock = -1;

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		LOGE("Socket error\n");
		goto sta_udp_exit;
	}

	l_socket.sin_family = AF_INET;
	l_socket.sin_port = htons(local_port);
	l_socket.sin_addr.s_addr = htonl(INADDR_ANY);
	os_memset(&(l_socket.sin_zero), 0, sizeof(l_socket.sin_zero));

	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &so_broadcast, sizeof(so_broadcast)) != 0)
	{
		LOGE("notify socket setsockopt error\n");
		goto sta_udp_exit;
	}

	if (bind(sock, (struct sockaddr *)&l_socket, sizeof(l_socket)) != 0)
	{
		LOGE(" notify socket bind error\n");
		goto sta_udp_exit;
	}

	if (fd_ptr)
	{
		*fd_ptr = sock;
	}

	return 0;

sta_udp_exit:
	if ((sock) != -1)
	{
		closesocket(sock);
		sock = -1;
	}

	return -1;
}

int media_sdp_generate(const char *name, uint32_t cmd_port, uint32_t img_port, uint32_t aud_port)
{
#define ADV_ALLOC_LEN    (1024)

	uint32 adv_len = ADV_ALLOC_LEN;
	const char *adv_temp =                                                                      \
	        "{\"service\":\"%s\", \"cmd\":\"%d\",\"img\":\"%d\", \"aud\":\"%d\"}";

	if (media_sdp->sdp_data == NULL)
	{
		media_sdp->sdp_data = os_malloc(adv_len);

		if (media_sdp->sdp_data == NULL)
		{
			LOGE("no memory\r\n");
			return BK_FAIL;
		}
	}

	os_memset(media_sdp->sdp_data, 0, adv_len);

	if (uap_ip_is_start())
	{
		LOGD("%s, ap mode\n", __func__);
	}
	else if (sta_ip_is_start())
	{
		LOGD("%s, sta mode\n", __func__);
	}

	sprintf(media_sdp->sdp_data, adv_temp,
	        name,
	        (int)cmd_port,
	        (int)img_port,
	        (int)aud_port);

	media_sdp->sdp_length = strlen(media_sdp->sdp_data);

	LOGD("adv_data:%s,%u\r\n", media_sdp->sdp_data, media_sdp->sdp_length);

	return 0;
}

static void media_sdp_timer_handler(void *data)
{
	if (media_sdp == NULL)
	{
		LOGE("media_sdp NULL return");
		return;
	}

	if (media_sdp->sdp_data == NULL
	    || media_sdp->sdp_data == NULL)
	{
		LOGE("media_sdp sdp data NULL return");
		return;
	}

	if (uap_ip_start_flag == 1)
	{
		ap_set_default_netif();
	}

	LOGV("sdp: %s\n", media_sdp->sdp_data);
	sendto(media_sdp->sock,
	       media_sdp->sdp_data,
	       media_sdp->sdp_length,
	       0,
	       (struct sockaddr *)&media_sdp->sock_remote,
	       sizeof(struct sockaddr));

}

int media_sdp_start_timer(UINT32 time_ms)
{
	if (media_sdp)
	{
		int err;
		UINT32 org_ms = media_sdp->intval_ms;

		if (org_ms != 0)
		{
			if ((org_ms != time_ms))
			{
				if (media_sdp->timer.handle != NULL)
				{
					err = rtos_deinit_timer(&media_sdp->timer);
					if (kNoErr != err)
					{
						LOGE("deinit time fail\r\n");
						return kGeneralErr;
					}
					media_sdp->timer.handle = NULL;
				}
			}
			else
			{
				LOGE("timer aready start\r\n");
				return kNoErr;
			}
		}

		err = rtos_init_timer(&media_sdp->timer,
		                      time_ms,
		                      media_sdp_timer_handler,
		                      NULL);
		if (kNoErr != err)
		{
			LOGE("init timer fail\r\n");
			return kGeneralErr;
		}
		media_sdp->intval_ms = time_ms;

		err = rtos_start_timer(&media_sdp->timer);
		if (kNoErr != err)
		{
			LOGE("start timer fail\r\n");
			return kGeneralErr;
		}
		LOGD("media_sdp_start_timer\r\n");

		return kNoErr;
	}
	return kInProgressErr;
}

int media_sdp_stop_timer(void)
{
	if (media_sdp)
	{
		int err;

		err = rtos_stop_timer(&media_sdp->timer);
		if (kNoErr != err)
		{
			LOGE("stop time fail\r\n");
			return kGeneralErr;
		}

		return kNoErr;
	}
	return kInProgressErr;
}

int media_sdp_reload_timer(UINT32 time_ms)
{
	if (media_sdp)
	{
		int err;

		//err = rtos_change_period(&media_sdp->timer, 60000);
		err = rtos_change_period(&media_sdp->timer, time_ms);
		if (kNoErr != err)
		{
			LOGE("change period fail\r\n");
			return kGeneralErr;
		}
		err = rtos_reload_timer(&media_sdp->timer);
		if (kNoErr != err)
		{
			LOGE("change period fail\r\n");
			return kGeneralErr;
		}

		return kNoErr;
	}
	return kInProgressErr;
}

int media_sdp_pub_deinit(void)
{
	LOGD("%s\r\n",__func__);

	if (media_sdp != NULL)
	{
		int err;
		if (media_sdp->timer.handle != NULL)
		{
			err = rtos_deinit_timer(&media_sdp->timer);
			if (kNoErr != err)
			{
				LOGE("deinit time fail\r\n");
				return kGeneralErr;
			}
		}

		closesocket(media_sdp->sock);

		os_free(media_sdp);
		media_sdp = NULL;

		LOGD("%s ok\r\n",__func__);
		return kNoErr;
	}

	return kInProgressErr;
}

int media_sdp_start(const char *name, uint32_t cmd_port, uint32_t img_port, uint32_t aud_port)
{
	LOGD("media_sdp_start\r\n");
	int ret = 0;

	if (media_sdp == NULL)
	{
		media_sdp = os_malloc(sizeof(media_sdp_t));

		if (media_sdp == NULL)
		{
			LOGE("malloc fail\r\n");
			return BK_FAIL;
		}
	}
	else
	{
		LOGE("already init?\r\n");
		media_sdp_reload(1000);
		return BK_FAIL;
	}

	os_memset(media_sdp, 0, sizeof(media_sdp_t));

	media_sdp_generate(name, cmd_port, img_port, aud_port);

	if (media_sdp_init_socket(&media_sdp->sock, UDP_SDP_LOCAL_PORT) != 0)
	{
		LOGE("socket fail\r\n");
		media_sdp = NULL;
		return kGeneralErr;
	}

	media_sdp->sock_remote.sin_family = AF_INET;
	media_sdp->sock_remote.sin_port = htons(UDP_SDP_REMOTE_PORT);
	media_sdp->sock_remote.sin_addr.s_addr = INADDR_BROADCAST;

	if (media_sdp_start_timer(1000) != kNoErr)
	{
		ret = -5;
		goto sdp_int_err;
	}

	LOGD("done\r\n");

	return 0;

sdp_int_err:
	media_sdp_pub_deinit();

	return ret;
}

int media_sdp_stop(void)
{
	LOGD("%s start\n", __func__);

	if (media_sdp_stop_timer() != kNoErr)
	{
		return -1;
	}

	if (media_sdp_pub_deinit() != kNoErr)
	{
		return -2;
	}

	LOGD("%s done\n", __func__);

	return 0;
}

int media_sdp_reload(UINT32 time_ms)
{
	LOGD("%s start\n", __func__);

	if (media_sdp_reload_timer(time_ms) != kNoErr)
	{
		return -1;
	}

	LOGD("%s done\n", __func__);

	return 0;
}

