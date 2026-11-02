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

/* Per-frame trace: compiled OUT by default (avoid log flood regardless of
 * runtime log level). Set CAST_TRACE_PER_FRAME=1 to re-enable for debugging. */
#ifndef CAST_TRACE_PER_FRAME
#define CAST_TRACE_PER_FRAME 0
#endif
#if CAST_TRACE_PER_FRAME
#define LOGPF(...) BK_LOGI(TAG, ##__VA_ARGS__)
#else
#define LOGPF(...) do {} while (0)
#endif

static media_network_msg_cb_t s_app_msg_cb = NULL;


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

/*
 * Queried by the unfragment layer for every incoming video fragment. While the
 * cast sink is closed no frame buffer can be handed out, so dropping here keeps
 * the packets out of the reassembly pool entirely.
 */
static bool media_bk_net_video_rx_gate(chan_type_t chan)
{
	(void)chan;
	return s_cast_video_alloc_allowed ? true : false;
}

/*
 * Cast session channel vote.
 *
 * A cast session spans the CTRL and VIDEO sockets. Tearing it down as soon as
 * either socket drops kills a session the peer is still using, and a single
 * socket hiccup is enough to trigger it. Track which channels are up and only
 * tear down once the last one is gone.
 */
#define CAST_CHAN_BIT_CTRL   (1U << 0)
#define CAST_CHAN_BIT_VIDEO  (1U << 1)

static volatile uint32_t s_cast_chan_up_mask;

static uint32_t cast_chan_vote_bit(uint8_t chan_type)
{
    if (chan_type == NTWK_TRANS_CHAN_CTRL)
    {
        return CAST_CHAN_BIT_CTRL;
    }
    if (chan_type == NTWK_TRANS_CHAN_VIDEO)
    {
        return CAST_CHAN_BIT_VIDEO;
    }
    return 0U;
}

static void cast_chan_vote_connected(uint8_t chan_type)
{
    uint32_t bit = cast_chan_vote_bit(chan_type);
    GLOBAL_INT_DECLARATION();

    if (bit == 0U)
    {
        return;
    }

    GLOBAL_INT_DISABLE();
    s_cast_chan_up_mask |= bit;
    GLOBAL_INT_RESTORE();
}

/* Returns true when this disconnect brought the last live channel down. */
static bool cast_chan_vote_disconnected(uint8_t chan_type)
{
    uint32_t bit = cast_chan_vote_bit(chan_type);
    uint32_t before;
    uint32_t after;
    GLOBAL_INT_DECLARATION();

    if (bit == 0U)
    {
        return false;
    }

    GLOBAL_INT_DISABLE();
    before = s_cast_chan_up_mask;
    s_cast_chan_up_mask &= ~bit;
    after = s_cast_chan_up_mask;
    GLOBAL_INT_RESTORE();

    return (before != 0U) && (after == 0U);
}

