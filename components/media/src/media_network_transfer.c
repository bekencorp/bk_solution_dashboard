#include "bk_private/bk_init.h"
#include <common/bk_err.h>
#include <os/os.h>
#include <string.h>

#include "media_data_process_que.h"
#include "media_network_transfer.h"
#include "network_transfer.h"

#include "media_msg.h"
#include "cast_jpeg_pipeline.h"

#define TAG "media-ntwk"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)


typedef struct
{
    uint16_t width;
    uint16_t height;
    image_format_t format;
    media_data_process_debug_info_t debug_info;
} media_video_cfg_t;

media_video_cfg_t *s_media_video_cfg = NULL;

static volatile uint8_t s_cast_video_alloc_allowed = 0U;

void media_bk_net_cast_video_alloc_gate(int allow)
{
	s_cast_video_alloc_allowed = allow ? 1U : 0U;
}

void get_last_debug_info(media_data_process_debug_info_t *info)
{
    if (s_media_video_cfg) {
        *info = s_media_video_cfg->debug_info;
    } else {
        os_memset(info, 0, sizeof(*info));
    }
}

bk_err_t media_bk_net_cntrl_recv(uint8_t *data, uint32_t length)
{
    LOGI("ctrl_recv: len=%u\n", (unsigned)length);
    return BK_OK;
}

static uint32_t s_video_recv_count = 0;
bk_err_t media_bk_net_video_recv(uint8_t *data, uint32_t length)
{
    s_video_recv_count++;
    if (s_video_recv_count <= 5 || (s_video_recv_count % 200) == 0) {
        LOGI("video_recv #%u: len=%u\n", (unsigned)s_video_recv_count, (unsigned)length);
    }
    return BK_OK;
}

bk_err_t media_bk_net_audio_recv(uint8_t *data, uint32_t length)
{
    LOGI("audio_recv: len=%u\n", (unsigned)length);
    return BK_OK;
}

static uint32_t s_malloc_count = 0;
frame_buffer_t *media_bk_net_frame_malloc(uint32_t size)
{
    if (!s_cast_video_alloc_allowed) {
        LOGW("frame_malloc: denied (cast alloc gate), size=%u\n", (unsigned)size);
        return NULL;
    }
    s_malloc_count++;
    frame_buffer_t *fb = media_frame_queue_malloc(size);
    LOGI("frame_malloc #%u: size=%u, result=%s\n",
         (unsigned)s_malloc_count, (unsigned)size, fb ? "OK" : "FAIL");
    return fb;
}

static uint32_t s_send_count = 0;
bk_err_t media_bk_net_frame_send (frame_buffer_t *data)
{
    if (data == NULL)
    {
        LOGE("%s: frame is NULL\n", __func__);
        return BK_FAIL;
    }

    /* In-flight buffers after explicit end / gate-off must not enter cast pipeline. */
    if (!s_cast_video_alloc_allowed)
    {
        LOGW("%s: drop len=%u (cast alloc gate)\n", __func__, (unsigned)data->length);
        media_frame_queue_free(data);
        return BK_OK;
    }

    s_send_count++;
    LOGI("frame_send #%u: len=%u\n", (unsigned)s_send_count, (unsigned)data->length);

    data->width = s_media_video_cfg->width;
    data->height = s_media_video_cfg->height;
    data->fmt = s_media_video_cfg->format;

    s_media_video_cfg->debug_info.wifi_transfer_frame_count++;
    s_media_video_cfg->debug_info.wifi_transfer_frame_size += data->length;

    bk_err_t ret = cast_jpeg_pipeline_push_frame_buffer(data);
    if (ret != BK_OK) {
        LOGE("frame_send: cast push failed, len=%u ret=%d\n",
            (unsigned)data->length, (int)ret);
        media_frame_queue_free(data);
        /* Back off so the WiFi recv task does not spin tight on malloc/push. */
        if (ret == BK_ERR_BUSY)
            rtos_delay_milliseconds(2);
        else
            rtos_delay_milliseconds(1);
        return ret;
    }
    return BK_OK;
}

