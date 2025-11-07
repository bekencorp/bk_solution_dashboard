#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "hfp_hf_demo.h"

#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_bt_types.h"
#include "components/bluetooth/bk_dm_bt.h"
#include <driver/sbc_types.h>
#include <driver/sbc.h>
#include "components/bluetooth/bk_ble_types.h"
#include "modules/sbc_encoder.h"
#include "components/bluetooth/bk_dm_gap_bt.h"
#include "components/bluetooth/bk_dm_hfp.h"


#include <driver/aud_dac.h>
#include <driver/aud_dac_types.h>
#include "ring_buffer_particle.h"
#include "bk_gpio.h"
#include "bt_manager.h"
#include <modules/pm.h>
#include <driver/pwr_clk.h>

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
#include "blue_audio_player_service.h"
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
#include "components/bk_audio/audio_encoders/sbc_enc.h"
#include "blue_audio_recorder_service.h"
#endif

#include "bluetooth_storage.h"

#define TAG "hfp_client"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define BT_AUDIO_HF_DEMO_MSG_COUNT          (30)
#define SCO_MSBC_SAMPLES_PER_FRAME      120
#define SCO_CVSD_SAMPLES_PER_FRAME      60

#define LOCAL_NAME "soundbar"

#define HF_LOCAL_SPEAKER_WAIT_TIME 2000

#define CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGI("CHECK_NULL fail \n");\
            return;\
        }\
    } while(0)


#define HF_MIC_THREAD_PRI       BEKEN_DEFAULT_WORKER_PRIORITY-1
#define HF_SPEAKER_THREAD_PRI   BEKEN_DEFAULT_WORKER_PRIORITY-1
#define HF_LOCAL_ROLLBACK_TEST        0
#define HF_REMOTE_ROLLBACK_TEST       0
#define HF_AT_TEST 0
#define HF_AT_ENABLE_CMEE 1
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE_SUPPORT_EQ
#define HF_MIC_EQ_ENABLE 0
#endif
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
#define HF_SPEAKER_EQ_ENABLE 0
#endif

enum
{
    BT_AUDIO_MSG_NULL,
    BT_AUDIO_VOICE_START_MSG,
    BT_AUDIO_VOICE_STOP_MSG,
    BT_AUDIO_VOICE_IND_MSG,
    BT_AUDIO_VOICE_TASK_EXIT_MSG,
    BT_AUDIO_VOICE_USER_START_MSG,
};

enum
{
    BT_AUDIO_SPK_TASK_START_VOTE_START = 0,
    BT_AUDIO_SPK_TASK_START_VOTE_HFP = BT_AUDIO_SPK_TASK_START_VOTE_START,
    BT_AUDIO_SPK_TASK_START_VOTE_USER,
    BT_AUDIO_SPK_TASK_START_VOTE_END,
};

typedef struct
{
    uint8_t type;
    uint16_t len;
    char *data;
} bt_audio_hf_demo_msg_t;

typedef struct
{
    uint8_t mic_vol;
    uint8_t spk_vol;
} hfp_ctx_t;

enum
{
    HFP_STATUS_IDLE,
    HFP_STATUS_WAIT_QUERY_CALL,
    HFP_STATUS_WAIT_VGS,
    HFP_STATUS_WAIT_VGM,
    HFP_STATUS_WAIT_CGMI,
    HFP_STATUS_WAIT_CUSTOM,
    HFP_STATUS_WAIT_QUERY_CURRENT_OP,
    HFP_STATUS_WAIT_RETRIEVE_SUB_INFO,
    HFP_STATUS_WAIT_SEND_VTS,
    HFP_STATUS_WAIT_REQ_LAST_TAG_NUM,
    HFP_STATUS_WAIT_NREC,
    HFP_STATUS_WAIT_VR_ENABLE,
    HFP_STATUS_WAIT_VR_DISBLE,
    HFP_STATUS_WAIT_DIAL,
    HFP_STATUS_WAIT_DIAL_HUP,
    HFP_STATUS_WAIT_DIAL_MEM,
    HFP_STATUS_WAIT_DIAL_MEM_HUP,
    HFP_STATUS_WAIT_REDAIL,
    HFP_STATUS_WAIT_REDAIL_HUP,
    HFP_STATUS_WAIT_DONE,
};

static uint8_t bt_audio_hfp_hf_codec = CODEC_VOICE_CVSD;
static uint8_t hfp_peer_addr [ 6 ] = {0};
static uint8_t hfp_profile_peer_addr [ 6 ] = {0};

static uint8_t s_hfp_status_mach = HFP_STATUS_IDLE;

static sbcdecodercontext_t bt_audio_hf_sbc_decoder;
static SbcEncoderContext bt_audio_hf_sbc_encoder;
static beken_queue_t bt_audio_hf_demo_msg_que = NULL;
static beken_thread_t bt_audio_hf_demo_thread_handle = NULL;
static beken_semaphore_t s_audio_player_en_sema = NULL;
static beken_semaphore_t s_connect_sema = NULL;

static uint8_t hf_mic_sco_data [ 1024 ] = {0};
static uint16_t hf_mic_data_count = 0;

volatile uint8_t hf_auido_start = 0;

static beken_thread_t hf_mic_thread_handle = NULL;
static beken_semaphore_t hf_speaker_sema = NULL;
static uint8_t s_hfp_hf_is_inited = 0;
static uint8_t s_hfp_hf_is_iphone = 0;
static uint8_t s_connect_status;
static uint8_t s_bt_manager_index = 0xff;
static beken_semaphore_t hf_mic_speaker_exit_sema = NULL;
#if HF_LOCAL_ROLLBACK_TEST
static uint16_t mic_read_size = 0;
#endif

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
static uint16_t frame_length = 0;
static blue_audio_decoder_type_t decoder_type = BLUE_AUDIO_DECODER_TYPE_PCM;
static blue_audio_player_handle_t gl_audio_player_handle = NULL;
#define PLATFORM_SPK_GAIN_MAX 0x3f // see onboard_mic_stream.h
#define PLATFORM_SPK_GAIN_DEFAULT 0x2d // see onboard_mic_stream.h
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
static blue_audio_recorder_handle_t s_bar_handle;
#endif

