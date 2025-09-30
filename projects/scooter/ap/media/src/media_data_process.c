// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <os/mem.h>
#include <components/log.h>
#include <components/video_types.h>
#include "media_data_process.h"
#include "media_data_process_que.h"
#define TAG "wifi_trs"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define VIDEO_FRAME_ERROR (-1)
#define VIDEO_FRAME_OK    (0)

typedef struct
{
    uint8_t task_running;
    beken_thread_t thread;
    beken_semaphore_t sem;
    video_pool_t pool;
    video_param_t param;
    video_buffer_t video_buf;
    const video_frame_callback_t *callback;
    uint32_t frame_cnt;
    media_data_process_debug_info_t debug_info;
} video_data_config_t;

video_data_config_t *s_video_data_config = NULL;

static avdk_err_t video_data_config_mem_free(video_data_config_t *config)
{
    if (config)
    {
        if (config->pool.pool)
        {
            os_free(config->pool.pool);
            config->pool.pool = NULL;
        }

        //释放信号量
        if (config->pool.sem)
        {
            rtos_deinit_semaphore(&config->pool.sem);
        }

        if (config->video_buf.frame)
        {
            config->callback->complete(config->param.fmt, config->video_buf.frame, VIDEO_FRAME_ERROR);
            config->video_buf.frame = NULL;
        }

        //释放信号量
        if (config->sem)
        {
            rtos_deinit_semaphore(&config->sem);
        }

        os_free(config);
    }

    return AVDK_ERR_OK;
}

static avdk_err_t video_pool_init(video_pool_t *pool)
{
    //初始化信号量
    avdk_err_t ret = rtos_init_semaphore(&pool->sem, 1);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, init pool->sem fail, ret = %d\n", __func__, ret);
        return ret;
    }

    if (pool->pool == NULL)
    {
#ifdef CONFIG_PSRAM_AS_SYS_MEMORY
        pool->pool = (uint8_t *)psram_malloc(VIDEO_POOL_LEN);
#else
        pool->pool = (uint8_t *)os_malloc(VIDEO_POOL_LEN);
#endif
        if (pool->pool == NULL)
        {
            LOGE("tvideo_pool alloc failed\r\n");
            rtos_deinit_semaphore(&pool->sem);
            return AVDK_ERR_NOMEM;
        }

        //os_memset(pool->pool, 0, VIDEO_POOL_LEN);
    }

    trans_list_init(&pool->free);
    trans_list_init(&pool->ready);

    for (uint8_t i = 0; i < (VIDEO_POOL_LEN / VIDEO_RXNODE_SIZE); i++) {
        pool->elem[i].buf_start =
            (void *)&pool->pool[i * VIDEO_RXNODE_SIZE];
        pool->elem[i].buf_len = 0;

        trans_list_push_back(&pool->free,
            (struct trans_list_hdr *)&pool->elem[i].hdr);
    }

    return ret;
}