bk_err_t media_bk_net_frame_free(frame_buffer_t *data)
{
    media_frame_queue_free(data);

    return BK_OK;
}

void media_bk_net_msg_evt_handle(ntwk_trans_event_t *event)
{
    ntwk_trans_ctxt_t *ctxt = ntwk_trans_get_ctxt();
    media_msg_t msg = {0};

    switch (event->code)
    {
        case NTWK_TRANS_EVT_START:
        {
            if (strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    msg.event = MEDIA_EVT_TCP_SERVICE_START;
                }
            }
            else if (strcmp(ctxt->service_name, "udp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    msg.event = MEDIA_EVT_UDP_SERVICE_START;
                }
            }
        } break;
        case NTWK_TRANS_EVT_CONNECTED:
        {
            if(strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                /*
                 * 原仅 CTRL 连上才开 JPEG 解码；App 若先连视频口、不配网/未连控制口，
                 * 解码器永不启动，投屏无画面、固定 JPEG 红块 demo 也不会跑。 ||
                    event->chan_type == NTWK_TRANS_CHAN_VIDEO
                 */
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = MEDIA_EVT_REMOTE_DEVICE_CONNECTED;
                }
            }
        } break;
        case NTWK_TRANS_EVT_DISCONNECTED:
        {
            if(strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = MEDIA_EVT_CAST_SESSION_TEARDOWN;
                    msg.param = MEDIA_CAST_TEARDOWN_REASON_CTRL_TCP;
                }
                else if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    /*
                     * Block unfragment malloc before the disconnect message is handled;
                     * avoids overlap with cast_jpeg_pipeline_turn_off (malloc+send
                     * same tick as teardown → heap assert in decoder deinit path).
                     */
                    media_bk_net_cast_video_alloc_gate(0);
                    msg.event = MEDIA_EVT_CAST_SESSION_TEARDOWN;
                    msg.param = MEDIA_CAST_TEARDOWN_REASON_VIDEO_TCP;
                }
            }
        } break;
        case NTWK_TRANS_EVT_STOP:
        {

        } break;
        default:
            break;
    }

    if (msg.event != 0)
    {
        if (msg.event == MEDIA_EVT_CAST_SESSION_TEARDOWN)
        {
            if (media_send_msg_wait(&msg) != BK_OK)
            {
                LOGE("%s: media_send_msg_wait(cast teardown) failed\n", __func__);
            }
        }
        else
        {
            msg.param = 0;
            media_send_msg(&msg);
        }
    }
}

bk_err_t media_tcp_network_transfer_start(char *service_name, void *param)
{
    LOGI("%s start\r\n", __func__);

    //configure message event callback
    ntwk_trans_register_msg_event_cb(media_bk_net_msg_evt_handle);

    //configure control receive callback
    ntwk_trans_register_ctrl_recv_cb(media_bk_net_cntrl_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_CTRL, NULL);

    //configure video receive callback
    ntwk_trans_register_unfragment_malloc_cb(NTWK_TRANS_CHAN_VIDEO, media_bk_net_frame_malloc);
    ntwk_trans_register_unfragment_send_cb(NTWK_TRANS_CHAN_VIDEO, media_bk_net_frame_send);
    ntwk_trans_register_unfragment_free_cb(NTWK_TRANS_CHAN_VIDEO, media_bk_net_frame_free);

    ntwk_trans_register_video_recv_cb(media_bk_net_video_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_VIDEO, NULL);

    //configure audio receive callback
    ntwk_trans_register_audio_recv_cb(media_bk_net_audio_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_AUDIO, NULL);

    return BK_OK;
}


