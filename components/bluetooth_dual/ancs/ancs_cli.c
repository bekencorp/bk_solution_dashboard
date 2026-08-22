#include "ancs_client.h"

#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>
#include <stdio.h>

#include "cli.h"
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "dm_gatt.h"

#define TAG "ancs_cli"

static const char *ancs_state_name(ancs_client_state_t state)
{
    switch (state)
    {
        case ANCS_CLIENT_IDLE:
            return "idle";
        case ANCS_CLIENT_ADVERTISING:
            return "advertising";
        case ANCS_CLIENT_CONNECTED:
            return "connected";
        case ANCS_CLIENT_DISCOVERING:
            return "discovering";
        case ANCS_CLIENT_SUBSCRIBING:
            return "subscribing";
        case ANCS_CLIENT_READY:
            return "ready";
        default:
            return "unknown";
    }
}

static void ancs_cli_usage(void)
{
    BK_LOGI(TAG, "Usage:\n");
    BK_LOGI(TAG, "  ancs status\n");
    BK_LOGI(TAG, "  ancs adv <start|stop>\n");
    BK_LOGI(TAG, "  ancs get <uid_hex>\n");
    BK_LOGI(TAG, "  ancs disconnect\n");
    BK_LOGI(TAG, "  ancs bond <list|clear>\n");
}

static void ancs_cli_status(void)
{
    ancs_client_status_t status;

    ancs_client_get_status(&status);
    BK_LOGI(TAG, "state=%s conn_id=%u peer=%02x:%02x:%02x:%02x:%02x:%02x mtu=%u\n", ancs_state_name(status.state), status.conn_id, status.peer_addr[5], status.peer_addr[4], status.peer_addr[3], status.peer_addr[2], status.peer_addr[1], status.peer_addr[0], status.mtu);
    BK_LOGI(TAG, "pending=%u uid=0x%08x\n", status.attr_pending, (unsigned)status.pending_uid);
    BK_LOGI(TAG, "handles ns=%u cp=%u ds=%u ns_cccd=%u ds_cccd=%u\n", status.notification_source_handle, status.control_point_handle, status.data_source_handle, status.notification_source_cccd, status.data_source_cccd);
}

static void ancs_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char *response = CLI_CMD_RSP_SUCCEED;
    bk_err_t ret = BK_OK;

    (void)xWriteBufferLen;

    if (argc < 2)
    {
        ancs_cli_usage();
        goto error;
    }

    if (!os_strcmp(argv[1], "status"))
    {
        ancs_cli_status();
    }
    else if (!os_strcmp(argv[1], "adv") && argc == 3)
    {
        if (!os_strcmp(argv[2], "start"))
        {
            ret = ancs_client_adv_start();
        }
        else if (!os_strcmp(argv[2], "stop"))
        {
            ret = ancs_client_adv_stop();
        }
        else
        {
            goto error;
        }
    }
    else if (!os_strcmp(argv[1], "get") && argc == 3)
    {
        uint32_t uid = (uint32_t)os_strtoul(argv[2], NULL, 16);
        ret = ancs_client_request_attrs(uid);
    }
    else if (!os_strcmp(argv[1], "disconnect") && argc == 2)
    {
        ret = ancs_client_disconnect();
    }
    else if (!os_strcmp(argv[1], "bond") && argc == 3)
    {
        if (!os_strcmp(argv[2], "list"))
        {
            ret = bk_dm_prf_gap_show_bond_list();
        }
        else if (!os_strcmp(argv[2], "clear"))
        {
            ret = bk_dm_prf_gap_clean_bond();
        }
        else
        {
            goto error;
        }
    }
    else
    {
        ancs_cli_usage();
        goto error;
    }

    if (ret != BK_OK)
    {
        BK_LOGE(TAG, "command failed: %d\n", ret);
        goto error;
    }

    os_memcpy(pcWriteBuffer, response, os_strlen(response));
    return;

error:
    response = CLI_CMD_RSP_ERROR;
    os_memcpy(pcWriteBuffer, response, os_strlen(response));
}

static const struct cli_command s_ancs_commands[] =
{
    {"ancs", "Apple Notification Center Service client", ancs_cli_cmd},
};

bk_err_t ancs_cli_init(void)
{
    return cli_register_commands(s_ancs_commands, sizeof(s_ancs_commands) / sizeof(s_ancs_commands[0]));
}