static void video_data_process_packet(video_data_config_t *config, uint8_t *data, uint32_t length)
{
    if (config->task_running == 0)
    {
        return;
    }

    video_buffer_t *video_buffer = &config->video_buf;
    video_param_t *param = &config->param;

    if ((video_buffer->start_buf == BUF_STA_INIT || video_buffer->start_buf == BUF_STA_COPY) && video_buffer->frame)
    {
        video_header_t *hdr = (video_header_t *)data;
        uint32_t org_len;
        GLOBAL_INT_DECLARATION();

        org_len = length - sizeof(video_header_t);
        data = data + sizeof(video_header_t);

        LOGV("id:%d eof %d pkt cnt %d pkt seq %d len %d org_len %d\r\n", hdr->id,hdr->is_eof,hdr->pkt_cnt,hdr->pkt_seq,length,org_len);

        //wifi_transfer_data_check(data,size);

        if (hdr->pkt_cnt == 1) {
            // start of frame;
            video_buffer->frame->length = 0;
            video_buffer->frame_pkt_cnt = 0;
            video_buffer->buf_ptr = video_buffer->frame->frame;
            video_buffer->start_buf = BUF_STA_COPY;
            LOGV("sof:%d\r\n", video_buffer->frame->sequence);
        }
        else
        {
            if (video_buffer->start_buf == BUF_STA_INIT)
                video_buffer->start_buf = BUF_STA_COPY;
        }

       /* LOGD("hdr-id:%d-%d, frame_packet_cnt:%d-%d, state:%d\r\n", hdr->id, wifi_recv_camera_buf->frame->sequence,
            (wifi_recv_camera_buf->frame_pkt_cnt + 1), hdr->pkt_cnt, wifi_recv_camera_buf->start_buf); */

        if (((video_buffer->frame_pkt_cnt + 1) == hdr->pkt_cnt)
            && (video_buffer->start_buf == BUF_STA_COPY))
        {
            if (video_buffer->frame->length + org_len > video_buffer->frame->size)
            {
                video_buffer->frame->length += org_len;
                video_buffer->frame_pkt_cnt += 1;
                if (hdr->is_eof == 1)
                {
                    LOGE("%s %d transfer_length %d is over frame_buffer size %d \r\n", __func__, __LINE__, video_buffer->frame->length, video_buffer->frame->size);
                    video_buffer->frame->width = param->width;
                    video_buffer->frame->height = param->height;
                    video_buffer->frame->fmt = param->fmt;
                    video_buffer->buf_ptr = video_buffer->frame->frame;
                    video_buffer->frame->length = 0;
                    video_buffer->frame->sequence = config->frame_cnt++;
                }
                return;
            }

            os_memcpy(video_buffer->buf_ptr, data, org_len);

            GLOBAL_INT_DISABLE();
            video_buffer->frame->length += org_len;
            video_buffer->buf_ptr += org_len;
            video_buffer->frame_pkt_cnt += 1;
            GLOBAL_INT_RESTORE();

            if (hdr->is_eof == 1)
            {
                frame_buffer_t *new_frame = config->callback->malloc(param->fmt, CONFIG_JPEG_FRAME_SIZE);
                if (new_frame)
                {
                    config->callback->complete(param->fmt, video_buffer->frame, VIDEO_FRAME_OK);
                    video_buffer->frame = new_frame;
                }
                else
                {
                    LOGE("frame buffer malloc failed\r\n");
                }

                video_buffer->frame->width = param->width;
                video_buffer->frame->height = param->height;
                video_buffer->frame->fmt = param->fmt;
                video_buffer->buf_ptr = video_buffer->frame->frame;
                video_buffer->frame->length = 0;
                video_buffer->frame->sequence = config->frame_cnt++;
            }
        }
    }
}

static void video_data_process_task_entry(beken_thread_arg_t data)
{
    video_data_config_t *video_config = (video_data_config_t *)data;
    video_elem_t *elem = NULL;
    bk_err_t err = 0;

    video_pool_t *pool = &video_config->pool;
    video_config->video_buf.start_buf = BUF_STA_INIT;
    video_config->task_running = true;
    rtos_set_semaphore(&video_config->sem);

    while (video_config->task_running)
    {
        err = rtos_get_semaphore(&pool->sem, 1000);

        if(!video_config->task_running)
        {
            break;
        }

        if(err != 0)
        {
            LOGV("%s get sem timeout\n", __func__);
            continue;
        }

        while((elem = (video_elem_t *)trans_list_pick(&pool->ready)) != NULL)
        {
            video_data_process_packet(video_config, elem->buf_start, elem->buf_len);

            trans_list_pop_front(&pool->ready);
            trans_list_push_back(&pool->free, (struct trans_list_hdr *)&elem->hdr);
        }
    };

    video_config->thread = NULL;
    rtos_set_semaphore(&video_config->sem);
    rtos_delete_thread(NULL);
}

static frame_buffer_t *receive_frame_malloc(image_format_t format, uint32_t size)
{
    return media_frame_queue_malloc(size);
}

static void receive_frame_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    LOGV("%s, seq:%d, length:%d, format:%x, result:%d\n", __func__, frame->sequence, frame->length, frame->fmt, result);
    if (result != BK_OK)
    {
        media_frame_queue_free(frame);
    }
    else
    {
        s_video_data_config->debug_info.wifi_transfer_frame_count++;
        s_video_data_config->debug_info.wifi_transfer_frame_size += frame->length;
        media_frame_queue_complete(frame);
    }
}

static const video_frame_callback_t video_cbs = {
    .malloc = receive_frame_malloc,
    .complete = receive_frame_complete,
};

void get_last_debug_info(media_data_process_debug_info_t *info)
{
    *info = s_video_data_config->debug_info;
}

