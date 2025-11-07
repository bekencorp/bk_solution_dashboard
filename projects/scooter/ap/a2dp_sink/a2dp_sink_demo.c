#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "headset_user_config.h"

#include "a2dp_sink_demo.h"

#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_bt_types.h"
#include "components/bluetooth/bk_dm_bt.h"
#include "components/bluetooth/bk_dm_a2dp_types.h"
#include "components/bluetooth/bk_dm_a2dp.h"
#include "components/bluetooth/bk_dm_gap_bt.h"

#include "blue_audio_player_service.h"

#include "components/bluetooth/bk_dm_avrcp.h"
#include "bluetooth_storage.h"
#if CONFIG_WIFI_COEX_SCHEME
#include "bk_coex_ext.h"
#endif
#include "bt_manager.h"

#include "bk_gpio.h"

#include "mpeg4_latm_dec.h"

#define TAG "a2dp_sink"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define CHECK_NULL(ptr) do {\
        if (ptr == NULL) {\
            LOGI("CHECK_NULL fail \n");\
            return;\
        }\
    } while(0)


#define CODEC_AUDIO_SBC                      0x00U
#define CODEC_AUDIO_AAC                      0x02U

#define A2DP_SBC_CHANNEL_MONO                        0x08U
#define A2DP_SBC_CHANNEL_DUAL                        0x04U
#define A2DP_SBC_CHANNEL_STEREO                      0x02U
#define A2DP_SBC_CHANNEL_JOINT_STEREO                0x01U


#define A2DP_CACHE_BUFFER_SIZE  ((CONFIG_A2DP_CACHE_FRAME_NUM + 2) * 1024)
#define A2DP_SBC_FRAME_BUFFER_MAX_SIZE  128   /**< A2DP SBC encoded frame maximum size */
#define A2DP_AAC_FRAME_BUFFER_MAX_SIZE  1024  /**< A2DP AAC encoded frame maximum size */

#define A2DP_SPEAKER_THREAD_PRI             BEKEN_DEFAULT_WORKER_PRIORITY-2
#define A2DP_SPEAKER_WRITE_SBC_FRAME_NUM    7
#define A2DP_SBC_MAX_FRAME_NUMS             0xF*5*1
#define A2DP_SBC_FRAME_HEADER_LEN           13

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE_SUPPORT_EQ
#define A2DP_SPEAKER_EQ_ENABLE 0
#endif

#if CONFIG_AAC_DECODER
#define A2DP_ACC_SINGLE_CHANNEL_MAX_FRAM_SIZE   768   //1024(samples)/48k/s(sample rate)*288k/s (max bitrate)/8
#define A2DP_AAC_MAX_FRAME_NUMS                 5     //21ms/frame*5
#endif

#define AVRCP_PASSTHROUTH_CMD_TIMEOUT                     2000

enum
{
    BT_AUDIO_MSG_NULL = 0,
    BT_AUDIO_D2DP_START_MSG,
    BT_AUDIO_D2DP_STOP_MSG,
    BT_AUDIO_D2DP_DATA_IND_MSG,
    BT_AUDIO_D2DP_SEND_DATA_2_SPK_MSG,
    BT_AUDIO_WIFI_STATE_UPDATE_MSG,
    BT_AUDIO_USER_START_MSG,
    BT_AUDIO_EXIT_MSG,
};

enum
{
    BT_AUDIO_SPK_TASK_START_VOTE_START = 0,
    BT_AUDIO_SPK_TASK_START_VOTE_A2DP = BT_AUDIO_SPK_TASK_START_VOTE_START,
    BT_AUDIO_SPK_TASK_START_VOTE_USER,
    BT_AUDIO_SPK_TASK_START_VOTE_END,
};

typedef struct
{
    uint8_t type;
    uint16_t len;
    char *data;
} bt_audio_sink_demo_msg_t;

typedef struct
{
    uint8_t wifi_state;
    uint8_t a2dp_state;
    uint8_t avrcp_state;
    beken2_timer_t avrcp_connect_tmr;
} bt_env_s;


static bk_a2dp_mcc_t bt_audio_a2dp_sink_codec = {0};

static beken_queue_t bt_audio_sink_demo_msg_que = NULL;
static beken_thread_t bt_audio_sink_demo_thread_handle = NULL;
//#define CONFIG_BLUE_AUDIO_PLAYER_SERVICE 1
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
#define PLATFORM_SPK_GAIN_MAX 0x3f // see onboard_mic_stream.h
#define PLATFORM_SPK_GAIN_DEFAULT 0x2d // see onboard_mic_stream.h
//static RingBufferNodeContext s_a2dp_frame_nodes;
static uint16_t frame_length = 0;
static blue_audio_decoder_type_t decoder_type = BLUE_AUDIO_DECODER_TYPE_SBC;

static blue_audio_player_handle_t gl_audio_player_handle = NULL;
#endif

static beken_semaphore_t s_audio_player_en_sema = NULL;
static beken_semaphore_t s_a2dp_connect_sema = NULL;

static uint8_t s_a2dp_vol = DEFAULT_A2DP_VOLUME;//0~0x7f
static uint16_t s_tg_current_registered_noti;

static bt_env_s s_bt_env;
#if CONFIG_WIFI_COEX_SCHEME
static coex_to_bt_func_p_t s_coex_to_bt_func = {0};
#endif

static beken_semaphore_t s_bt_api_event_cb_sema = NULL;
static beken_semaphore_t s_bt_avrcp_event_cb_sema = NULL;

static uint8_t s_a2dp_sink_is_inited = 0;
static uint8_t s_a2dp_sink_bt_manager_index = 0xff;
static uint8_t s_auto_accept_connect_req = 1;
static uint8_t s_mix_multi_channel = 1;

#define AVRCP_GAIN_MAX (128 - 1) //see avrcp porotocl

static bk_err_t bk_bt_dac_set_gain(uint8_t avrcp_vol)
{
    if(gl_audio_player_handle)
    {
        uint8_t gain = 0;

        gain = ((PLATFORM_SPK_GAIN_MAX + 1) * 1.0 / (AVRCP_GAIN_MAX + 1)) * avrcp_vol;

        LOGI("%s set spk gain 0x%x\n", __func__, gain);
        blue_audio_player_set_volume(gl_audio_player_handle, gain);
    }
    else
    {
        LOGE("%s audio play not enable\n", __func__);
    }

    return BK_OK;
}

static void avrcp_connect_timer_hdl(void *param, unsigned int ulparam)
{
    rtos_deinit_oneshot_timer(&s_bt_env.avrcp_connect_tmr);
    if (0 == s_bt_env.avrcp_state)
    {
        bk_bt_avrcp_connect(bt_manager_get_connected_device());
    }
}

static void bk_bt_start_avrcp_connect(void)
{
    if (!rtos_is_oneshot_timer_init(&s_bt_env.avrcp_connect_tmr))
    {
        rtos_init_oneshot_timer(&s_bt_env.avrcp_connect_tmr, 300, (timer_2handler_t)avrcp_connect_timer_hdl, NULL, 0);
        rtos_start_oneshot_timer(&s_bt_env.avrcp_connect_tmr);
    }
}

static bk_err_t one_spk_frame_played_cmpl_handler(unsigned int size)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return BK_OK;
    }

    demo_msg.type = BT_AUDIO_D2DP_SEND_DATA_2_SPK_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);
    }
    return BK_OK;
}

static void bt_audio_sink_task_exit(void)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_sink_demo_msg_t));

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return;
    }

    demo_msg.type = BT_AUDIO_EXIT_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);
    }
}

static void bt_audio_sink_task_user_vote_spk_task(uint8_t enable)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_sink_demo_msg_t));

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return;
    }

    demo_msg.type = BT_AUDIO_USER_START_MSG;
    demo_msg.len = 0;
    demo_msg.data = (typeof(demo_msg.data))(uint32_t)enable;

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);
    }
}

