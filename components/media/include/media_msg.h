#pragma once


#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
	MEDIA_EVT_UDP_SERVICE_START,
	MEDIA_EVT_TCP_SERVICE_START,
	MEDIA_EVT_REMOTE_DEVICE_CONNECTED,
	MEDIA_EVT_REMOTE_DEVICE_DISCONNECTED,
	MEDIA_EVT_IMAGE_TCP_SERVICE_DISCONNECTED,
	MEDIA_EVT_EXIT,
} media_evt_t;

typedef enum
{
	MEDIA_SERVICE_NONE = 0,
	MEDIA_SERVICE_LAN_UDP,
	MEDIA_SERVICE_LAN_TCP,
} media_service_t;


typedef struct
{
	uint32_t event;
	uint32_t param;
} media_msg_t;

bk_err_t media_send_msg(media_msg_t *msg);

void media_msg_init(void);



#ifdef __cplusplus
}
#endif
