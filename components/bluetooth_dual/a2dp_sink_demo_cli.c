#include "cli.h"
#include "components/bluetooth/bk_dm_a2dp.h"
#include "a2dp_sink/a2dp_sink_demo.h"
#include "hfp_hf/hfp_hf_demo.h"
#include "bt_manager.h"
#include "bk_avrcp_ct_service.h"
#include "bluetooth_storage.h"
#if CONFIG_BLE
#include "dm_gatt.h"
#endif

#define MP3_PROMPT_TONE_TEST    (0)

#if CONFIG_MP3_PLAY && MP3_PROMPT_TONE_TEST
#include "bk_mp3_play.h"
#include "a2dp_mp3_tone_test_array.h"
#endif

static void headset_usage(void)
{
    CLI_LOGI("Usage:\n"
             "headset connect XX:XX:XX:XX:XX:XX\n"
             "headset disconnect XX:XX:XX:XX:XX:XX\n"
             "headset play\n"
             "headset pause\n"
             "headset next\n"
             "headset prev\n"
             "headset rewind XX\n"
             "headset fast_forward XX\n"
             "headset vol_up\n"
             "headset vol_down\n"
             "headset set_delay_value XX\n"
             "headset get_delay_value\n"
#if CONFIG_MP3_PLAY && MP3_PROMPT_TONE_TEST
             "headset tone_play xx\n"
#endif
            );

    return;
}

#if CONFIG_MP3_PLAY && MP3_PROMPT_TONE_TEST
static bk_err_t get_mp3_music_ptr(uint32_t mp3_music_id, char **mp3_music_ptr, uint32_t *mp3_music_size)
{
    if (mp3_music_id > A2DP_MP3_TONE_MAX)
    {
        CLI_LOGE("mp3_music_id: %d is not support\n", mp3_music_id);
        return BK_FAIL;
    }
    else
    {
        switch (mp3_music_id)
        {
            case 0:
                *mp3_music_ptr = (char *)a2dp_mp3_tone_0102_44100_16bit_mono;
                *mp3_music_size = sizeof(a2dp_mp3_tone_0102_44100_16bit_mono);
                break;

            case 1:
                *mp3_music_ptr = (char *)a2dp_mp3_tone_0104_44100_16bit_mono;
                *mp3_music_size = sizeof(a2dp_mp3_tone_0104_44100_16bit_mono);
                break;

            case 2:
                *mp3_music_ptr = (char *)a2dp_mp3_tone_0112_44100_16bit_mono;
                *mp3_music_size = sizeof(a2dp_mp3_tone_0112_44100_16bit_mono);
                break;

            case 3:
                *mp3_music_ptr = (char *)a2dp_mp3_tone_0205_44100_16bit_mono;
                *mp3_music_size = sizeof(a2dp_mp3_tone_0205_44100_16bit_mono);
                break;

            default:
                break;
        }
    }

    CLI_LOGE("mp3_music_size: %d ----------\n", *mp3_music_size);

    return BK_OK;
}
#endif