#define PLATFORM_MIC_GAIN_MAX 0x2d // see onboard_mic_stream.h
#define PLATFORM_MIC_GAIN_DEFAULT 0x2d // see onboard_mic_stream.h

#define HFP_GAIN_MAX 15 //see hfp protocol

static hfp_ctx_t s_hfp_ctx;

static void mic_task(void *arg);
static int mic_task_init();

int bt_audio_hf_demo_task_init(void);

static bk_err_t bk_bt_dac_set_gain(uint8_t *hfp_mic_vol, uint8_t *hfp_spk_vol)
{
    if(gl_audio_player_handle)
    {
        uint8_t gain = 0;

        if(hfp_spk_vol)
        {
            gain = ((PLATFORM_SPK_GAIN_MAX + 1) * 1.0 / (HFP_GAIN_MAX + 1)) * *hfp_spk_vol;

            LOGI("%s set spk gain 0x%x\n", __func__, gain);
            blue_audio_player_set_volume(gl_audio_player_handle, gain);
        }

        if(hfp_mic_vol)
        {
            gain = ((PLATFORM_SPK_GAIN_MAX + 1) * 1.0 / (HFP_GAIN_MAX + 1)) * *hfp_mic_vol;
            LOGI("%s set mic gain 0x%x\n", __func__, gain);
        }
    }
    else
    {
        LOGE("%s audio play not enable\n", __func__);
    }

    return BK_OK;
}

static void bt_audio_hfp_client_voice_data_ind(const uint8_t *data, uint16_t data_len)
{
    bt_audio_hf_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_hf_demo_msg_t));
    if (bt_audio_hf_demo_msg_que == NULL)
        return;

    demo_msg.data = (char *) psram_malloc(data_len);
    if (demo_msg.data == NULL)
    {
        LOGI("%s, malloc failed\r\n", __func__);
        return;
    }

    os_memcpy(demo_msg.data, data, data_len);
    demo_msg.type = BT_AUDIO_VOICE_IND_MSG;
    demo_msg.len = data_len;

    rc = rtos_push_to_queue(&bt_audio_hf_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);
    if (kNoErr != rc)
    {
        LOGI("%s, send queue failed\r\n", __func__);
        if (demo_msg.data)
        {
            psram_free(demo_msg.data);
        }
    }
}

static void bt_audio_hf_sco_connected(void)
{
    bt_audio_hf_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_hf_demo_msg_t));
    if (bt_audio_hf_demo_msg_que == NULL)
        return;

    demo_msg.type = BT_AUDIO_VOICE_START_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_hf_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);
    if (kNoErr != rc)
    {
        LOGI("%s, send queue failed\r\n", __func__);
    }
}

static void bt_audio_hf_sco_disconnected(void)
{
    bt_audio_hf_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_hf_demo_msg_t));
    if (bt_audio_hf_demo_msg_que == NULL)
        return;

    demo_msg.type = BT_AUDIO_VOICE_STOP_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_hf_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);
    if (kNoErr != rc)
    {
        LOGI("%s, send queue failed\r\n", __func__);
    }
}

static void bt_audio_task_exit(void)
{
    bt_audio_hf_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_hf_demo_msg_t));
    if (bt_audio_hf_demo_msg_que == NULL)
        return;

    demo_msg.type = BT_AUDIO_VOICE_TASK_EXIT_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_hf_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);
    if (kNoErr != rc)
    {
        LOGI("%s, send queue failed\r\n", __func__);
    }
}


