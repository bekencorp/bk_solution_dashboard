#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_CMD_RSP_SUCCEED "CMDRSP:OK\r\n"
#define CLI_CMD_RSP_ERROR   "CMDRSP:ERROR\r\n"

int cli_video_player_init(void);

#ifdef __cplusplus
}
#endif