static bk_err_t bt_audio_player_open(blue_audio_decoder_type_t decoder_type, uint16_t frame_length, uint8_t open_vote)
{
    if(gl_audio_player_handle)
    {
        LOGW("%s audio player already open\n", __func__);
        return BK_OK;
    }

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

    blue_audio_player_cfg_t audio_player_cfg = {0};

    if (decoder_type == BLUE_AUDIO_DECODER_TYPE_SBC)
    {
        blue_audio_player_cfg_t temp_audio_player_cfg = DEFAULT_BLUE_AUDIO_PLAYER_SBC_ONBOARD_SPK_CONFIG();
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_en = true;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_gpio = 5;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_level = 1;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_delay = 10;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_off_delay = 0;
        temp_audio_player_cfg.raw_strm_cfg.out_block_size = frame_length;
        temp_audio_player_cfg.raw_strm_cfg.out_block_num = A2DP_SBC_MAX_FRAME_NUMS;
#if A2DP_SPEAKER_EQ_ENABLE
        temp_audio_player_cfg.eq_en = true;
        eq_algorithm_cfg_t tmp_eq_cfg = DEFAULT_BLUE_AUDIO_PLAYER_EQ_CONFIG();
        temp_audio_player_cfg.eq_cfg.eq_alg_cfg = tmp_eq_cfg;
#endif
        audio_player_cfg = temp_audio_player_cfg;
    }
    else if (decoder_type == BLUE_AUDIO_DECODER_TYPE_AAC)
    {
#if CONFIG_AAC_DECODER
        blue_audio_player_cfg_t temp_audio_player_cfg = DEFAULT_BLUE_AUDIO_PLAYER_AAC_ONBOARD_SPK_CONFIG();
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_en = true;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_ctrl_gpio = 5;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_level = 1;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_on_delay = 10;
        temp_audio_player_cfg.speaker_cfg.ob_spk_cfg.pa_off_delay = 0;
        temp_audio_player_cfg.raw_strm_cfg.out_block_size = frame_length;
        temp_audio_player_cfg.raw_strm_cfg.out_block_num = A2DP_AAC_MAX_FRAME_NUMS;
#if A2DP_SPEAKER_EQ_ENABLE
        temp_audio_player_cfg.eq_en = true;
        eq_algorithm_cfg_t tmp_eq_cfg = DEFAULT_BLUE_AUDIO_PLAYER_EQ_CONFIG();
        temp_audio_player_cfg.eq_cfg.eq_alg_cfg = tmp_eq_cfg;
#endif
        audio_player_cfg = temp_audio_player_cfg;
#endif
    }
    else
    {
        LOGE("blue_audio_player_open: invalid decoder type %d\n", decoder_type);
        return BK_FAIL;
    }

    extern int32_t wait_hfp_speaker_mic_task_end(void);

    LOGI("%s wait hfp task end\n", __func__);
    wait_hfp_speaker_mic_task_end();
    LOGI("%s hfp task end completed\n", __func__);

    gl_audio_player_handle = blue_audio_player_create(&audio_player_cfg);
    if(!gl_audio_player_handle)
    {
        LOGE("blue_audio_player_create fail \n");
        return BK_FAIL;
    }

    bk_bt_dac_set_gain(s_a2dp_vol);

#if !SLOW_SEND_FRAME_WHEN_A2DP_ENABLE
    if (BK_OK != blue_audio_player_start(gl_audio_player_handle))
    {
        LOGE("blue_audio_player_start fail \n");
        goto fail;
    }
#endif

    return BK_OK;

fail:
    blue_audio_player_destroy(gl_audio_player_handle);
    gl_audio_player_handle = NULL;
    return BK_FAIL;
}

/**
 * @brief API function to parse and obtain the frame length of SBC and mSBC.
 * @param buf Address of the two-byte buffer of the previous frame data.
 * @param len Length of the data.
 * @return Returns the effective length of the frame on success, and BK_FAIL on failure.
 */
bk_err_t bk_sbc_frame_length_parse(uint8_t *buf, uint16_t len)
{
    // Check input parameters
    if (buf == NULL || len < 3)
    {
        LOGE("Invalid input parameters\n");
        return BK_FAIL;
    }

    uint8_t syncword = buf[0];
    uint8_t channel_mode = 0;
    uint8_t blocks = 0;
    uint8_t subbands = 0;
    uint8_t bitpool = 0;
    uint8_t num_channels = 1;
    int32_t calculated_frame_len = 0;

    // Determine whether it is an SBC frame or an mSBC frame
    if (syncword == 0x9C)    // SBC frame sync word
    {
        // Parse SBC frame header information
        channel_mode = (buf[1] >> 2) & 0x03;
        blocks = (((buf[1] >> 4) & 0x03) + 1) << 2; // 4, 8, 12, 16
        subbands = ((buf[1] & 0x01) + 1) << 2; // 4 or 8
        bitpool = buf[2];

        // Determine the number of channels based on the channel mode
        num_channels = (channel_mode == 0) ? 1 : 2; // 0 indicates MONO mode
    }
    else if (syncword == 0xAD)    // mSBC frame sync word (according to common definition)
    {
        // mSBC usually has a fixed configuration
        blocks = 15;
        subbands = 8;
        bitpool = 26;
        num_channels = 1;
        channel_mode = 0; // MONO mode

        // For some extended mSBC formats, they may contain additional configuration information
        if (buf[1] != 0 || buf[2] != 0)
        {
            channel_mode = (buf[1] >> 2) & 0x03;
            subbands = ((buf[1] & 0x01) + 1) << 2;
            bitpool = buf[2];
            num_channels = (channel_mode == 0) ? 1 : 2;
        }
    }
    else
    {
        // Not a valid SBC or mSBC frame header
        LOGE("Invalid syncword: 0x%x\n", syncword);
        return BK_FAIL;
    }

    // Check if the bitpool is out of range
    if (((channel_mode == 0 || channel_mode == 1) && (bitpool > (subbands << 4))) ||
        ((channel_mode == 2 || channel_mode == 3) && (bitpool > (subbands << 5))))
    {
        LOGE("Bitpool out of bounds: %d\n", bitpool);
        return BK_FAIL;
    }

    // Calculate the frame length
    calculated_frame_len = 4 + ((4 * subbands * num_channels) >> 3);

    if (channel_mode == 0 || channel_mode == 1)    // MONO or DUAL_CHANNEL
    {
        calculated_frame_len += ((blocks * num_channels * bitpool) + 7) >> 3;
    }
    else    // STEREO or JOINT_STEREO
    {
        if (channel_mode == 3)    // JOINT_STEREO
        {
            calculated_frame_len += (subbands + blocks * bitpool + 7) >> 3;
        }
        else    // STEREO
        {
            calculated_frame_len += (blocks * bitpool + 7) >> 3;
        }
    }

    // Check if the calculated frame length exceeds the input data length
    if (calculated_frame_len > len)
    {
        LOGE("Frame length %d exceeds buffer length %d\n", calculated_frame_len, len);
        return BK_FAIL;
    }

    // Return the calculated frame length
    return calculated_frame_len;
}