static void cmd_headset_demo(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char *msg = NULL;
    int ret = 0;

    if (argc == 1)
    {
        goto __usage;
    }
    else if (os_strcmp(argv[1], "-h") == 0)
    {
        goto __usage;
    }
    else if (os_strcmp(argv[1], "connect") == 0)
    {
        if (argc >= 3)
        {
            uint8_t mac_final[6] = {0};
            uint32_t mac[6] = {0};

            //sscanf bug: cant detect uint8_t size point
            ret = sscanf(argv[2], "%02x:%02x:%02x:%02x:%02x:%02x", //argv[1], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                         mac + 5,
                         mac + 4,
                         mac + 3,
                         mac + 2,
                         mac + 1,
                         mac);

            if (ret != 6)
            {
                CLI_LOGE("param err, need mac, %d %s\n", ret, argv[1]);
                return;
            }

            for (uint8_t i = 0; i < sizeof(mac_final) / sizeof(mac_final[0]); ++i)
            {
                mac_final[i] = mac[i];
            }


            CLI_LOGI("%s mac %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
                     mac_final[5],
                     mac_final[4],
                     mac_final[3],
                     mac_final[2],
                     mac_final[1],
                     mac_final[0]);


            ret = bk_bt_a2dp_sink_connect(mac_final);

            if (ret)
            {
                goto __error;
            }
        }
        else
        {
            goto __usage;
        }
    }
    else if (os_strcmp(argv[1], "disconnect") == 0)
    {
        if (argc >= 3)
        {
            uint8_t mac_final[6] = {0};
            uint32_t mac[6] = {0};

            //sscanf bug: cant detect uint8_t size point
            ret = sscanf(argv[2], "%02x:%02x:%02x:%02x:%02x:%02x", //argv[1], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                         mac + 5,
                         mac + 4,
                         mac + 3,
                         mac + 2,
                         mac + 1,
                         mac);

            if (ret != 6)
            {
                CLI_LOGE("param err, need mac, %d %s\n", ret, argv[1]);
                return;
            }

            for (uint8_t i = 0; i < sizeof(mac_final) / sizeof(mac_final[0]); ++i)
            {
                mac_final[i] = mac[i];
            }

            CLI_LOGI("%s mac %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
                     mac_final[5],
                     mac_final[4],
                     mac_final[3],
                     mac_final[2],
                     mac_final[1],
                     mac_final[0]);

            ret = bk_bt_a2dp_sink_disconnect(mac_final);

            if (ret)
            {
                goto __error;
            }
        }
        else
        {
            goto __usage;
        }
    }
    else if (os_strcmp(argv[1], "play") == 0)
    {
        ret = bk_avrcp_ct_play();
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "pause") == 0)
    {
        ret = bk_avrcp_ct_pause();
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "prev") == 0)
    {
        ret = bk_avrcp_ct_prev();
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "next") == 0)
    {
        ret = bk_avrcp_ct_next();
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "rewind") == 0)
    {
        int32_t option = 500;

        if (argc >= 3)
        {
            ret = sscanf(argv[2], "%d", &option);

            if (ret != 1)
            {
                CLI_LOGE("param err\n");
                goto __usage;
            }
        }

        ret = bk_avrcp_ct_rewind(option);
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "fast_forward") == 0)
    {
        int32_t option = 500;

        if (argc >= 3)
        {
            ret = sscanf(argv[2], "%d", &option);

            if (ret != 1)
            {
                CLI_LOGE("param err\n");
                goto __usage;
            }
        }

        ret = bk_avrcp_ct_fast_forward(option);
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "vol_up") == 0)
    {
        ret = bk_avrcp_ct_vol_up();
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "vol_down") == 0)
    {
        ret = bk_avrcp_ct_vol_down();
        if (ret)
        {
            goto __error;
        }
    }
    else if (os_strcmp(argv[1], "pair_mode") == 0)
    {
        bk_bt_enter_pairing_mode(1);
    }
    else if (os_strcmp(argv[1], "set_delay_value") == 0)
    {
        if (argc >= 3)
        {
            uint16_t delay_value = os_strtoul(argv[2], NULL, 10) & 0xFFFF;
            ret = bk_bt_a2dp_sink_set_delay_value(delay_value);

            if (ret)
            {
                CLI_LOGE("%s a2dp sink set delay value err %d\n", __func__, ret);
                goto __error;
            }
        }
        else
        {
            goto __usage;
        }
    }
    else if (os_strcmp(argv[1], "get_delay_value") == 0)
    {
        ret = bk_bt_a2dp_sink_get_delay_value();

        if (ret)
        {
            CLI_LOGE("%s a2dp sink set delay value err %d\n", __func__, ret);
            goto __error;
        }
    }
    else if(os_strcmp(argv[1], "get_attr") == 0)
    {
        int32_t attr = 0;

        if (argc >= 3)
        {
            ret = sscanf(argv[2], "%d", &attr);

            if (ret != 1)
            {
                goto __error;
            }
        }

        ret = bk_avrcp_ct_get_attr(attr);
        if (ret)
        {
            goto __error;
        }
    }
    else if(os_strcmp(argv[1], "vr") == 0 && argc >= 3)
    {
        uint8_t enable = 0;

        ret = sscanf(argv[2], "%hhu", &enable);

        if (ret != 1)
        {
            goto __error;
        }

        hfp_demo_vr(enable);
    }
    else if(os_strcmp(argv[1], "dial") == 0 && argc >= 3)
    {
        uint8_t enable = 0;
        uint8_t number[32] = "112";

        ret = sscanf(argv[2], "%hhu", &enable);

        if (ret != 1)
        {
            goto __error;
        }

        if (argc >= 4)
        {
            ret = sscanf(argv[3], "%31s", number);

            if (ret != 1)
            {
                goto __error;
            }
        }

        hfp_demo_dial(enable, number);
    }
    else if(os_strcmp(argv[1], "answer") == 0 && argc >= 3)
    {
        uint8_t accept = 0;
        uint8_t number[32] = "112";

        ret = sscanf(argv[2], "%hhu", &accept);

        if (ret != 1)
        {
            goto __error;
        }

        hfp_demo_answer(accept);
    }
    else if(os_strcmp(argv[1], "hfpcmd") == 0 && argc >= 3)
    {
        uint8_t accept = 0;
        uint8_t cmd[64] = {0};

        ret = sscanf(argv[2], "%63s", cmd);

        if (ret != 1)
        {
            goto __error;
        }

        hfp_demo_cust_cmd(cmd);
    }
    else if(os_strcmp(argv[1], "enable_a2dp_to_aud") == 0)
    {
        uint8_t enable = 1;

        if(argc >= 3)
        {
            ret = sscanf(argv[2], "%hhu", &enable);

            if (ret != 1)
            {
                goto __error;
            }
        }

        a2dp_sink_demo_audio_spk_enable(enable);
    }
    else if(os_strcmp(argv[1], "mix_channel") == 0 && argc >= 3)
    {
        uint8_t enable = 0;
        ret = sscanf(argv[2], "%hhu", &enable);
        if (ret != 1)
        {
            goto __error;
        }
        a2dp_sink_demo_set_mix(enable);
    }
    else if(os_strcmp(argv[1], "debug") == 0)
    {
        //bk_bt_a2dp_sink_demo_debug_info();
    }