static bk_err_t bt_audio_player_open(blue_audio_decoder_type_t decoder_type, uint16_t frame_length, uint8_t open_vote)
{
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
    if(gl_audio_player_handle)
    {
        LOGW("%s audio player already open\n", __func__);
        return BK_OK;
    }
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
    if(s_bar_handle)
    {
        LOGE("%s recorder already create !!!\n", __func__);
        return -1;
    }
#endif

    LOGI("%s start %d %d %d\n", __func__, decoder_type, frame_length, open_vote);
    /* check whether the audio player service need create */
    uint8_t vote = 0;

    for (uint32_t i = BT_AUDIO_SPK_TASK_START_VOTE_START; i < BT_AUDIO_SPK_TASK_START_VOTE_END; ++i)
    {
        vote |= (1 << i);
    }

    if(vote != open_vote)
    {
        LOGW("%s vote not full, can't start audio player service 0x%x\n", __func__, open_vote);
        return BK_OK;
    }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
    blue_audio_player_cfg_t audio_player_cfg = {0};

    if (decoder_type == BLUE_AUDIO_DECODER_TYPE_SBC)
    {
        //for msbc
        blue_audio_player_cfg_t temp_audio_player_cfg = DEFAULT_BLUE_AUDIO_PLAYER_SBC_ONBOARD_SPK_CONFIG();
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_en = true;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_gpio = 5;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_level = 1;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_delay = 10;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_off_delay = 0;
        temp_audio_player_cfg.raw_strm_cfg.out_block_size = frame_length;
        temp_audio_player_cfg.raw_strm_cfg.out_block_num = 1;//A2DP_SBC_MAX_FRAME_NUMS;
        temp_audio_player_cfg.mix_en = false;
#if HF_SPEAKER_EQ_ENABLE
        temp_audio_player_cfg.eq_en = true;
        eq_algorithm_cfg_t tmp_eq_cfg = DEFAULT_BLUE_AUDIO_PLAYER_EQ_CONFIG();
        temp_audio_player_cfg.eq_cfg.eq_alg_cfg = tmp_eq_cfg;
#endif
        audio_player_cfg = temp_audio_player_cfg;
    }
    else if(decoder_type == BLUE_AUDIO_DECODER_TYPE_PCM)
    {
        blue_audio_player_cfg_t temp_audio_player_cfg = DEFAULT_BLUE_AUDIO_PLAYER_PCM_ONBOARD_SPK_CONFIG();
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_en = true;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_gpio = 5;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_level = 1;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_delay = 10;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_off_delay = 0;
        temp_audio_player_cfg.raw_strm_cfg.output_port_type = PORT_TYPE_RB;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.sample_rate = 8000;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.frame_size = 640;
        temp_audio_player_cfg.mix_en = false;
#if HF_SPEAKER_EQ_ENABLE
        temp_audio_player_cfg.eq_en = true;
        eq_algorithm_cfg_t tmp_eq_cfg = DEFAULT_BLUE_AUDIO_PLAYER_EQ_CONFIG();
        temp_audio_player_cfg.eq_cfg.eq_alg_cfg = tmp_eq_cfg;
#endif
        audio_player_cfg = temp_audio_player_cfg;
    }
    else
    {
        LOGE("blue_audio_player_open: invalid decoder type %d\n", decoder_type);
        return BK_FAIL;
    }
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
    blue_audio_recorder_cfg_t bar_config = {0};

    switch(decoder_type)
    {
    case BLUE_AUDIO_DECODER_TYPE_SBC:
    {
        blue_audio_recorder_cfg_t tmp_config = DEFAULT_BLUE_AUDIO_RECORDER_SBC_ONBOARD_MIC_CONFIG();
#if HF_MIC_EQ_ENABLE
        tmp_config.eq_en = true;
        eq_algorithm_cfg_t tmp_eq_cfg = DEFAULT_BLUE_AUDIO_RECORDER_EQ_CONFIG();
        tmp_config.eq_cfg.eq_alg_cfg = tmp_eq_cfg;
#endif
        bar_config = tmp_config;
    }
    break;

    case BLUE_AUDIO_DECODER_TYPE_PCM:
    {
        blue_audio_recorder_cfg_t tmp_config = DEFAULT_BLUE_AUDIO_RECORDER_PCM_ONBOARD_MIC_CONFIG();
#if HF_MIC_EQ_ENABLE
        tmp_config.eq_en = true;
        eq_algorithm_cfg_t tmp_eq_cfg = DEFAULT_BLUE_AUDIO_RECORDER_EQ_CONFIG();
        tmp_config.eq_cfg.eq_alg_cfg = tmp_eq_cfg;
#endif
        bar_config = tmp_config;
    }
    break;

    default:
        LOGE("%s invalid decoder type %d\n", __func__, decoder_type);
        return -1;
        break;
    }
#endif

    extern int32_t wait_a2dp_speaker_task_end(void);

    LOGI("%s wait a2dp task end\n", __func__);
    wait_a2dp_speaker_task_end();
    LOGI("%s a2dp task end completed\n", __func__);

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
    gl_audio_player_handle = blue_audio_player_create(&audio_player_cfg);
    if(!gl_audio_player_handle)
    {
        LOGE("blue_audio_player_create fail \n");
        return BK_FAIL;
    }
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
    s_bar_handle = blue_audio_recorder_create(&bar_config);
    if(!s_bar_handle)
    {
        LOGE("%s blue_audio_recorder_create err\n", __func__);
        return -1;
    }
#endif

    bk_bt_dac_set_gain(&s_hfp_ctx.mic_vol, &s_hfp_ctx.spk_vol);

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
    if (BK_OK != blue_audio_player_start(gl_audio_player_handle))
    {
        LOGE("blue_audio_player_start fail \n");
        goto fail;
    }
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
    if(blue_audio_recorder_start(s_bar_handle))
    {
        LOGE("%s blue_audio_recorder_start err\n", __func__);
        goto fail;
    }
#endif

    LOGI("%s end ok\n", __func__);

    return BK_OK;

fail:
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
    blue_audio_player_destroy(gl_audio_player_handle);
    gl_audio_player_handle = NULL;
#endif
#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
    blue_audio_recorder_destroy(s_bar_handle);
    s_bar_handle = NULL;
#endif

    LOGI("%s end fail\n", __func__);
    return BK_FAIL;
}