static void bt_audio_sink_demo_main(void *arg)
{
    uint8_t recon_addr[6] = {0};
    uint16_t org_frame_len = 0;
    uint32_t spk_task_start_vote = (1 << BT_AUDIO_SPK_TASK_START_VOTE_USER);

#if SLOW_SEND_FRAME_WHEN_A2DP_ENABLE
    uint32_t slow_send_buffer_count = 0;
    uint8_t slow_send_status = 0;
#endif

    if ((bluetooth_storage_get_newest_linkkey_info(recon_addr,NULL)) < 0)
    {
        LOGI("%s can't find linkkey info\n", __func__);
        bt_manager_set_mode(BT_MNG_MODE_PAIRING);
    }
    else
    {
        LOGI("%s find addr\n", __func__);
        if(bluetooth_storage_find_volume_by_addr(recon_addr, &s_a2dp_vol) < 0)
        {
            //s_a2dp_vol = DEFAULT_A2DP_VOLUME; //default vol
            s_a2dp_vol = 1.0 * PLATFORM_SPK_GAIN_DEFAULT / PLATFORM_SPK_GAIN_MAX * AVRCP_GAIN_MAX;
        }

        if(s_a2dp_vol == 0)
        {
            //s_a2dp_vol = DEFAULT_A2DP_VOLUME;
            s_a2dp_vol = 1.0 * PLATFORM_SPK_GAIN_DEFAULT / PLATFORM_SPK_GAIN_MAX * AVRCP_GAIN_MAX;
        }

        LOGI("initial volume %d %02x:%02x:%02x:%02x:%02x:%02x\r\n",s_a2dp_vol,
                        recon_addr[5],
                        recon_addr[4],
                        recon_addr[3],
                        recon_addr[2],
                        recon_addr[1],
                        recon_addr[0]
                        );

        //bt_manager_start_reconnect(recon_addr, 1);
    }

    while (1)
    {
        bk_err_t err;
        bt_audio_sink_demo_msg_t msg;

        err = rtos_pop_from_queue(&bt_audio_sink_demo_msg_que, &msg, BEKEN_WAIT_FOREVER);

        if (kNoErr == err)
        {
            switch (msg.type)
            {
            case BT_AUDIO_D2DP_START_MSG:
            {
                LOGI("BT_AUDIO_D2DP_START_MSG \r\n");

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
                spk_task_start_vote |= (1 << BT_AUDIO_SPK_TASK_START_VOTE_A2DP);

                bk_a2dp_mcc_t *p_codec_info = (bk_a2dp_mcc_t *)msg.data;
                if (CODEC_AUDIO_SBC == p_codec_info->type)
                {
                    uint8_t chnl_mode = p_codec_info->cie.sbc_codec.channel_mode;
                    uint8_t chnls = p_codec_info->cie.sbc_codec.channels;
                    uint8_t subbands = p_codec_info->cie.sbc_codec.subbands;
                    uint8_t blocks = p_codec_info->cie.sbc_codec.block_len;
                    uint8_t bitpool = p_codec_info->cie.sbc_codec.bit_pool;
                    if(chnl_mode == A2DP_SBC_CHANNEL_MONO || chnl_mode == A2DP_SBC_CHANNEL_DUAL)
                    {
                         frame_length = 4 + ((4 * subbands * chnls)>>3) + ((blocks*chnls*bitpool+7)>>3);
                    }else if(chnl_mode == A2DP_SBC_CHANNEL_STEREO)
                    {
                        frame_length = 4 + ((4 * subbands * chnls)>>3) + ((blocks*bitpool+7)>>3);
                    }else //A2DP_SBC_CHANNEL_JOINT_STEREO
                    {
                        frame_length = 4 + ((4 * subbands * chnls)>>3) + ((subbands+blocks*bitpool+7)>>3);
                    }

                    org_frame_len = frame_length;
                    frame_length *= 2;
                    //get_sbc_encoder_len(p_codec_info);
                    //A2DP_SBC_MAX_FRAME_NUMS
                    decoder_type = BLUE_AUDIO_DECODER_TYPE_SBC;
                    LOGI("cm:%d, c:%d, s:%d, b:%d, bi:%d, frame_length:%d \n",chnl_mode, chnls, subbands,blocks, bitpool, frame_length);
                }
#if (CONFIG_AAC_DECODER)
                else if(CODEC_AUDIO_AAC == p_codec_info->type)
                {
                    uint8_t channle = p_codec_info->cie.aac_codec.channels;
                    uint32_t sample_rate = p_codec_info->cie.aac_codec.sample_rate;
                    frame_length = A2DP_ACC_SINGLE_CHANNEL_MAX_FRAM_SIZE*channle;
                    LOGI("ch:%d, frame_length:%d \n", channle, frame_length);
                    //A2DP_AAC_MAX_FRAME_NUMS
                    decoder_type = BLUE_AUDIO_DECODER_TYPE_AAC;
                }
#endif
                if (BK_OK != bt_audio_player_open(decoder_type, frame_length, spk_task_start_vote))
                {
                    LOGE("%s bt_audio_player_open failed\n", __func__);
                    //TODO
                }
#if SLOW_SEND_FRAME_WHEN_A2DP_ENABLE
                slow_send_status = 0;
#endif
#endif
                psram_free(msg.data);
            }
            break;

            case BT_AUDIO_USER_START_MSG:
            {
                uint32_t enable = (typeof(enable))msg.data;

                LOGI("BT_AUDIO_USER_START_MSG %d\n", enable);

                if(enable)
                {
                    spk_task_start_vote |= (1 << BT_AUDIO_SPK_TASK_START_VOTE_USER);
                    if (BK_OK != bt_audio_player_open(decoder_type, frame_length, spk_task_start_vote))
                    {
                        LOGE("%s bt_audio_player_open failed\n", __func__);
                        //TODO
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
                /* set sync semaphore */
                rtos_set_semaphore(&s_audio_player_en_sema);
            }
            break;

            case BT_AUDIO_D2DP_DATA_IND_MSG:
            {
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE

#if SLOW_SEND_FRAME_WHEN_A2DP_ENABLE
                if(slow_send_status)
                {

                }
                else
                {
                    slow_send_buffer_count++;
                    if(slow_send_buffer_count < 10)
                    {

                    }
                    else
                    {
                        slow_send_buffer_count = 0;

                        if (BK_OK != blue_audio_player_start(gl_audio_player_handle))
                        {
                            LOGE("blue_audio_player_start fail \n");
                            BK_ASSERT(0);
                        }

                        slow_send_status = 1;
                    }
                }
#endif
                uint8 *fb = (uint8_t *)msg.data;
                if(gl_audio_player_handle)
                {
                    if (CODEC_AUDIO_SBC == bt_audio_a2dp_sink_codec.type)
                    {
                        uint8_t payload_header = *fb++;
                        uint8_t frame_num = payload_header & 0xF;

                        /* frame length change, bitpool change */
                        if(msg.len - 1 != org_frame_len * frame_num)
                        {
                            uint8_t detect_frame_num = 0;
                            uint32_t i = 0;

                            LOGD("%s frame num not match, payload_header 0x%x, payload_len: %d, frame_num:%d %d %d\n", __func__, payload_header, msg.len - 1, frame_num, frame_length, org_frame_len);

                            for (uint32_t i = 0; i < msg.len - 1;)
                            {
                                int32_t tmp_frame_len = bk_sbc_frame_length_parse(fb, msg.len - 1);
                                if(tmp_frame_len > 0)
                                {
                                    if (blue_audio_player_get_free_frame_num(gl_audio_player_handle))
                                    {
                                        blue_audio_player_write_frame_data(gl_audio_player_handle, (char *)(fb + i), tmp_frame_len);
                                    }
                                    else
                                    {
                                        LOGW("%s blue_audio_player frame nodes buffer(sbc) is full \n", __func__);
                                        goto SEND_A2DP_DATA_END;
                                    }

                                    i += tmp_frame_len;
                                }
                                else
                                {
                                    LOGE("%s index %d is not 0x9c (0x%x)!!!\n", __func__, i, fb[i]);
                                    goto SEND_A2DP_DATA_END;
                                }
                            }
                        }
                        else
                        {
                            for(uint8_t i = 0; i < frame_num; i++)
                            {
                                if (blue_audio_player_get_free_frame_num(gl_audio_player_handle))
                                {
                                    uint16_t tmp_len = (msg.len - 1) / frame_num;
                                    blue_audio_player_write_frame_data(gl_audio_player_handle, (char *)fb, tmp_len);
                                    fb += tmp_len;
                                }
                                else
                                {
                                    LOGW("%s blue_audio_player frame nodes buffer(sbc) is full \n", __func__);
                                    //psram_free(msg.data);
                                    break;
                                }
                            }
                        }
                    }
#if CONFIG_AAC_DECODER
                    else if(CODEC_AUDIO_AAC == bt_audio_a2dp_sink_codec.type)
                    {
                        // LOGI("-> %d \n", msg.len);
                        uint8_t *inbuf = &fb[9];
                        uint32_t inlen = 0;
                        uint8_t  len   = 255;

                        do
                        {
                            inlen += len = *inbuf++;
                        }
                        while (len == 255);

                        {
                            if (blue_audio_player_get_free_frame_num(gl_audio_player_handle))
                            {
                                uint8_t *output = 0;
                                uint32_t output_len = 0;

                                if(mpeg4_latm_decode(fb, msg.len, &output, &output_len))
                                {
                                    LOGE("====%s latm decode err, discard it\n", __func__);
                                    psram_free(msg.data);
                                    break;
                                }
                                else if(msg.len - (output - fb) != output_len ||
                                                inlen != output_len ||
                                                output != inbuf)
                                {
                                    LOGE("====%s len not match, total %d, type1 offset %d len %d, type2 offset %d len %d\n", __func__, msg.len, output - fb, inbuf - fb, output_len, inlen);
                                }

                                blue_audio_player_write_frame_data(gl_audio_player_handle, (char *)inbuf, inlen);
                            }
                            else
                            {
                                LOGI("%s blue_audio_player frame nodes buffer(aac) is full \n", __func__);
                                psram_free(msg.data);
                                break;
                            }
                        }
                    }
#endif
                    else
                    {
                        LOGE("%s, Unsupported a2dp codec %d \r\n", __func__, bt_audio_a2dp_sink_codec.type);
                    }
                }
SEND_A2DP_DATA_END:;
#endif
                psram_free(msg.data);
            }
            break;

            case BT_AUDIO_D2DP_STOP_MSG:
            case BT_AUDIO_EXIT_MSG:
            {
                if(msg.type == BT_AUDIO_D2DP_STOP_MSG)
                {
                    LOGI("BT_AUDIO_D2DP_STOP_MSG\n");
                }
                else
                {
                    LOGI("BT_AUDIO_EXIT_MSG\n");
                }

#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
                /* destroy blue_audio_player */
                spk_task_start_vote &= ~(1 << BT_AUDIO_SPK_TASK_START_VOTE_A2DP);
                if(gl_audio_player_handle)
                {
                    LOGI("%s start blue_audio_player_stop/destroy\n", __func__);
                    blue_audio_player_stop(gl_audio_player_handle);
                    blue_audio_player_destroy(gl_audio_player_handle);
                    LOGI("%s blue_audio_player_stop/destroy end\n", __func__);
                    gl_audio_player_handle = NULL;
                }
#endif

                if(msg.type == BT_AUDIO_EXIT_MSG)
                {
                    goto exit;
                }
            }
            break;

            case BT_AUDIO_D2DP_SEND_DATA_2_SPK_MSG:
            {

            }
            break;

            case BT_AUDIO_WIFI_STATE_UPDATE_MSG:
            {
                LOGI("BT_AUDIO_WIFI_STATE_UPDATE_MSG \r\n");
                uint8_t reconn_addr[8] = {0};
                bt_manager_get_reconnect_device(reconn_addr);
                if (0 == s_bt_env.wifi_state && BT_STATE_WAIT_FOR_RECONNECT == bt_manager_get_connect_state())
                {
                    bt_manager_start_reconnect(reconn_addr, 1);
                }

                if (s_bt_env.wifi_state && BT_STATE_RECONNECTING == bt_manager_get_connect_state())
                {
                   bk_bt_gap_create_conn_cancel(reconn_addr);
                }
            }
            break;

            default:
                break;
            }
        }
    }

exit:
//    rtos_deinit_queue(&bt_audio_sink_demo_msg_que);
//    bt_audio_sink_demo_msg_que = NULL;
//    bt_audio_sink_demo_thread_handle = NULL;

    frame_length = 0;

    rtos_delete_thread(NULL);
}

static int bt_audio_sink_demo_task_init(void)
{
    bk_err_t ret = BK_OK;

    if ((!bt_audio_sink_demo_thread_handle) && (!bt_audio_sink_demo_msg_que))
    {
        ret = rtos_init_queue(&bt_audio_sink_demo_msg_que,
                              "bt_audio_sink_demo_msg_que",
                              sizeof(bt_audio_sink_demo_msg_t),
                              BT_AUDIO_SINK_DEMO_MSG_COUNT);

        if (ret != kNoErr)
        {
            LOGE("bt_audio sink demo msg queue failed \r\n");
            return BK_FAIL;
        }

        ret = rtos_create_thread(&bt_audio_sink_demo_thread_handle,
                                 A2DP_SINK_DEMO_TASK_PRIORITY,
                                 "bt_audio_sink_demo",
                                 (beken_thread_function_t)bt_audio_sink_demo_main,
                                 4096,
                                 (beken_thread_arg_t)0);

        if (ret != kNoErr)
        {
            LOGE("bt_audio sink demo task fail \r\n");
            rtos_deinit_queue(&bt_audio_sink_demo_msg_que);
            bt_audio_sink_demo_msg_que = NULL;
            bt_audio_sink_demo_thread_handle = NULL;
        }

        return kNoErr;
    }
    else
    {
        return kInProgressErr;
    }
}

static int bt_audio_sink_demo_task_deinit(void)
{
    bk_err_t ret = BK_OK;

    if (bt_audio_sink_demo_thread_handle)
    {
        bt_audio_sink_task_exit();

        LOGI("%s wait demo task end\n", __func__);
        rtos_thread_join(&bt_audio_sink_demo_thread_handle);
        LOGI("%s demo task end !!!\n", __func__);
        bt_audio_sink_demo_thread_handle = NULL;

        if (bt_audio_sink_demo_msg_que)
        {
            bk_err_t err = 0;
            bt_audio_sink_demo_msg_t msg = {0};

            while ((err = rtos_pop_from_queue(&bt_audio_sink_demo_msg_que, &msg, 0)) == 0)
            {
                switch (msg.type)
                {
                case BT_AUDIO_D2DP_DATA_IND_MSG:
                    if (msg.data)
                    {
                        psram_free(msg.data);
                        msg.data = NULL;
                    }

                    break;

                default:
                    break;
                }

                os_memset(&msg, 0, sizeof(msg));
            }

            rtos_deinit_queue(&bt_audio_sink_demo_msg_que);
            bt_audio_sink_demo_msg_que = NULL;
        }
    }

    (void)ret;
    return 0;
}

static void bt_audio_sink_media_data_ind(const uint8_t *data, uint16_t data_len)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_sink_demo_msg_t));

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return;
    }

    demo_msg.data = (char *) psram_malloc(data_len);

    if (demo_msg.data == NULL)
    {
        LOGE("%s, malloc failed\r\n", __func__);
        return;
    }

    os_memcpy(demo_msg.data, data, data_len);
    demo_msg.type = BT_AUDIO_D2DP_DATA_IND_MSG;
    demo_msg.len = data_len;

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);

        if (demo_msg.data)
        {
            psram_free(demo_msg.data);
        }
    }
}