#if CONFIG_MP3_PLAY && MP3_PROMPT_TONE_TEST
    else if(os_strcmp(argv[1], "tone_play") == 0)
    {
        /* stop bluetooth audio player service */
        bk_bt_app_a2dp_audio_spk_enable(0);

        /* create mp3 play handle */
        bk_mp3_play_handle_t gl_mp3_play_handle = NULL;
        bk_mp3_play_cfg_t cfg = DEFAULT_MP3_PLAY_CONFIG();
        gl_mp3_play_handle = bk_mp3_play_create(&cfg);
        if (gl_mp3_play_handle == NULL)
        {
            CLI_LOGE("create mp3 play fail\n");
            return;
        }

        /* start mp3 play handle to paly tone */
        bk_mp3_play_start(gl_mp3_play_handle);
        char *temp_mp3_music_ptr = NULL;
        uint32_t temp_mp3_music_size = 0;
        uint32_t music_id = 0;
        if (argc >= 3)
        {
            music_id = os_strtoul((char *)argv[2], NULL, 10);
        }
        get_mp3_music_ptr(music_id, &temp_mp3_music_ptr, &temp_mp3_music_size);
        CLI_LOGE("temp_mp3_music_size: %d\n", temp_mp3_music_size);
        bk_mp3_play_write_data(gl_mp3_play_handle, temp_mp3_music_ptr, temp_mp3_music_size);
        bk_mp3_play_write_data_done(gl_mp3_play_handle);

        /* stop mp3 play handle */
         bk_mp3_play_stop(gl_mp3_play_handle);

         /* destroy mp3 play handle */
         bk_mp3_play_destroy(gl_mp3_play_handle);
         gl_mp3_play_handle = NULL;

         /* enable bluetooth audio player service */
         bk_bt_app_a2dp_audio_spk_enable(1);
    }
#endif
    else if(os_strcmp(argv[1], "clean_bond") == 0)
    {
        CLI_LOGI("clean bluetooth bond info (br linkkey + ble bond)\n");

        bluetooth_storage_clean_linkkey_info();
#if CONFIG_BLE
        bluetooth_storage_clean_ble_key_info();
        if (bk_dm_prf_gap_clean_bond() != 0)
        {
            CLI_LOGW("clean ble bond in host failed, storage cleared\n");
        }
#endif
        bluetooth_storage_sync_to_flash();
    }
    else if(os_strcmp(argv[1], "show_bond_info") == 0)
    {
        extern int32_t bluetooth_storage_linkkey_debug(void);
        bluetooth_storage_linkkey_debug();
    }
    else
    {
        goto __usage;
    }

    msg = CLI_CMD_RSP_SUCCEED;
    os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
    return;

__usage:
    headset_usage();

__error:
    msg = CLI_CMD_RSP_ERROR;
    os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
}

static const struct cli_command s_headset_commands[] =
{
    {"headset", "see -h", cmd_headset_demo},
};

int cli_headset_demo_init(void)
{
    return cli_register_commands(s_headset_commands, sizeof(s_headset_commands) / sizeof(s_headset_commands[0]));
}