static void bk_bt_app_hfp_client_cb(bk_hf_client_cb_event_t event, bk_hf_client_cb_param_t *param)
{
    LOGI("%s event: %d, addr:%02x:%02x:%02x:%02x:%02x:%02x\r\n", __func__, event, param->remote_bda[0], param->remote_bda[1],
                                                                                  param->remote_bda[2], param->remote_bda[3],
                                                                                  param->remote_bda[4], param->remote_bda[5]);

    switch (event)
    {
        case BK_HF_CLIENT_AUDIO_STATE_EVT:
        {
            LOGI("HFP client audio state: %d\r\n", param->audio_state.state);

            if (BK_HF_CLIENT_AUDIO_STATE_DISCONNECTED == param->audio_state.state)
            {
                bt_audio_hf_sco_disconnected();
            }
            else if (BK_HF_CLIENT_AUDIO_STATE_CONNECTED == param->audio_state.state)
            {
                bt_audio_hfp_hf_codec = param->audio_state.codec_type;
                os_memcpy(hfp_peer_addr, param->remote_bda, 6);
                LOGI("sco connected to %02x:%02x:%02x:%02x:%02x:%02x, codec type %d\n", hfp_peer_addr [ 5 ], hfp_peer_addr [ 4 ], hfp_peer_addr [ 3 ],
                     hfp_peer_addr [ 2 ], hfp_peer_addr [ 1 ], hfp_peer_addr [ 0 ], bt_audio_hfp_hf_codec);

                bt_audio_hf_sco_connected();
            }

        }
        break;
        case BK_HF_CLIENT_CONNECTION_STATE_EVT:
        {
            if (param->conn_state.state == BK_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED)
            {
                LOGI("HFP service level connected, ag_feature:0x%x, ag_chld_feature:0x%x \n", param->conn_state.peer_feat, param->conn_state.chld_feat);
                LOGI("HFP client connect to peer address: %02x:%02x:%02x:%02x:%02x:%02x \n", param->remote_bda [ 0 ], param->remote_bda [ 1 ],
                     param->remote_bda [ 2 ], param->remote_bda [ 3 ],
                     param->remote_bda [ 4 ], param->remote_bda [ 5 ]);
                os_memcpy(hfp_profile_peer_addr, param->remote_bda, sizeof(param->remote_bda));
                s_hfp_status_mach = HFP_STATUS_WAIT_QUERY_CALL;
                bk_bt_hf_client_query_current_calls(param->remote_bda);
                s_connect_status = 1;
            }
            else if (param->conn_state.state == BK_HF_CLIENT_CONNECTION_STATE_DISCONNECTED)
            {
                LOGI("HFP disconnected \n");
                LOGI("HFP disconnect peer address: %02x:%02x:%02x:%02x:%02x:%02x \n", param->remote_bda [ 0 ], param->remote_bda [ 1 ],
                     param->remote_bda [ 2 ], param->remote_bda [ 3 ],
                     param->remote_bda [ 4 ], param->remote_bda [ 5 ]);
                os_memset(hfp_profile_peer_addr, 0, sizeof(hfp_profile_peer_addr));
                s_connect_status = 0;

                if(s_connect_sema)
                {
                    rtos_set_semaphore(&s_connect_sema);
                }
            }
        }
        break;
        case BK_HF_CLIENT_BVRA_EVT:
        {
            LOGI("+BRVA: HPF voice recognition activation status: %d \n", param->bvra.value);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_EVT:
        {
            LOGI("+CIND: HFP call staus:%d \n", param->call.status);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_SETUP_EVT:
        {
            LOGI("+CIND: HFP call_setup status:%d \n", param->call_setup.status);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_HELD_EVT:
        {
            LOGI("+CIND: HFP call_hold status:%d \n", param->call_held.status);
        }
        break;
        case BK_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        {
            LOGI("+CIND: HFP service availability ind: %d\n", param->service_availability.status);
        }
        break;
        case BK_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        {
            LOGI("+CIND: HFP signal strength ind: %d\n", param->signal_strength.value);
        }
        break;
        case BK_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        {
            LOGI("+CIND: HFP roming status:%d \n", param->roaming.status);
        }
        break;
        case BK_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        {
            LOGI("+CIND: HFP battery ind:%d \n", param->battery_level.value);
        }
        break;
        case BK_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        {
            LOGI("+COPS: HFP network operator name:%s \n", param->cops.name);
        }
        break;
        case BK_HF_CLIENT_BTRH_EVT:
        {
            LOGI("+BTRH: HFP Hold status: %d \n", param->btrh.status);
        }
        break;
        case BK_HF_CLIENT_CLIP_EVT:
        {
            LOGI("+CLIP: HFP calling line number: %s, name:%s \n", param->clip.number, param->clip.name);
        }
        break;
        break;
        case BK_HF_CLIENT_CCWA_EVT:
        {
            LOGI("+CCWA: HFP calling waiting number:%s, name: %s\n", param->ccwa.number, param->ccwa.name);
        }
        break;
        case BK_HF_CLIENT_CLCC_EVT:
        {
            LOGI("+CLCC: HFP calls result dir:%d, idx:%d, mpty:%d, number:%s, status:%d \n", param->clcc.dir, param->clcc.idx, param->clcc.mpty, param->clcc.number, param->clcc.status);
        }
        break;
        case BK_HF_CLIENT_VOLUME_CONTROL_EVT:
        {
            if (param->volume_control.type == BK_HF_VOLUME_CONTROL_TARGET_SPK)
            {
                LOGI("+VGS: HPF Speaker gain: %d \n", param->volume_control.volume);
                s_hfp_ctx.spk_vol = param->volume_control.volume;
                bluetooth_storage_save_hfp_volume(hfp_peer_addr, 1, s_hfp_ctx.spk_vol);
                bk_bt_dac_set_gain(NULL, &s_hfp_ctx.spk_vol);
            }
            else if (param->volume_control.type == BK_HF_VOLUME_CONTROL_TARGET_MIC)
            {
                LOGI("+VGM: HPF Microphone gain: %d \n", param->volume_control.volume);
                s_hfp_ctx.mic_vol = param->volume_control.volume;
                bluetooth_storage_save_hfp_volume(hfp_peer_addr, 0, s_hfp_ctx.mic_vol);
                bk_bt_dac_set_gain(&s_hfp_ctx.mic_vol, NULL);
            }
        }
        break;
        case BK_HF_CLIENT_AT_RESPONSE_EVT:
        {
            if (0)//param->at_response.code == BK_HF_AT_RESPONSE_CODE_CME)
            {
                LOGI("+CME ERROR: HFP AG error code: %d \n", param->at_response.cme);
            }
            else
            {
                if(param->at_response.code == BK_HF_AT_RESPONSE_CODE_OK)
                {
                    LOGI("BK_HF_CLIENT_AT_RESPONSE_EVT ok, asso_cmd %d, status %d\n", param->at_response.asso_cmd, s_hfp_status_mach);
                }
                else if(param->at_response.code == BK_HF_AT_RESPONSE_CODE_CME)
                {
                    LOGI("BK_HF_CLIENT_AT_RESPONSE_EVT cme err, cme code 0x%x, asso_cmd %d, status %d\n", param->at_response.cme, param->at_response.asso_cmd, s_hfp_status_mach);
                }
                else
                {
                    LOGI("BK_HF_CLIENT_AT_RESPONSE_EVT normal err 0x%x, asso_cmd %d, status %d\n", param->at_response.code, param->at_response.asso_cmd, s_hfp_status_mach);
                }
#if !HF_AT_TEST
                switch(s_hfp_status_mach)
                {
                case HFP_STATUS_WAIT_QUERY_CALL:
                    s_hfp_status_mach = HFP_STATUS_WAIT_VGS;
                    bk_bt_hf_client_volume_update(hfp_profile_peer_addr, BK_HF_VOLUME_CONTROL_TARGET_SPK, 7);
                    break;

                case HFP_STATUS_WAIT_VGS:
                    s_hfp_status_mach = HFP_STATUS_WAIT_VGM;
                    bk_bt_hf_client_volume_update(hfp_profile_peer_addr, BK_HF_VOLUME_CONTROL_TARGET_MIC, 7);
                    break;

                case HFP_STATUS_WAIT_VGM:
                    s_hfp_status_mach = HFP_STATUS_WAIT_CGMI;//HFP_STATUS_WAIT_DONE;
                    const char *at_cgmi = "AT+CGMI?";
                    hfp_demo_cust_cmd((uint8_t *)at_cgmi);
                    break;

                case HFP_STATUS_WAIT_CGMI:
                    s_hfp_status_mach = HFP_STATUS_WAIT_DONE;
                    LOGI("%s end op\n", __func__);
                    break;
                }

#else
                switch(param->at_response.asso_cmd)
                {
                case BK_HF_AT_CMD_CLCC:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_QUERY_CALL)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_VGS;
                        bk_bt_hf_client_volume_update(hfp_profile_peer_addr, BK_HF_VOLUME_CONTROL_TARGET_SPK, 7);
                    }
                    break;

                case BK_HF_AT_CMD_VGS:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_VGS)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_VGM;
                        bk_bt_hf_client_volume_update(hfp_profile_peer_addr, BK_HF_VOLUME_CONTROL_TARGET_MIC, 7);
                    }
                    break;

                case BK_HF_AT_CMD_VGM:

                    if(s_hfp_status_mach == HFP_STATUS_WAIT_VGM)
                    {
#if HF_AT_ENABLE_CMEE
                        s_hfp_status_mach = HFP_STATUS_WAIT_CUSTOM;
                        bk_bt_hf_client_send_custom_cmd(hfp_profile_peer_addr, "AT+CMEE=1");
                    }
                    break;

                case BK_HF_AT_CMD_CUSTOM:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_CUSTOM)
                    {
#endif
                        s_hfp_status_mach = HFP_STATUS_WAIT_QUERY_CURRENT_OP;
                        bk_bt_hf_client_query_current_operator_name(hfp_profile_peer_addr);
                    }
                    break;

                case BK_HF_AT_CMD_COPS:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_QUERY_CURRENT_OP)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_RETRIEVE_SUB_INFO;
                        bk_bt_hf_client_retrieve_subscriber_info(hfp_profile_peer_addr);
                    }
                    break;

                case BK_HF_AT_CMD_CNUM:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_RETRIEVE_SUB_INFO)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_SEND_VTS;
                        bk_bt_hf_client_send_dtmf(hfp_profile_peer_addr, "1");
                    }
                    break;

                case BK_HF_AT_CMD_VTS:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_SEND_VTS)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_REQ_LAST_TAG_NUM;
                        bk_bt_hf_client_request_last_voice_tag_number(hfp_profile_peer_addr);
                    }
                    break;

                case BK_HF_AT_CMD_BINP:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_REQ_LAST_TAG_NUM)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_NREC;
                        bk_bt_hf_client_send_nrec(hfp_profile_peer_addr);
                    }
                    break;

                case BK_HF_AT_CMD_NREC:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_NREC)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_VR_ENABLE;
                        bk_bt_hf_client_start_voice_recognition(hfp_profile_peer_addr);
                    }
                    break;

                case BK_HF_AT_CMD_BVRA:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_VR_ENABLE)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_VR_DISBLE;
                        bk_bt_hf_client_stop_voice_recognition(hfp_profile_peer_addr);
                    }
                    else if(s_hfp_status_mach == HFP_STATUS_WAIT_VR_DISBLE)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_DIAL;
                        bk_bt_hf_client_dial(hfp_profile_peer_addr, "112");
                    }
                    break;

                case BK_HF_AT_CMD_OUTCALL:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_DIAL)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_DIAL_HUP;
                        bk_bt_hf_client_reject_call(hfp_profile_peer_addr);
                    }
                    else if(s_hfp_status_mach == HFP_STATUS_WAIT_DIAL_MEM)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_DIAL_MEM_HUP;
                        bk_bt_hf_client_reject_call(hfp_profile_peer_addr);
                    }
                    else if(s_hfp_status_mach == HFP_STATUS_WAIT_REDAIL)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_REDAIL_HUP;
                        bk_bt_hf_client_reject_call(hfp_profile_peer_addr);
                    }
                    break;

                case BK_HF_AT_CMD_CHUP:
                    if(s_hfp_status_mach == HFP_STATUS_WAIT_DIAL_HUP)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_DIAL_MEM;
                        bk_bt_hf_client_dial_memory(hfp_profile_peer_addr, 1);
                    }
                    else if(s_hfp_status_mach == HFP_STATUS_WAIT_DIAL_MEM_HUP)
                    {
                        s_hfp_status_mach = HFP_STATUS_WAIT_REDAIL;
                        bk_bt_hf_client_redial(hfp_profile_peer_addr);
                    }
                    else if(s_hfp_status_mach == HFP_STATUS_WAIT_REDAIL_HUP)
                    {
                        LOGI("%s end op\n", __func__);
                        s_hfp_status_mach = HFP_STATUS_WAIT_DONE;
                    }
                    break;

                default:
                    break;
                }