avdk_err_t video_data_process_open(uint16_t width, uint16_t height, image_format_t format)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_video_data_config != NULL)
    {
        LOGW("%s, video data process already open\n", __func__);
        return ret;
    }

    s_video_data_config = (video_data_config_t *)os_malloc(sizeof(video_data_config_t));
    if (s_video_data_config == NULL)
    {
        ret = AVDK_ERR_NOMEM;
        LOGE("%s, malloc s_video_data_config fail\n", __func__);
        return ret;
    }

    // 初始化frame_queue
    media_frame_queue_init(format);

    os_memset(s_video_data_config, 0, sizeof(video_data_config_t));
    s_video_data_config->callback = &video_cbs;
    s_video_data_config->param.width = width;
    s_video_data_config->param.height = height;
    s_video_data_config->param.fmt = format;
    s_video_data_config->video_buf.frame = NULL;

    ret = video_pool_init(&s_video_data_config->pool);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, video_pool_init fail, ret = %d\n", __func__, ret);
        goto error;
    }

    //创建信号量
    ret = rtos_init_semaphore(&s_video_data_config->sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s, init s_video_data_config->sem fail, ret = %d\n", __func__, ret);
        goto error;
    }

    //初始化video_buffer->frame，此处只考虑IMAGE_MJPEG格式
    s_video_data_config->video_buf.frame = s_video_data_config->callback->malloc(s_video_data_config->param.fmt, CONFIG_JPEG_FRAME_SIZE);
    if (s_video_data_config->video_buf.frame == NULL)
    {
        ret = AVDK_ERR_NOMEM;
        LOGE("%s, malloc s_video_data_config->video_buffer->frame fail\n", __func__);
        goto error;
    }

    s_video_data_config->video_buf.buf_ptr = s_video_data_config->video_buf.frame->frame;
    s_video_data_config->video_buf.frame->width = width;
    s_video_data_config->video_buf.frame->height = height;
    s_video_data_config->video_buf.frame->fmt = format;
    s_video_data_config->video_buf.frame->length = 0;
    s_video_data_config->video_buf.frame->sequence = s_video_data_config->frame_cnt++;
    s_video_data_config->video_buf.start_buf = BUF_STA_INIT;

    //创建接收线程
    ret = rtos_create_thread(&s_video_data_config->thread,
                                6,
                                "video_task",
                                (beken_thread_function_t)video_data_process_task_entry,
                                4 * 1024,
                                s_video_data_config);
    if (ret != BK_OK)
    {
        LOGE("%s, create video_data_process_task fail, ret = %d\n", __func__, ret);
        goto error;
    }

    rtos_get_semaphore(&s_video_data_config->sem, BEKEN_WAIT_FOREVER);

    return ret;

error:

    LOGE("%s, failed!, ret = %d\n", __func__, ret);
    video_data_config_mem_free(s_video_data_config);
    s_video_data_config = NULL;
    return ret;
}

avdk_err_t video_data_process_close(void)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_video_data_config == NULL || s_video_data_config->task_running == false)
    {
        LOGE("%s, video data process not open\n", __func__);
        return ret;
    }

    if (s_video_data_config->video_buf.start_buf == BUF_STA_COPY)
    {
        s_video_data_config->video_buf.start_buf = BUF_STA_DEINIT;
    }

    s_video_data_config->task_running = false;
    rtos_get_semaphore(&s_video_data_config->sem, BEKEN_WAIT_FOREVER);
    video_data_config_mem_free(s_video_data_config);
    s_video_data_config = NULL;
    return ret;
}

uint32_t video_data_receive_complete(uint8_t *data, uint32_t length, video_send_type_t type)
{
    video_elem_t *elem = NULL;

    video_data_config_t *video_config = s_video_data_config;

    if (video_config == NULL || video_config->task_running == false)
    {
        return length;
    }

    video_config->debug_info.wifi_transfer_rate += length;
    video_param_t *param = &video_config->param;
    video_pool_t *pool = &video_config->pool;

    if (param->send_type != type) {
        param->send_type = type;
    }

    if (length <= 4)
    {
        return length;
    }

    elem = (video_elem_t *)trans_list_pick(&pool->free);
    if (elem)
    {
        os_memcpy(elem->buf_start, data, length);
        elem->buf_len = length;
        trans_list_pop_front(&pool->free);
        trans_list_push_back(&pool->ready, (struct trans_list_hdr *)&elem->hdr);
        rtos_set_semaphore(&pool->sem);
    }
    else
    {
        LOGD("list all busy\r\n");
    }

    return length;
}