static void bt_audio_a2dp_sink_suspend_ind(void)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_sink_demo_msg_t));

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return;
    }

    demo_msg.type = BT_AUDIO_D2DP_STOP_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);
    }
}

static void bt_audio_a2dp_sink_start_ind(bk_a2dp_mcc_t *codec)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_sink_demo_msg_t));

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return;
    }

    demo_msg.data = (char *) psram_malloc(sizeof(bk_a2dp_mcc_t));

    if (demo_msg.data == NULL)
    {
        LOGE("%s, malloc failed\r\n", __func__);
        return;
    }

    os_memcpy(demo_msg.data, codec, sizeof(bk_a2dp_mcc_t));
    demo_msg.type = BT_AUDIO_D2DP_START_MSG;
    demo_msg.len = sizeof(bk_a2dp_mcc_t);

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);
    }
}

static bk_a2dp_audio_state_t s_audio_state = BK_A2DP_AUDIO_STATE_SUSPEND;

static void bk_bt_app_a2dp_sink_cb(bk_a2dp_cb_event_t event, bk_a2dp_cb_param_t *p_param)
{
    LOGI("%s event: %d\r\n", __func__, event);

    bk_a2dp_cb_param_t *a2dp = (bk_a2dp_cb_param_t *)(p_param);

    switch (event)
    {
    case BK_A2DP_PROF_STATE_EVT:
    {
        LOGI("a2dp prof init action %d status %d reason %d\r\n",
                        p_param->a2dp_prof_stat.action, p_param->a2dp_prof_stat.status, p_param->a2dp_prof_stat.reason);

        if (!p_param->a2dp_prof_stat.status)
        {
            if (s_bt_api_event_cb_sema)
            {
                rtos_set_semaphore( &s_bt_api_event_cb_sema );
            }
        }
    }
    break;

    case BK_A2DP_CONNECTION_STATE_EVT:
    {
        uint8_t *bda = a2dp->conn_state.remote_bda;
        LOGI("A2DP connection state: %d, [%02x:%02x:%02x:%02x:%02x:%02x]\r\n",
                  a2dp->conn_state.state, bda[5], bda[4], bda[3], bda[2], bda[1], bda[0]);

        if (BK_A2DP_CONNECTION_STATE_DISCONNECTED == a2dp->conn_state.state)
        {
            s_bt_env.a2dp_state = 0;
            if (BK_A2DP_AUDIO_STATE_STARTED == s_audio_state)
            {
                s_audio_state = BK_A2DP_AUDIO_STATE_SUSPEND;
                bt_audio_a2dp_sink_suspend_ind();
            }
            if(s_a2dp_connect_sema)
            {
                rtos_set_semaphore(&s_a2dp_connect_sema);
            }
        }
        else if (BK_A2DP_CONNECTION_STATE_CONNECTED == a2dp->conn_state.state)
        {
            bt_manager_set_connect_state(BT_STATE_PROFILE_CONNECTED);
            s_bt_env.a2dp_state = 1;
            //bluetooth_storage_update_to_newest(bda);
            //bluetooth_storage_sync_to_flash();
            if (0 == s_bt_env.avrcp_state)
            {
                bk_bt_start_avrcp_connect();
            }
        }
    }
    break;

    case BK_A2DP_AUDIO_STATE_EVT:
    {
        LOGI("A2DP audio state: %d\r\n", a2dp->audio_state.state);

        if (BK_A2DP_AUDIO_STATE_STARTED == a2dp->audio_state.state)
        {
            s_audio_state = a2dp->audio_state.state;
            bt_audio_a2dp_sink_start_ind(&bt_audio_a2dp_sink_codec);
        }
        else if ((BK_A2DP_AUDIO_STATE_SUSPEND == a2dp->audio_state.state) && (BK_A2DP_AUDIO_STATE_STARTED == s_audio_state))
        {
            s_audio_state = a2dp->audio_state.state;
            bt_audio_a2dp_sink_suspend_ind();
        }
    }
    break;

    case BK_A2DP_AUDIO_CFG_EVT:
    {
        bt_audio_a2dp_sink_codec = a2dp->audio_cfg.mcc;
        LOGI("%s, codec_id %d \r\n", __func__, bt_audio_a2dp_sink_codec.type);
    }
    break;

    case BK_A2DP_L2CAP_CONNECT_REQ_EVT:
    {
        struct a2dp_l2cap_connect_req_param *param = (typeof(param))p_param;

        LOGI("%s BK_A2DP_L2CAP_CONNECT_REQ_EVT %02x:%02x:%02x:%02x:%02x:%02x, %s\n", __func__,
                        param->remote_bda[5],
                        param->remote_bda[4],
                        param->remote_bda[3],
                        param->remote_bda[2],
                        param->remote_bda[1],
                        param->remote_bda[0],
                        s_auto_accept_connect_req ? "accept" : "reject");

        param->accept = s_auto_accept_connect_req;

        if(!param->accept)
        {
            bt_manager_set_connect_state(BT_STATE_PROFILE_CONNECTED);
        }
    }
    break;

    case BK_A2DP_SET_CAP_COMPLETED_EVT:
    {
        struct a2dp_set_cap_completed_param *param = (typeof(param))p_param;
        LOGI("%s set cap status 0x%x\n", __func__, param->status);

        if (s_bt_api_event_cb_sema)
        {
            rtos_set_semaphore( &s_bt_api_event_cb_sema );
        }
    }
    break;

    default:
        LOGW("Invalid A2DP event: %d\r\n", event);
        break;
    }
}