#endif
            }
        }
        break;
        case BK_HF_CLIENT_CNUM_EVT:
        {
            LOGI("+CNUM: HFP subscriber number info, type:%d, number:%s \n", param->cnum.type, param->cnum.number);
        }
        break;
        case BK_HF_CLIENT_BSIR_EVT:
        {
            LOGI("+BSIR: HFP In-band Ring tone staus: %d\n", param->bsir.state);
        }
        break;
        case BK_HF_CLIENT_BINP_EVT:
        {
            LOGI("+BINP: HFP last voice tag record: %s \n", param->binp.number);
        }
        break;
        case BK_HF_CLIENT_RING_IND_EVT:
        {
            LOGI("RING HPF incoming call ind evt\n");
        }
        break;
        case BK_HF_CLIENT_UNKNOWN_DATA_IND_EVT:
        {
            LOGI("unknown data received (len %d)\n", param->unknown_data.data_len);
            const char* CGMI_IPHONE = "Apple Inc";
            if (os_strstr(param->unknown_data.data, CGMI_IPHONE))
            {
                s_hfp_hf_is_iphone = 1;
                LOGI("iphone device\r\n");
            }
        }
        break;
        default:
            LOGW("Invalid HFP client event: %d\r\n", event);
            break;
    }
}

void hfp_demo_vr(uint8_t enable)
{
    if(enable)
    {
        bk_bt_hf_client_start_voice_recognition(hfp_profile_peer_addr);
    }
    else
    {
        bk_bt_hf_client_stop_voice_recognition(hfp_profile_peer_addr);
    }
}

void hfp_demo_dial(uint8_t enable, uint8_t *num)
{
    if(enable)
    {
        bk_bt_hf_client_dial(hfp_profile_peer_addr, (const char *)num);
    }
    else
    {
        bk_bt_hf_client_reject_call(hfp_profile_peer_addr);
    }
}