bk_err_t media_udp_network_transfer_start(char *service_name, void *param)
{
    LOGI("%s start\r\n", __func__);

    //configure message event callback
    ntwk_trans_register_msg_event_cb(media_bk_net_msg_evt_handle);

    //configure control receive callback
    ntwk_trans_register_ctrl_recv_cb(media_bk_net_cntrl_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_CTRL, NULL);

    //configure video receive callback
    ntwk_trans_register_unfragment_malloc_cb(NTWK_TRANS_CHAN_VIDEO, media_bk_net_frame_malloc);
    ntwk_trans_register_unfragment_send_cb(NTWK_TRANS_CHAN_VIDEO, media_bk_net_frame_send);
    ntwk_trans_register_unfragment_free_cb(NTWK_TRANS_CHAN_VIDEO, media_bk_net_frame_free);

    ntwk_trans_register_video_recv_cb(media_bk_net_video_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_VIDEO, NULL);

    //configure audio receive callback
    ntwk_trans_register_audio_recv_cb(media_bk_net_audio_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_AUDIO, NULL);

    LOGI("%s end\r\n", __func__);

    return BK_OK;
}

bk_err_t media_network_transfer_stop(void)
{
    LOGI("%s start\r\n", __func__);

    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_VIDEO);
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_AUDIO);

    LOGI("%s end\r\n", __func__);

    return BK_OK;
}

bk_err_t media_bk_network_transfer_init(char *service_name, void *param)
{
    LOGI("%s start\r\n", __func__);

    if (s_media_video_cfg != NULL)
    {
        LOGI("%s: s_media_video_cfg already init\n", __func__);
        return BK_OK;
    }

    s_media_video_cfg = (media_video_cfg_t *)os_malloc(sizeof(media_video_cfg_t));
    if (s_media_video_cfg == NULL)
    {
        LOGE("%s: malloc s_media_video_cfg fail\n", __func__);
        return BK_FAIL;
    }

    s_media_video_cfg->width = (uint16_t)CAST_JPEG_SRC_WIDTH;
    s_media_video_cfg->height = (uint16_t)CAST_JPEG_SRC_HEIGHT;
    s_media_video_cfg->format = IMAGE_MJPEG;

    media_frame_queue_init(s_media_video_cfg->format);

    cast_jpeg_pipeline_set_frame_buffer_release(media_frame_queue_free);
    cast_jpeg_pipeline_set_video_recv_alloc_gate_fn(media_bk_net_cast_video_alloc_gate);
    media_bk_net_cast_video_alloc_gate(0);

    if (strcmp(service_name, "tcp_service") == 0)
    {
        bk_tcp_trans_service_init(service_name);
        media_tcp_network_transfer_start(service_name, param);
    }
    else if (strcmp(service_name, "udp_service") == 0)
    {
        bk_udp_trans_service_init(service_name);
        media_udp_network_transfer_start(service_name, param);
    }
    else
    {
        LOGE("Invalid service name: %s\n", service_name);
        return BK_FAIL;
    }

    LOGI("%s end\r\n", __func__);

    return BK_OK;
}

bk_err_t media_bk_network_transfer_deinit(char *service_name)
{
    LOGI("%s start\r\n", __func__);

    media_network_transfer_stop();

    if (strcmp(service_name, "tcp_service") == 0)
    {
        bk_tcp_trans_service_deinit();
    }
    else if (strcmp(service_name, "udp_service") == 0)
    {
        bk_udp_trans_service_deinit();
    }
    else
    {
        LOGE("Invalid service name: %s\n", service_name);
        return BK_FAIL;
    }

    media_frame_queue_deinit();

    cast_jpeg_pipeline_set_video_recv_alloc_gate_fn(NULL);
    s_cast_video_alloc_allowed = 1U;

    if (s_media_video_cfg != NULL)
    {
        os_free(s_media_video_cfg);
        s_media_video_cfg = NULL;
    }

    LOGI("%s end\r\n", __func__);
    return BK_OK;
}