static void gap_event_cb(bk_gap_bt_cb_event_t event, bk_bt_gap_cb_param_t *param)
{
    switch (event)
    {
        case BK_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        break;

        case BK_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        break;

        case BK_BT_GAP_AUTH_CMPL_EVT:
        break;

        case BK_BT_GAP_LINK_KEY_NOTIF_EVT:
        {
            s_a2dp_vol = DEFAULT_A2DP_VOLUME;
            bluetooth_storage_save_volume(param->link_key_notif.bda, s_a2dp_vol);
        }
        break;

        case BK_BT_GAP_LINK_KEY_REQ_EVT:
        {
        }
        break;

        default:
            break;
    }

}

uint8_t avrcp_remote_bda[6] = {0};

static void bt_av_notify_evt_handler(uint8_t event_id, bk_avrcp_rn_param_t *event_parameter)
{
    switch (event_id)
    {
        /* when track status changed, this event comes */
        case BK_AVRCP_RN_PLAY_STATUS_CHANGE:
        {
            LOGI("Playback status changed: 0x%x\r\n", event_parameter->playback);
            bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_PLAY_STATUS_CHANGE, 0);
        }
        break;

        case BK_AVRCP_RN_TRACK_CHANGE:
        {
            uint64_t track = 0;
            os_memcpy(&track, event_parameter->elm_id, sizeof(event_parameter->elm_id));
            LOGI("track changed: %lld\n", track);
            bk_bt_app_avrcp_ct_get_attr(BK_AVRCP_MEDIA_ATTR_ID_TITLE);
            bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_TRACK_CHANGE, 0);
        }
        break;

        case BK_AVRCP_RN_PLAY_POS_CHANGED:
        {
            LOGI("play pos changed: %d ms\n", event_parameter->play_pos);
            bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_PLAY_POS_CHANGED, 1);
        }
        break;

        case BK_AVRCP_RN_AVAILABLE_PLAYERS_CHANGE:
        {
            LOGI("avaliable player changed\n");
            bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_AVAILABLE_PLAYERS_CHANGE, 0);
        }
        break;

        /* others */
        default:
        LOGW("unhandled event: %d\r\n", event_id);
            break;
    }
}

static void bk_bt_app_avrcp_ct_cb(bk_avrcp_ct_cb_event_t event, bk_avrcp_ct_cb_param_t *param)
{
    LOGI("%s event: %d\r\n", __func__, event);

    bk_avrcp_ct_cb_param_t *avrcp = (bk_avrcp_ct_cb_param_t *)(param);

    switch (event)
    {
        case BK_AVRCP_CT_CONNECTION_STATE_EVT:
        {
            uint8_t *bda = avrcp->conn_state.remote_bda;
        LOGI("AVRCP CT connection state: %d, [%02x:%02x:%02x:%02x:%02x:%02x]\r\n",
                      avrcp->conn_state.connected, bda[5], bda[4], bda[3], bda[2], bda[1], bda[0]);

        s_bt_env.avrcp_state = avrcp->conn_state.connected;
            if (avrcp->conn_state.connected)
            {
                os_memcpy(avrcp_remote_bda, bda, 6);
                bk_bt_avrcp_ct_send_get_rn_capabilities_cmd(bda);
            }
            else
            {
                os_memset(avrcp_remote_bda, 0x0, sizeof(avrcp_remote_bda));
            }
        }
        break;

        case BK_AVRCP_CT_PASSTHROUGH_RSP_EVT:
        {
            struct avrcp_ct_psth_rsp_param *pm = (typeof(pm))param;

            LOGI("AVRCP psth rsp 0x%x op 0x%x release %d tl %d %02x:%02x:%02x:%02x:%02x:%02x\n",
                       pm->rsp_code,
                       pm->key_code,
                       pm->key_state,
                       pm->tl,
                       pm->remote_bda[5],
                       pm->remote_bda[4],
                       pm->remote_bda[3],
                       pm->remote_bda[2],
                       pm->remote_bda[1],
                       pm->remote_bda[0]);
            if(s_bt_avrcp_event_cb_sema
                && pm->key_state == BK_AVRCP_PT_CMD_STATE_PRESSED //press state set sem
                && (pm->key_code == BK_AVRCP_PT_CMD_PLAY
                ||  pm->key_code == BK_AVRCP_PT_CMD_PAUSE
                ||  pm->key_code == BK_AVRCP_PT_CMD_FORWARD
                ||  pm->key_code == BK_AVRCP_PT_CMD_BACKWARD
                ||  pm->key_code == BK_AVRCP_PT_CMD_VOL_DOWN
                ||  pm->key_code == BK_AVRCP_PT_CMD_VOL_UP) )
            {
                rtos_set_semaphore(&s_bt_avrcp_event_cb_sema);
            }
        }
        break;

        case BK_AVRCP_CT_GET_RN_CAPABILITIES_RSP_EVT:
        {
            LOGI("AVRCP peer supported notification events 0x%x %02x:%02x:%02x:%02x:%02x:%02x\n", avrcp->get_rn_caps_rsp.evt_set.bits,
                            avrcp->get_rn_caps_rsp.remote_bda[5],
                            avrcp->get_rn_caps_rsp.remote_bda[4],
                            avrcp->get_rn_caps_rsp.remote_bda[3],
                            avrcp->get_rn_caps_rsp.remote_bda[2],
                            avrcp->get_rn_caps_rsp.remote_bda[1],
                            avrcp->get_rn_caps_rsp.remote_bda[0]);

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_PLAY_STATUS_CHANGE))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_PLAY_STATUS_CHANGE, 0);
            }

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_TRACK_CHANGE))
            {
                //bk_bt_app_avrcp_ct_get_attr(BK_AVRCP_MEDIA_ATTR_ID_TITLE);
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_TRACK_CHANGE, 0);
            }
#if 0
            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_PLAY_POS_CHANGED))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_PLAY_POS_CHANGED, 1);
            }

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_SYSTEM_STATUS_CHANGE))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_SYSTEM_STATUS_CHANGE, 0);
            }

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_APP_SETTING_CHANGE))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_APP_SETTING_CHANGE, 0);
            }

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_AVAILABLE_PLAYERS_CHANGE))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_AVAILABLE_PLAYERS_CHANGE, 0);
            }

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_ADDRESSED_PLAYER_CHANGE))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_ADDRESSED_PLAYER_CHANGE, 0);
            }

            if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_UIDS_CHANGE))
            {
                bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_UIDS_CHANGE, 0);
            }