void hfp_demo_answer(uint8_t accept)
{
    if(accept)
    {
        bk_bt_hf_client_answer_call(hfp_profile_peer_addr);
    }
    else
    {
        bk_bt_hf_client_reject_call(hfp_profile_peer_addr);
    }
}

void hfp_demo_cust_cmd(uint8_t *cmd)
{
    LOGI("%s len %d\n", __func__, strlen((char *)cmd));
    bk_bt_hf_client_send_custom_cmd(hfp_profile_peer_addr, (const char *)cmd);
}

int32_t hfp_demo_chld_cmd(uint8_t op)
{
    LOGI("%s %d\n", __func__, op);
    return bk_bt_hf_client_send_chld_cmd(hfp_profile_peer_addr, op);
}

int32_t hfp_demo_btrh_cmd(uint8_t op)
{
    LOGI("%s %d\n", __func__, op);
    return bk_bt_hf_client_send_btrh_cmd(hfp_profile_peer_addr, op);
}

uint8_t hfp_hf_check_is_iphone(void)
{
    return s_hfp_hf_is_iphone;
}

static void bt_audio_hf_demo_main(void *arg)
{
    uint32_t spk_task_start_vote = (1 << BT_AUDIO_SPK_TASK_START_VOTE_USER);
    uint8_t recon_addr[6] = {0};

    if ((bluetooth_storage_get_newest_linkkey_info(recon_addr, NULL)) < 0)
    {
        LOGI("%s can't find linkkey info\n", __func__);
    }
    else
    {
        LOGI("%s find addr\n", __func__);
        if(bluetooth_storage_find_hfp_volume_by_addr(recon_addr, &s_hfp_ctx.mic_vol, &s_hfp_ctx.spk_vol) < 0)
        {

            s_hfp_ctx.mic_vol = 1.0 * PLATFORM_MIC_GAIN_DEFAULT / PLATFORM_MIC_GAIN_MAX * HFP_GAIN_MAX;
            s_hfp_ctx.spk_vol = 1.0 * PLATFORM_SPK_GAIN_DEFAULT / PLATFORM_SPK_GAIN_MAX * HFP_GAIN_MAX;
        }

        if(s_hfp_ctx.mic_vol == 0)
        {
            s_hfp_ctx.mic_vol = 1.0 * PLATFORM_MIC_GAIN_DEFAULT / PLATFORM_MIC_GAIN_MAX * HFP_GAIN_MAX;
        }

        if(s_hfp_ctx.spk_vol == 0)
        {
            s_hfp_ctx.spk_vol = 1.0 * PLATFORM_SPK_GAIN_DEFAULT / PLATFORM_SPK_GAIN_MAX * HFP_GAIN_MAX;
        }

        LOGI("initial volume %d %d %02x:%02x:%02x:%02x:%02x:%02x\n",
                        s_hfp_ctx.mic_vol,
                        s_hfp_ctx.spk_vol,
                        recon_addr[5],
                        recon_addr[4],
                        recon_addr[3],
                        recon_addr[2],
                        recon_addr[1],
                        recon_addr[0]
                        );
    }

    while (1)
    {
        bk_err_t err;
        bt_audio_hf_demo_msg_t msg;

        err = rtos_pop_from_queue(&bt_audio_hf_demo_msg_que, &msg, BEKEN_WAIT_FOREVER);
        if (kNoErr == err)
        {
            switch (msg.type)
            {
                case BT_AUDIO_VOICE_START_MSG:
                {
                    LOGI("BT_AUDIO_VOICE_START_MSG \r\n");

                    spk_task_start_vote |= (1 << BT_AUDIO_SPK_TASK_START_VOTE_HFP);

                    if (CODEC_VOICE_MSBC == bt_audio_hfp_hf_codec)
                    {
                        decoder_type = BLUE_AUDIO_DECODER_TYPE_SBC;
                    }
                    else
                    {
                        decoder_type = BLUE_AUDIO_DECODER_TYPE_PCM;
                    }

                    hf_auido_start = 1;

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
                    if (BK_OK != bt_audio_player_open(decoder_type, decoder_type == BLUE_AUDIO_DECODER_TYPE_SBC ? 58 : 0,
                            spk_task_start_vote))
                    {
                        LOGE("%s bt_audio_player_open failed\n", __func__);
                    }
#endif
                    mic_task_init();
                    LOGI("hfp audio init ok\r\n");
                }
                break;

                case BT_AUDIO_VOICE_USER_START_MSG:
                {
                    uint32_t enable = (typeof(enable))msg.data;

                    LOGI("BT_AUDIO_VOICE_USER_START_MSG %d\n", enable);
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
                    if(enable)
                    {
                        spk_task_start_vote |= (1 << BT_AUDIO_SPK_TASK_START_VOTE_USER);

                        if (BK_OK != bt_audio_player_open(decoder_type, frame_length, spk_task_start_vote))
                        {
                            LOGE("%s bt_audio_player_open failed\n", __func__);
                            BK_ASSERT(0);
                        }
                    }
                    else
                    {
                        spk_task_start_vote &= ~(1 << BT_AUDIO_SPK_TASK_START_VOTE_USER);
                        /* destroy blue_audio_player */
                        if(gl_audio_player_handle)
                        {
                            blue_audio_player_stop(gl_audio_player_handle);
                            blue_audio_player_destroy(gl_audio_player_handle);
                            gl_audio_player_handle = NULL;
                        }
                    }
#endif
                    /* set sync semaphore */
                    rtos_set_semaphore(&s_audio_player_en_sema);
                }
                break;

                case BT_AUDIO_VOICE_STOP_MSG:
                case BT_AUDIO_VOICE_TASK_EXIT_MSG:
                {
                    if(msg.type == BT_AUDIO_VOICE_STOP_MSG)
                    {
                        LOGI("BT_AUDIO_VOICE_STOP_MSG\n");
                    }
                    else
                    {
                        LOGI("BT_AUDIO_VOICE_TASK_EXIT_MSG\n");
                    }

                    if(hf_mic_thread_handle) //hf_speaker_thread_handle
                    {
                        if (kNoErr != rtos_init_semaphore(&hf_mic_speaker_exit_sema, 1))//2))
                        {
                            LOGE("init sema fail, %d \n", __LINE__);
                        }

                        hf_auido_start = 0;
                    }

                    if(hf_mic_thread_handle)
                    {
                        LOGI("%s wait mic thread end\n", __func__);
                        if (hf_mic_speaker_exit_sema)
                        {
                            rtos_get_semaphore(&hf_mic_speaker_exit_sema, BEKEN_WAIT_FOREVER);
                        }
                        LOGI("%s mic thread end !!!\n", __func__);
                        hf_mic_thread_handle = NULL;
                    }

                    spk_task_start_vote &= ~(1 << BT_AUDIO_SPK_TASK_START_VOTE_HFP);
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
                    /* destroy blue_audio_player */
                    if(gl_audio_player_handle)
                    {
                        LOGI("%s start blue_audio_player_stop/destroy\n", __func__);
                        blue_audio_player_stop(gl_audio_player_handle);
                        blue_audio_player_destroy(gl_audio_player_handle);
                        gl_audio_player_handle = NULL;
                        LOGI("%s blue_audio_player_stop/destroy end\n", __func__);
                    }
#endif

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE
                    if(s_bar_handle)
                    {
                        LOGI("%s start blue_audio_recorder_stop/destroy\n", __func__);
                        blue_audio_recorder_stop(s_bar_handle);
                        blue_audio_recorder_destroy(s_bar_handle);
                        s_bar_handle = NULL;
                        LOGI("%s blue_audio_recorder_stop/destroy end\n", __func__);
                    }
#endif
                    if (hf_mic_speaker_exit_sema)
                    {
                        rtos_deinit_semaphore(&hf_mic_speaker_exit_sema);
                        hf_mic_speaker_exit_sema = NULL;
                    }

                    if(msg.type == BT_AUDIO_VOICE_TASK_EXIT_MSG)
                    {
                        goto exit;
                    }
                }
                break;

                case BT_AUDIO_VOICE_IND_MSG:
                {
                    bk_err_t ret = BK_OK;
                    uint8 *fb = (uint8_t *)msg.data;
                    uint16_t r_len = 0;
                    uint16_t packet_len = SCO_CVSD_SAMPLES_PER_FRAME * 2;
                    uint8_t packet_num = 4;

                    if (CODEC_VOICE_MSBC == bt_audio_hfp_hf_codec)
                    {
                        LOGI("%s -->len %d, 0x%x 0x%x 0x%x\n", __func__, msg.len, fb[0], fb[1], fb[2]);
                    }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE

                    if (CODEC_VOICE_MSBC == bt_audio_hfp_hf_codec)
                    {
                        //todo: msbc
                        fb += 2; //Skip Synchronization Header
                        //ret = bk_sbc_decoder_frame_decode(&bt_audio_hf_sbc_decoder, fb, msg.len - 2);
                        blue_audio_player_write_frame_data(gl_audio_player_handle, (char *)fb, msg.len - 2);
                    }
                    else
                    {
                        packet_len = r_len = SCO_CVSD_SAMPLES_PER_FRAME * 2;

                        if(r_len != msg.len)
                        {
                            LOGE("%s len not match %d %d\n", __func__, r_len, msg.len);
                        }

                        blue_audio_player_write_frame_data(gl_audio_player_handle, (char *)fb, packet_len);
                    }
#endif

                    psram_free(msg.data);
                }
                break;

                default:
                    break;
            }
        }
    }

exit:

    rtos_delete_thread(NULL);
}

