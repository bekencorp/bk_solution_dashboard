#pragma once


#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
	MEDIA_EVT_UDP_SERVICE_START,
	MEDIA_EVT_TCP_SERVICE_START,
	MEDIA_EVT_REMOTE_DEVICE_CONNECTED,
	/** Unified Wi‑Fi/LAN cast teardown (CTRL/VIDEO TCP disconnect or stale watchdog). param = reason. */
	MEDIA_EVT_CAST_SESSION_TEARDOWN,
	MEDIA_EVT_EXIT,
} media_evt_t;

#define MEDIA_CAST_TEARDOWN_REASON_CTRL_TCP         1u
#define MEDIA_CAST_TEARDOWN_REASON_VIDEO_TCP        2u
#define MEDIA_CAST_TEARDOWN_REASON_STALE_WATCHDOG   3u

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

/** Push to media queue with wait (use for cast teardown so the message is not dropped under load). */
bk_err_t media_send_msg_wait(media_msg_t *msg);

void media_msg_init(void);



#ifdef __cplusplus
}
#endif