#endif
        }
        break;

        case BK_AVRCP_CT_CHANGE_NOTIFY_EVT:
        {
            LOGI("AVRCP event notification: %d %02x:%02x:%02x:%02x:%02x:%02x\n", avrcp->change_ntf.event_id,
                            avrcp->change_ntf.remote_bda[5],
                            avrcp->change_ntf.remote_bda[4],
                            avrcp->change_ntf.remote_bda[3],
                            avrcp->change_ntf.remote_bda[2],
                            avrcp->change_ntf.remote_bda[1],
                            avrcp->change_ntf.remote_bda[0]);

            bt_av_notify_evt_handler(avrcp->change_ntf.event_id, &avrcp->change_ntf.event_parameter);
        }
        break;

        case BK_AVRCP_CT_GET_ELEM_ATTR_RSP_EVT:
        {
            struct avrcp_ct_elem_attr_rsp_param *pm = (typeof(pm))param;
            LOGI("%s get elem rsp status %d count %d %02x:%02x:%02x:%02x:%02x:%02x\n", __func__, pm->status, pm->attr_count,
                            pm->remote_bda[5],
                            pm->remote_bda[4],
                            pm->remote_bda[3],
                            pm->remote_bda[2],
                            pm->remote_bda[1],
                            pm->remote_bda[0]);

            for (uint32_t i = 0; i < pm->attr_count; ++i)
            {
                LOGI("%s get elem attr 0x%x charset 0x%x len %d\n", __func__, pm->attr_array[i].attr_id, pm->attr_array[i].attr_text_charset, pm->attr_array[i].attr_length);
            }
        }
        break;

        default:
            LOGW("Invalid AVRCP event: %d\r\n", event);
        break;
    }
}
static void avrcp_tg_cb(bk_avrcp_tg_cb_event_t event, bk_avrcp_tg_cb_param_t *param)
{
    int ret = 0;

    //LOGI("%s event: %d\n", __func__, event);

    switch (event)
    {
    case BK_AVRCP_TG_CONNECTION_STATE_EVT:
    {
        uint8_t status = param->conn_stat.connected;
        uint8_t *bda = param->conn_stat.remote_bda;
        LOGI("%s avrcp tg connection state: %d, [%02x:%02x:%02x:%02x:%02x:%02x]\n", __func__,
                  status, bda[5], bda[4], bda[3], bda[2], bda[1], bda[0]);

        if (!status)
        {
            s_tg_current_registered_noti = 0;
        }
    }
    break;

    case BK_AVRCP_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
    {
        LOGI("%s recv abs vol 0x%x %02x:%02x:%02x:%02x:%02x:%02x\n", __func__, param->set_abs_vol.volume,
                        param->set_abs_vol.remote_bda[5],
                        param->set_abs_vol.remote_bda[4],
                        param->set_abs_vol.remote_bda[3],
                        param->set_abs_vol.remote_bda[2],
                        param->set_abs_vol.remote_bda[1],
                        param->set_abs_vol.remote_bda[0]);

        s_a2dp_vol = param->set_abs_vol.volume;

        bk_bt_dac_set_gain(s_a2dp_vol);
        if (s_a2dp_vol)
        {
            //sys_hal_aud_dacmute_en(0); //TODO
        }
        else
        {
            //sys_hal_aud_dacmute_en(1); //TODO
        }
        bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
    }
    break;

    case BK_AVRCP_TG_REGISTER_NOTIFICATION_EVT:
    {
        LOGI("%s recv reg evt 0x%x param %d %02x:%02x:%02x:%02x:%02x:%02x\n", __func__, param->reg_ntf.event_id, param->reg_ntf.event_parameter,
                        param->reg_ntf.remote_bda[5],
                        param->reg_ntf.remote_bda[4],
                        param->reg_ntf.remote_bda[3],
                        param->reg_ntf.remote_bda[2],
                        param->reg_ntf.remote_bda[1],
                        param->reg_ntf.remote_bda[0]);

        s_tg_current_registered_noti |= (1 << param->reg_ntf.event_id);

        bk_avrcp_rn_param_t cmd;

        os_memset(&cmd, 0, sizeof(cmd));

        switch (param->reg_ntf.event_id)
        {
        case BK_AVRCP_RN_VOLUME_CHANGE:
            cmd.volume = s_a2dp_vol;
            break;

        default:
            LOGW("%s unknow reg event 0x%x\n", __func__, param->reg_ntf.event_id);
            goto error;
        }

        ret = bk_bt_avrcp_tg_send_rn_rsp(avrcp_remote_bda, param->reg_ntf.event_id, BK_AVRCP_RN_RSP_INTERIM, &cmd);

        if (ret)
        {
            LOGE("%s send rn rsp err %d\n", __func__, ret);
        }
    }
    break;

    default:
        LOGW("%s unknow event 0x%x\n", __func__, event);
        break;
    }

error:;
}

void bk_bt_app_avrcp_ct_play(void)
{
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_PLAY, BK_AVRCP_PT_CMD_STATE_PRESSED);
    if(s_bt_avrcp_event_cb_sema)
    {
        rtos_get_semaphore(&s_bt_avrcp_event_cb_sema, AVRCP_PASSTHROUTH_CMD_TIMEOUT);
    }
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_PLAY, BK_AVRCP_PT_CMD_STATE_RELEASED);
}

void bk_bt_app_avrcp_ct_pause(void)
{
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_PAUSE, BK_AVRCP_PT_CMD_STATE_PRESSED);
    if(s_bt_avrcp_event_cb_sema)
    {
        rtos_get_semaphore(&s_bt_avrcp_event_cb_sema, AVRCP_PASSTHROUTH_CMD_TIMEOUT);
    }
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_PAUSE, BK_AVRCP_PT_CMD_STATE_RELEASED);
}

void bk_bt_app_avrcp_ct_next(void)
{
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_FORWARD, BK_AVRCP_PT_CMD_STATE_PRESSED);
    if(s_bt_avrcp_event_cb_sema)
    {
        rtos_get_semaphore(&s_bt_avrcp_event_cb_sema, AVRCP_PASSTHROUTH_CMD_TIMEOUT);
    }
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_FORWARD, BK_AVRCP_PT_CMD_STATE_RELEASED);
}

void bk_bt_app_avrcp_ct_prev(void)
{
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_BACKWARD, BK_AVRCP_PT_CMD_STATE_PRESSED);
    if(s_bt_avrcp_event_cb_sema)
    {
        rtos_get_semaphore(&s_bt_avrcp_event_cb_sema, AVRCP_PASSTHROUTH_CMD_TIMEOUT);
    }
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_BACKWARD, BK_AVRCP_PT_CMD_STATE_RELEASED);
}

void bk_bt_app_avrcp_ct_rewind(uint32_t ms)
{
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_REWIND, BK_AVRCP_PT_CMD_STATE_PRESSED);

    if(ms)
    {
        rtos_delay_milliseconds(ms);
    }

    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_REWIND, BK_AVRCP_PT_CMD_STATE_RELEASED);
}

void bk_bt_app_avrcp_ct_fast_forward(uint32_t ms)
{
    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_FAST_FORWARD, BK_AVRCP_PT_CMD_STATE_PRESSED);

    if(ms)
    {
        rtos_delay_milliseconds(ms);
    }

    bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_FAST_FORWARD, BK_AVRCP_PT_CMD_STATE_RELEASED);
}

void bk_bt_app_avrcp_ct_vol_up(void)
{
    uint8_t idx = (s_a2dp_vol + 5) >> 3;
    uint8_t old = s_a2dp_vol;
    if (idx < 16)
    {
        idx += 1;
        s_a2dp_vol = (idx <= 0) ? 0 : (idx >= 16) ? 0x7f :((idx-1) * 8 + 9);
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
        bk_bt_dac_set_gain(s_a2dp_vol);
        if (s_a2dp_vol)
        {
            //sys_hal_aud_dacmute_en(0);//TODO
        }
        else
        {
            //sys_hal_aud_dacmute_en(1);//TODO
        }

        if (s_tg_current_registered_noti & (1 << BK_AVRCP_RN_VOLUME_CHANGE))
        {
            bk_avrcp_rn_param_t cmd;
            int ret = 0;

            os_memset(&cmd, 0, sizeof(cmd));

            cmd.volume = s_a2dp_vol;

            ret = bk_bt_avrcp_tg_send_rn_rsp(avrcp_remote_bda, BK_AVRCP_RN_VOLUME_CHANGE, BK_AVRCP_RN_RSP_CHANGED, &cmd);

            if (ret)
            {
                LOGE("%s send rn rsp err %d\n", __func__, ret);
            }

            bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
        }
        else if(0)
        {
            LOGE("%s peer not reg vol change, adjust local only !!!\n", __func__);
            bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
        }
        else
        {
            bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_VOL_UP, BK_AVRCP_PT_CMD_STATE_PRESSED);
            if(s_bt_avrcp_event_cb_sema)
            {
                rtos_get_semaphore(&s_bt_avrcp_event_cb_sema, AVRCP_PASSTHROUTH_CMD_TIMEOUT);
            }
            bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_VOL_UP, BK_AVRCP_PT_CMD_STATE_RELEASED);

            bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
        }