int bt_audio_hf_demo_task_init(void)
{
    bk_err_t ret = BK_OK;
    if ((!bt_audio_hf_demo_thread_handle) && (!bt_audio_hf_demo_msg_que))
    {
        ret = rtos_init_queue(&bt_audio_hf_demo_msg_que,
                              "bt_audio_hf_demo_msg_que",
                              sizeof(bt_audio_hf_demo_msg_t),
                              BT_AUDIO_HF_DEMO_MSG_COUNT);
        if (ret != kNoErr)
        {
            LOGI("bt_audio hf demo msg queue failed \r\n");
            return BK_FAIL;
        }

        ret = rtos_create_thread(&bt_audio_hf_demo_thread_handle,
                                 BEKEN_DEFAULT_WORKER_PRIORITY,
                                 "bt_audio_hf_demo",
                                 (beken_thread_function_t)bt_audio_hf_demo_main,
#if (HF_MIC_EQ_ENABLE || HF_SPEAKER_EQ_ENABLE)
                                 1024 * 6,
#else
                                 4096,
#endif
                                 (beken_thread_arg_t)0);
        if (ret != kNoErr)
        {
            LOGI("bt_audio hf demo task fail \r\n");
            rtos_deinit_queue(&bt_audio_hf_demo_msg_que);
            bt_audio_hf_demo_msg_que = NULL;
            bt_audio_hf_demo_thread_handle = NULL;
        }

        return kNoErr;
    }
    else
    {
        return kInProgressErr;
    }
}

static int bt_audio_hf_demo_task_deinit(void)
{
    bk_err_t ret = BK_OK;

    if (bt_audio_hf_demo_thread_handle)
    {
        bt_audio_task_exit();

        LOGI("%s wait demo task end\n", __func__);
        rtos_thread_join(&bt_audio_hf_demo_thread_handle);
        LOGI("%s demo task end !!!\n", __func__);
        bt_audio_hf_demo_thread_handle = NULL;

        if (bt_audio_hf_demo_msg_que)
        {
            bk_err_t err = 0;
            bt_audio_hf_demo_msg_t msg = {0};

            while ((err = rtos_pop_from_queue(&bt_audio_hf_demo_msg_que, &msg, 0)) == 0)
            {
                switch (msg.type)
                {
                case BT_AUDIO_VOICE_IND_MSG:
                    if (msg.data)
                    {
                        os_free(msg.data);
                        msg.data = NULL;
                    }

                    break;

                default:
                    break;
                }

                os_memset(&msg, 0, sizeof(msg));
            }

            rtos_deinit_queue(&bt_audio_hf_demo_msg_que);
            bt_audio_hf_demo_msg_que = NULL;
        }
    }

    (void)ret;
    return 0;
}

static void bk_bt_hfp_disconnect(uint8_t *remote_addr)
{
    LOGI("%s %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
                    remote_addr[5],
                    remote_addr[4],
                    remote_addr[3],
                    remote_addr[2],
                    remote_addr[1],
                    remote_addr[0]);

    bk_bt_hf_client_disconnect(remote_addr);
}