static void cast_chan_vote_reset(void)
{
    GLOBAL_INT_DECLARATION();

    GLOBAL_INT_DISABLE();
    s_cast_chan_up_mask = 0U;
    GLOBAL_INT_RESTORE();
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
    if ((length == 5U) && (s_media_video_cfg != NULL))
    {
        uint8_t codec_type = data[0];
        uint16_t width = data[1] | (data[2]<<8);
        uint16_t height = data[3] | (data[4]<<8);
        LOGI("codec type:%d width:%d height:%d\r\n", codec_type, width, height);
        if ((((uint32_t)width * height) !=
             ((uint32_t)s_media_video_cfg->width * s_media_video_cfg->height)) &&
            (s_app_msg_cb != NULL))
        {
            s_app_msg_cb("navi width height ERROR!!!");
        }
    }
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
        /* The rx gate normally drops these packets earlier; anything reaching
         * here is in-flight, so keep the log rate-limited. */
        static uint32_t s_denied_count = 0;
        if ((s_denied_count++ % 512U) == 0U) {
            LOGW("frame_malloc: denied (cast alloc gate), size=%u total=%u\n",
                 (unsigned)size, (unsigned)s_denied_count);
        }
        return NULL;
    }
    s_malloc_count++;
    frame_buffer_t *fb = media_frame_queue_malloc(size);
    LOGPF("frame_malloc #%u: size=%u, result=%s\n",
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
    LOGPF("frame_send #%u: len=%u\n", (unsigned)s_send_count, (unsigned)data->length);

    data->width = s_media_video_cfg->width;
    data->height = s_media_video_cfg->height;
    data->fmt = s_media_video_cfg->format;

    s_media_video_cfg->debug_info.wifi_transfer_frame_count++;
    s_media_video_cfg->debug_info.wifi_transfer_frame_size += data->length;

    bk_err_t ret = cast_jpeg_pipeline_push_frame_buffer(data);
    if (ret != BK_OK) {
        /* Backpressure and H264 GOP drops both land here and can persist for a
         * whole GOP, so keep the log rate-limited. */
        static uint32_t s_push_fail_count = 0;
        if ((s_push_fail_count++ % 128U) == 0U) {
            LOGW("frame_send: cast push rejected, len=%u ret=%d total=%u\n",
                (unsigned)data->length, (int)ret, (unsigned)s_push_fail_count);
        }
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
                cast_chan_vote_connected(event->chan_type);

                /*
                 * 原仅 CTRL 连上才开 JPEG 解码；App 若先连视频口、不配网/未连控制口，
                 * 解码器永不启动，投屏无画面、固定 JPEG 红块 demo 也不会跑。 ||
                    event->chan_type == NTWK_TRANS_CHAN_VIDEO
                 */
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    LOGI("%s NETWORK CTRL channle connected \r\n", __func__);
                    msg.event = MEDIA_EVT_REMOTE_DEVICE_CONNECTED;
                }
                else if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    LOGI("%s NETWORK VIDEO channle connected \r\n", __func__);
                    /*
                     * The video socket can drop and reconnect while the session
                     * as a whole survives the vote. Its disconnect closed the
                     * alloc gate, so reopen it here; otherwise the unfragment
                     * layer never gets a frame buffer again and the video stays
                     * black until the whole session is rebuilt.
                     */
                    if (cast_jpeg_pipeline_is_running())
                    {
                        media_bk_net_cast_video_alloc_gate(1);
                    }
                }
            }
        } break;
        case NTWK_TRANS_EVT_DISCONNECTED:
        {
            if(strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                bool     session_down = cast_chan_vote_disconnected(event->chan_type);
                uint32_t reason       = 0U;

                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    LOGI("%s NETWORK CTRL channle disconnected \r\n", __func__);
                    reason = MEDIA_CAST_TEARDOWN_REASON_CTRL_TCP;
                }
                else if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    /*
                     * Block unfragment malloc before the disconnect message is handled;
                     * avoids overlap with cast_jpeg_pipeline_turn_off (malloc+send
                     * same tick as teardown → heap assert in decoder deinit path).
                     */
                    LOGI("%s NETWORK VIDEO channle disconnected \r\n", __func__);
                    media_bk_net_cast_video_alloc_gate(0);
                    reason = MEDIA_CAST_TEARDOWN_REASON_VIDEO_TCP;
                }

                if (reason != 0U)
                {
                    if (session_down)
                    {
                        msg.event = MEDIA_EVT_CAST_SESSION_TEARDOWN;
                        msg.param = reason;
                    }
                    else
                    {
                        LOGI("%s: chan %u down, session kept alive (up_mask=0x%x)\n",
                             __func__, (unsigned)event->chan_type,
                             (unsigned)s_cast_chan_up_mask);
                    }
                }
            }
        } break;
        case NTWK_TRANS_EVT_STOP:
        {
            cast_chan_vote_reset();
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

static bk_err_t media_tcp_network_transfer_start(void)
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


static bk_err_t media_udp_network_transfer_start(void)
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

bk_err_t media_bk_network_transfer_init(char *service_name, media_network_msg_cb_t msg_cb)
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
    cast_chan_vote_reset();
    s_app_msg_cb = msg_cb;
    if (strcmp(service_name, "tcp_service") == 0)
    {
        bk_tcp_trans_service_init(service_name);
        media_tcp_network_transfer_start();
    }
    else if (strcmp(service_name, "udp_service") == 0)
    {
        bk_udp_trans_service_init(service_name);
        media_udp_network_transfer_start();
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
    cast_chan_vote_reset();

    if (s_media_video_cfg != NULL)
    {
        os_free(s_media_video_cfg);
        s_media_video_cfg = NULL;
    }
    s_app_msg_cb = NULL;

    LOGI("%s end\r\n", __func__);
    return BK_OK;
}