#endif
        LOGI("vol_up, vol: %d -> %d\r\n", old, s_a2dp_vol);
    }
}

void bk_bt_app_avrcp_ct_vol_down(void)
{
    uint8_t idx = (s_a2dp_vol + 5) >> 3;
    uint8_t old = s_a2dp_vol;

    if (idx > 0)
    {
        idx -= 1;
        s_a2dp_vol = (idx <= 0) ? 0 : (idx >= 16) ? 0x7f :((idx-1) * 8 + 9);
#if CONFIG_BLUE_AUDIO_PLAYER_SERVICE
        bk_bt_dac_set_gain(s_a2dp_vol);
        if (s_a2dp_vol)
        {
            //sys_hal_aud_dacmute_en(0);//TODO
        }
        else
        {
            //sys_hal_aud_dacmute_en(1);//TODO
        }

        if (s_tg_current_registered_noti & (1 << BK_AVRCP_RN_VOLUME_CHANGE))
        {
            bk_avrcp_rn_param_t cmd;
            int ret = 0;

            os_memset(&cmd, 0, sizeof(cmd));

            cmd.volume = s_a2dp_vol;

            ret = bk_bt_avrcp_tg_send_rn_rsp(avrcp_remote_bda, BK_AVRCP_RN_VOLUME_CHANGE, BK_AVRCP_RN_RSP_CHANGED, &cmd);

            if (ret)
            {
                LOGE("%s send rn rsp err %d\n", __func__, ret);
            }

            bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
        }
        else if(0)
        {
            LOGE("%s peer not reg vol change, adjust local only !!!\n", __func__);
            bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
        }
        else
        {
            bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_VOL_DOWN, BK_AVRCP_PT_CMD_STATE_PRESSED);
            if(s_bt_avrcp_event_cb_sema)
            {
                rtos_get_semaphore(&s_bt_avrcp_event_cb_sema, AVRCP_PASSTHROUTH_CMD_TIMEOUT);
            }
            bk_bt_avrcp_ct_send_passthrough_cmd(avrcp_remote_bda, BK_AVRCP_PT_CMD_VOL_DOWN, BK_AVRCP_PT_CMD_STATE_RELEASED);

            bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
        }
#endif
        LOGI("vol_down, vol: %d -> %d\r\n", old, s_a2dp_vol);
    }
}

static void bt_wifi_state_updated(void)
{
    bt_audio_sink_demo_msg_t demo_msg;
    int rc = -1;

    os_memset(&demo_msg, 0x0, sizeof(bt_audio_sink_demo_msg_t));

    if (bt_audio_sink_demo_msg_que == NULL)
    {
        return;
    }

    demo_msg.type = BT_AUDIO_WIFI_STATE_UPDATE_MSG;
    demo_msg.len = 0;

    rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);

    if (kNoErr != rc)
    {
        LOGE("%s, send queue failed\r\n", __func__);
    }
}

#if CONFIG_WIFI_COEX_SCHEME
static void wifi_state_callback(uint8_t status_id, uint8_t status_info)
{
    if (COEX_WIFI_STAT_ID_SCANNING == status_id || COEX_WIFI_STAT_ID_CONNECTING == status_id)
    {
        if (status_info)
        {
            s_bt_env.wifi_state |= (1 << status_id);
        }
        else
        {
            s_bt_env.wifi_state &= ~(1 << status_id);
        }
        bt_wifi_state_updated();
    }
}

static void coex_regisiter_wifi_state(void)
{
    os_memset(&s_coex_to_bt_func, 0, sizeof(s_coex_to_bt_func));
    s_coex_to_bt_func.version = 0x01;
    s_coex_to_bt_func.inform_wifi_status = wifi_state_callback;

    coex_bt_if_init(&s_coex_to_bt_func);
}
#endif

int32_t bk_bt_app_avrcp_ct_get_attr(uint32_t attr)
{
    uint32_t media_attr_id_mask = (1 << BK_AVRCP_MEDIA_ATTR_ID_TITLE);

    if(attr)
    {
        media_attr_id_mask = (1 << attr);
    }

    return bk_bt_avrcp_ct_send_get_elem_attribute_cmd(avrcp_remote_bda, media_attr_id_mask);
}

static void bk_bt_a2dp_connect(uint8_t *remote_addr)
{
    if (s_bt_env.wifi_state)
    {
        bt_manager_set_connect_state(BT_STATE_WAIT_FOR_RECONNECT);
        return;
    }

    if(!s_auto_accept_connect_req)
    {
        LOGW("%s no need reconnect\n", __func__);
        return;
    }

    bk_bt_a2dp_sink_connect(remote_addr);
}

static void bk_bt_a2dp_stop_connect(void)
{
    if (rtos_is_oneshot_timer_init(&s_bt_env.avrcp_connect_tmr))
    {
        if (rtos_is_oneshot_timer_running(&s_bt_env.avrcp_connect_tmr))
        {
            rtos_stop_oneshot_timer(&s_bt_env.avrcp_connect_tmr);
        }
        rtos_deinit_oneshot_timer(&s_bt_env.avrcp_connect_tmr);
    }
}

static void bk_bt_a2dp_disconnect(uint8_t *remote_addr)
{
    LOGI("%s %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
                    remote_addr[5],
                    remote_addr[4],
                    remote_addr[3],
                    remote_addr[2],
                    remote_addr[1],
                    remote_addr[0]);

    bk_bt_a2dp_sink_disconnect(bt_manager_get_connected_device());
}

