#include "video_player_cli.h"

#include <common/bk_include.h>
#include <bk_private/bk_cli.h>
#include <os/str.h>

#include "cli.h"
#include "video_play_engine_api.h"

static void cli_video_play_engine_cmd(char *write_buffer,
                                      int write_buffer_len,
                                      int argc,
                                      char **argv)
{
    avdk_err_t ret = AVDK_ERR_INVAL;

    if (argc >= 2) {
        if (os_strcmp(argv[1], "start") == 0 && argc >= 3) {
            ret = video_play_engine_api_start(argv[2]);
        } else if (os_strcmp(argv[1], "stop") == 0) {
            ret = video_play_engine_api_stop();
        } else if (os_strcmp(argv[1], "pause") == 0) {
            ret = video_play_engine_api_set_pause(true);
        } else if (os_strcmp(argv[1], "resume") == 0) {
            ret = video_play_engine_api_set_pause(false);
        } else if (os_strcmp(argv[1], "seek") == 0 && argc >= 3) {
            ret = video_play_engine_api_seek(
                (uint64_t)os_strtoul(argv[2], NULL, 10));
        } else if (os_strcmp(argv[1], "vol_up") == 0 && argc >= 3) {
            ret = video_play_engine_api_volume_up(
                (uint8_t)os_strtoul(argv[2], NULL, 10));
        } else if (os_strcmp(argv[1], "vol_down") == 0 && argc >= 3) {
            ret = video_play_engine_api_volume_down(
                (uint8_t)os_strtoul(argv[2], NULL, 10));
        } else if (os_strcmp(argv[1], "mute") == 0 && argc >= 3) {
            ret = video_play_engine_api_set_mute(
                os_strcmp(argv[2], "on") == 0);
        } else if (os_strcmp(argv[1], "audio_track") == 0 && argc >= 3) {
            ret = video_play_engine_api_select_audio_track(
                (uint8_t)os_strtoul(argv[2], NULL, 10));
        }
    }

    const char *response = ret == AVDK_ERR_OK
        ? CLI_CMD_RSP_SUCCEED
        : CLI_CMD_RSP_ERROR;
    if (write_buffer != NULL && write_buffer_len > 0) {
        os_strncpy(write_buffer, response, (size_t)write_buffer_len - 1U);
        write_buffer[write_buffer_len - 1] = '\0';
    }
}

static const struct cli_command s_video_player_commands[] = {
    {
        "video_play_engine",
        "video_play_engine start <file>|stop|pause|resume|seek <ms>|"
        "vol_up <n>|vol_down <n>|mute <on|off>|audio_track <index>",
        cli_video_play_engine_cmd,
    },
};

int cli_video_player_init(void)
{
    return cli_register_commands(
        s_video_player_commands,
        sizeof(s_video_player_commands) / sizeof(s_video_player_commands[0]));
}