int hfp_hf_demo_init(uint8_t msbc_supported)
{
    int ret = kNoErr;

    LOGI("%s\n", __func__);

    if (s_hfp_hf_is_inited)
    {
        LOGE("%s already init\n", __func__);
        return -1;
    }

    bt_audio_hf_demo_task_init();

    ret = bk_bt_hf_client_register_callback(bk_bt_app_hfp_client_cb);
    if (ret)
    {
        LOGI("%s bk_bt_hf_client_register_callback err %d\n", __func__, ret);
        return -1;
    }

    ret = bk_bt_hf_client_init(msbc_supported);
    if (ret)
    {
        LOGI("%s bk_bt_hf_client_init err %d\n", __func__, ret);
        return -1;
    }

    ret = bk_bt_hf_client_register_data_callback(bt_audio_hfp_client_voice_data_ind);
    if (ret)
    {
        LOGI("%s bk_bt_hf_client_register_data_callback err %d\n", __func__, ret);
        return -1;
    }

    btm_callback_s btm_cb =
    {
        .start_disconnect_cb = bk_bt_hfp_disconnect,
    };

    s_bt_manager_index = bt_manager_register_callback(&btm_cb);

    s_hfp_hf_is_inited = 1;

    LOGI("%s end\n", __func__);
    return ret;
}

int hfp_hf_demo_deinit(void)
{
    int ret = kNoErr;

    LOGI("%s\n", __func__);

    if (!s_hfp_hf_is_inited)
    {
        LOGE("%s already deinit\n", __func__);
        return -1;
    }

    if(s_connect_status)
    {
        if (!s_connect_sema)
        {
            if (rtos_init_semaphore(&s_connect_sema, 1))
            {
                LOGE("%s init connect sema fail\n", __func__);
                goto end;
            }
        }

        LOGW("%s disconnecting hfp\n", __func__);
        bk_bt_hf_client_disconnect(hfp_profile_peer_addr);
        LOGW("%s wait disconnect hfp sem\n", __func__);

        ret = rtos_get_semaphore(&s_connect_sema, 5000);

        if(ret)
        {
            LOGE("%s wait disconnect hfp sem err %d\n", __func__, ret);
        }
        else
        {
            LOGW("%s wait disconnect hfp success\n", __func__);
        }
    }

    bt_audio_hf_demo_task_deinit();

    bt_manager_unregister_callback(s_bt_manager_index);
    s_bt_manager_index = 0xFF;

    bk_bt_hf_client_register_callback(NULL);
    bk_bt_hf_client_register_data_callback(NULL);

    bk_bt_hf_client_deinit();

    s_hfp_hf_is_inited = 0;

end:;

    if (s_connect_sema)
    {
        if (rtos_deinit_semaphore(&s_connect_sema))
        {
            LOGE("%s deinit connect sema fail\n", __func__);
        }

        s_connect_sema = NULL;
    }

    LOGI("%s end\n", __func__);
    return ret;
}

static int mic_task_init()
{
    bk_err_t ret = BK_OK;
    if (!hf_mic_thread_handle)
    {
        ret = rtos_create_thread(&hf_mic_thread_handle,
                                 HF_MIC_THREAD_PRI,
                                 "bt_hf_mic",
                                 (beken_thread_function_t)mic_task,
                                 4096,
                                 (beken_thread_arg_t)0);
        if (ret != kNoErr)
        {
            LOGE("mic task fail \r\n");
        }

        return kNoErr;
    }
    else
    {
        LOGE("%s mic task already exist \r\n", __func__);
        return kInProgressErr;
    }

    return kNoErr;
}


static void mic_task(void *arg)
{
    int32_t ret = 0;

    LOGI("%s wait a2dp task end\n", __func__);
    extern int32_t wait_a2dp_speaker_task_end(void);
    wait_a2dp_speaker_task_end();

#if CONFIG_BLUE_AUDIO_RECORDER_SERVICE

    LOGI("%s init success!! \r\n", __func__);

    while (hf_auido_start)
    {
        int32_t expect_len = 0;
        int32_t read_len = 0;
        int32_t read_all_len = 0;

        if(bt_audio_hfp_hf_codec == CODEC_VOICE_CVSD)
        {
            expect_len = SCO_CVSD_SAMPLES_PER_FRAME * 2;

        }
        else if(bt_audio_hfp_hf_codec == CODEC_VOICE_MSBC)
        {
            expect_len = 58;
        }

        if (hf_mic_data_count + expect_len > sizeof(hf_mic_sco_data))
        {
            LOGE("%s mic data buffer overflow\n", __func__);
            hf_mic_data_count = 0;
        }

        while(hf_auido_start && read_all_len < expect_len)
        {
            read_len = blue_audio_recorder_read_frame_data(s_bar_handle, (char *)(hf_mic_sco_data + hf_mic_data_count + read_all_len), expect_len - read_all_len);

            if(read_len <= 0)
            {
                LOGE("%s blue audio recorder read ret %d !!!\n", __func__, read_len);
                continue;
            }

            if(bt_audio_hfp_hf_codec == CODEC_VOICE_MSBC)
            {
                LOGI("read msbc len %d\n", read_len);
            }

            read_all_len += read_len;

            if(read_all_len < expect_len)
            {
                continue;
            }

            hf_mic_data_count += read_all_len;

            if(hf_mic_data_count)
            {
                LOGV("send sco %d\n", hf_mic_data_count);

                bk_bt_hf_client_voice_out_write(hfp_peer_addr, hf_mic_sco_data, hf_mic_data_count);
                hf_mic_data_count = 0;
            }

            break;
        }
    }

#else

#endif
    LOGI("%s end!! %d\r\n", __func__, hf_auido_start);

    if (hf_mic_speaker_exit_sema)
    {
        rtos_set_semaphore(&hf_mic_speaker_exit_sema);
    }

    rtos_delete_thread(NULL);
}

int32_t wait_hfp_speaker_mic_task_end(void)
{
    while(hf_mic_thread_handle || gl_audio_player_handle)
    {
        rtos_delay_milliseconds(20);
    }

    return 0;
}