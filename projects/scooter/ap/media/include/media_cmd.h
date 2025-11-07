#pragma once


#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_CMD_BUFFER (1460)

void av_server_cmd_server_init(void);
in_addr_t media_cmd_get_socket_address(void);

#ifdef __cplusplus
}
#endif