int a2dp_sink_demo_init(uint8_t aac_supported)
{
    int ret = 0;
    LOGI("%s\n", __func__);

    if (s_a2dp_sink_is_inited)
    {
        LOGE("%s already init\n", __func__);
        return -1;
    }

    if (aac_supported)
    {
#if (!CONFIG_AAC_DECODER)
        LOGI("%s AAC is not supported!\n", __func__);
        return -1;
#endif
    }

    bk_avrcp_rn_evt_cap_mask_t tmp_cap = {0};
    bk_avrcp_rn_evt_cap_mask_t final_cap =
    {
        .bits = 0
        | (1 << BK_AVRCP_RN_VOLUME_CHANGE)
        ,
    };

    if (!s_bt_api_event_cb_sema)
    {
        ret = rtos_init_semaphore(&s_bt_api_event_cb_sema, 1);

        if (ret)
        {
            LOGE("%s sem init err %d\n", __func__, ret);
            return -1;
        }
    }
    if(!s_bt_avrcp_event_cb_sema)
    {
        ret = rtos_init_semaphore(&s_bt_avrcp_event_cb_sema, 1);

        if (ret)
        {
            LOGE("%s avrcp evt sem init err %d\n", __func__, ret);
            return -1;
        }
    }

    if (!s_audio_player_en_sema)
    {
        ret = rtos_init_semaphore(&s_audio_player_en_sema, 1);
        if (ret)
        {
            LOGE("%s audio player semaphore init err %d\n", __func__, ret);
            return -1;
        }
    }

#if CONFIG_WIFI_COEX_SCHEME
    coex_regisiter_wifi_state();
#endif

    btm_callback_s btm_cb =
    {
        .gap_cb = gap_event_cb,
        .start_connect_cb = bk_bt_a2dp_connect,
        .start_disconnect_cb = bk_bt_a2dp_disconnect,
        .stop_connect_cb = bk_bt_a2dp_stop_connect,
    };
    s_a2dp_sink_bt_manager_index = bt_manager_register_callback(&btm_cb);

    bt_audio_sink_demo_task_init();

    bk_bt_avrcp_ct_init();
    bk_bt_avrcp_ct_register_callback(bk_bt_app_avrcp_ct_cb);

    bk_bt_avrcp_tg_init();

    bk_bt_avrcp_tg_get_rn_evt_cap(BK_AVRCP_RN_CAP_API_METHOD_ALLOWED, &tmp_cap);

    final_cap.bits &= tmp_cap.bits;

    LOGI("%s set rn cap 0x%x\n", __func__, final_cap.bits);

    ret = bk_bt_avrcp_tg_set_rn_evt_cap(&final_cap);

    if (ret)
    {
        LOGE("%s set rn cap err %d\n", __func__, ret);
        return -1;
    }

    bk_bt_avrcp_tg_register_callback(avrcp_tg_cb);

    ret = bk_bt_a2dp_register_callback(bk_bt_app_a2dp_sink_cb);
    if (ret)
    {
        LOGE("%s bk_bt_a2dp_register_callback err %d\n", __func__, ret);
        return -1;
    }

    ret = bk_bt_a2dp_sink_init(aac_supported);

    if (ret)
    {
        LOGI("%s a2dp sink init err %d\n", __func__, ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_bt_api_event_cb_sema, 6 * 1000);

    if (ret)
    {
        LOGI("%s get sem for a2dp sink init err\n", __func__);
        return -1;
    }

#if 1
    bk_a2dp_codec_cap_t cap =
    {
        .type = BK_A2DP_CODEC_TYPE_SBC,
        .param.sbc_codec_cap.channel_mode =
                BK_A2DP_SBC_CHANNEL_MODE_MONO
                | BK_A2DP_SBC_CHANNEL_MODE_DUAL
                | BK_A2DP_SBC_CHANNEL_MODE_STEREO
                | BK_A2DP_SBC_CHANNEL_MODE_JOINT_STEREO
                ,
        .param.sbc_codec_cap.bit_pool_max = 35,
    };

    ret = bk_bt_a2dp_set_cap(1, &cap);

    if (ret)
    {
        LOGI("%s bk_bt_a2dp_set_cap err %d\n", __func__, ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_bt_api_event_cb_sema, 6 * 1000);

    if (ret)
    {
        LOGI("%s get sem for bk_bt_a2dp_set_cap err\n", __func__);
        return -1;
    }
#endif

    ret = bk_bt_a2dp_sink_register_data_callback(&bt_audio_sink_media_data_ind);

    if (ret)
    {
        LOGE("%s bk_bt_a2dp_sink_register_data_callback err %d\n",__func__,  ret);
        return -1;
    }

    s_auto_accept_connect_req = 1;

    s_a2dp_sink_is_inited = 1;

    LOGI("%s end\n", __func__);
    return 0;
}

int a2dp_sink_demo_deinit(void)
{
    int ret = 0;
    LOGI("%s\n", __func__);

    if (!s_a2dp_sink_is_inited)
    {
        LOGE("%s already deinit\n", __func__);
        return -1;
    }

    if(s_bt_env.a2dp_state)
    {
        if (!s_a2dp_connect_sema)
        {
            if (rtos_init_semaphore(&s_a2dp_connect_sema, 1))
            {
                LOGE("%s init connect sema fail\n", __func__);
                goto end;
            }
        }

        LOGW("%s disconnecting a2dp\n", __func__);
        bk_bt_a2dp_sink_disconnect(bt_manager_get_connected_device());
        LOGW("%s wait disconnect a2dp sem\n", __func__);

        ret = rtos_get_semaphore(&s_a2dp_connect_sema, 5000);

        if(ret)
        {
            LOGE("%s wait disconnect a2dp sem err %d\n", __func__, ret);
        }
        else
        {
            LOGW("%s wait disconnect a2dp success\n", __func__);
        }
    }

    bt_audio_sink_demo_task_deinit();

#if CONFIG_WIFI_COEX_SCHEME
    coex_bt_if_init(NULL);
#endif

    bt_manager_unregister_callback(s_a2dp_sink_bt_manager_index);
    s_a2dp_sink_bt_manager_index = 0xFF;

    bk_bt_avrcp_ct_register_callback(NULL);
    bk_bt_avrcp_tg_register_callback(NULL);
    bk_bt_a2dp_register_callback(NULL);
    bk_bt_a2dp_sink_register_data_callback(NULL);


    bk_bt_avrcp_ct_deinit();

    bk_bt_avrcp_tg_deinit();

    bk_bt_a2dp_sink_deinit();

    if (s_bt_api_event_cb_sema)
    {
        rtos_deinit_semaphore(&s_bt_api_event_cb_sema);
        s_bt_api_event_cb_sema = NULL;
    }

    if (s_bt_avrcp_event_cb_sema)
    {
        rtos_deinit_semaphore(&s_bt_avrcp_event_cb_sema);
        s_bt_avrcp_event_cb_sema = NULL;
    }

    if (s_audio_player_en_sema)
    {
        rtos_deinit_semaphore(&s_audio_player_en_sema);
        s_audio_player_en_sema = NULL;
    }

    bk_bt_a2dp_stop_connect();
    os_memset(&s_bt_env, 0, sizeof(s_bt_env));

    s_a2dp_sink_is_inited = 0;

end:;

    if (s_a2dp_connect_sema)
    {
        if (rtos_deinit_semaphore(&s_a2dp_connect_sema))
        {
            LOGE("%s deinit connect sema fail\n", __func__);
        }

        s_a2dp_connect_sema = NULL;
    }

    LOGW("%s end\n", __func__);
    return 0;
}

int32_t wait_a2dp_speaker_task_end(void)
{
    while(gl_audio_player_handle)
    {
        rtos_delay_milliseconds(20);
    }

    return 0;
}

void bk_bt_app_a2dp_audio_spk_enable(uint8_t enable)
{
    bt_audio_sink_task_user_vote_spk_task(enable);
    if(s_audio_player_en_sema)
    {
        rtos_get_semaphore(&s_audio_player_en_sema, BEKEN_WAIT_FOREVER);
    }
}

void a2dp_sink_demo_set_mix(uint8_t enable)
{
    s_mix_multi_channel = enable;
}

void bk_bt_a2dp_sink_demo_set_auto_accept_connect_req(uint8_t accept)
{
    s_auto_accept_connect_req = accept;
}

int32_t bk_bt_a2dp_sink_demo_try_connect(void)
{
    int32_t ret = 0;

    if (!s_a2dp_sink_is_inited)
    {
        LOGE("%s already deinit\n", __func__);
        return -1;
    }

    LOGW("%s current bt manager status %d %d\n", __func__, bt_manager_get_connect_state(), s_bt_env.a2dp_state);

    if((BT_STATE_LINK_CONNECTED == bt_manager_get_connect_state() || BT_STATE_PROFILE_CONNECTED == bt_manager_get_connect_state())
                    && !s_bt_env.a2dp_state)
    {
        LOGW("%s start connect a2dp profile %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
                        bt_manager_get_connected_device()[5],
                        bt_manager_get_connected_device()[4],
                        bt_manager_get_connected_device()[3],
                        bt_manager_get_connected_device()[2],
                        bt_manager_get_connected_device()[1],
                        bt_manager_get_connected_device()[0]);

        ret = bk_bt_a2dp_sink_connect(bt_manager_get_connected_device());

        if(ret)
        {
            LOGE("%s bk_bt_a2dp_sink_connect err %d\n", __func__, ret);
        }
    }

    return ret;
}

int32_t bk_bt_a2dp_sink_demo_try_disconnect_current(void)
{
    int32_t ret = 0;

    if(s_bt_env.a2dp_state)
    {
        if (!s_a2dp_connect_sema)
        {
            if (rtos_init_semaphore(&s_a2dp_connect_sema, 1))
            {
                LOGE("%s init connect sema fail\n", __func__);
                ret = -1;
                goto end;
            }
        }

        LOGW("%s disconnecting a2dp\n", __func__);
        bk_bt_a2dp_sink_disconnect(bt_manager_get_connected_device());
        LOGW("%s wait disconnect a2dp sem\n", __func__);

        ret = rtos_get_semaphore(&s_a2dp_connect_sema, 5000);

        if(ret)
        {
            LOGE("%s wait disconnect a2dp sem err %d\n", __func__, ret);
        }
        else
        {
            LOGW("%s wait disconnect a2dp success\n", __func__);
        }
    }

end:;

    if (s_a2dp_connect_sema)
    {
        if (rtos_deinit_semaphore(&s_a2dp_connect_sema))
        {
            LOGE("%s deinit connect sema fail\n", __func__);
        }

        s_a2dp_connect_sema = NULL;
    }

    LOGW("%s end\n", __func__);

    return ret;
}
