// Copyright 2024-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>

#include "media_frame_psram_alloc.h"
#include "media_data_process_que.h"
#define TAG "data_process_que"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define FB_FRAME_QUEUE_MAX_COUNT  (5)

typedef struct
{
    uint8_t count;
    image_format_t format;
    beken_queue_t free_que;
    beken_queue_t ready_que;
} frame_queue_t;

typedef struct
{
    uint32_t frame;
} frame_msg_t;

frame_queue_t s_frame_queue = {0};

/**
 * @brief 初始化frame_queue数据结构
 *
 * 该函数根据指定的图像格式初始化frame_queue数据结构。
 * 它会根据图像格式设置队列的最大帧数量，并初始化队列的free_que和ready_que。
 * 初始化完成后，会返回初始化结果。
 *
 * @param format 图像格式
 * @return bk_err_t 初始化结果
 */
avdk_err_t media_frame_queue_init(image_format_t format)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_frame_queue.count)
    {
        return ret;
    }

    s_frame_queue.format = format;
    s_frame_queue.count = FB_FRAME_QUEUE_MAX_COUNT;

    // 初始化队列
    // 注意：这里需要根据实际的beken_queue_t定义来实现队列初始化
    // 假设beken_queue_t有相应的初始化函数
    ret = rtos_init_queue(&s_frame_queue.free_que, "free_que", sizeof(frame_msg_t), s_frame_queue.count);
    if (ret != AVDK_ERR_OK)
    {
        LOGD("%s, %d free_que init fail:%d\n", __func__, __LINE__, ret);
        return ret;
    }

    for (int i = 0; i < s_frame_queue.count; i++)
    {
        frame_msg_t msg;
        msg.frame = 0;
        rtos_push_to_queue(&s_frame_queue.free_que, &msg, 0);
    }

    ret = rtos_init_queue(&s_frame_queue.ready_que, "ready_que", sizeof(frame_msg_t), s_frame_queue.count);
    if (ret != AVDK_ERR_OK)
    {
        // 如果ready_que初始化失败，需要释放已经初始化的free_que
        rtos_deinit_queue(&s_frame_queue.free_que);
        os_memset(&s_frame_queue, 0, sizeof(frame_queue_t));
        LOGD("%s, %d ready_que init fail:%d\n", __func__, __LINE__, ret);
    }

    return ret;
}

/**
 * @brief 释放frame_queue数据结构
 *
 * @return avdk_err_t 释放结果
 */
avdk_err_t media_frame_queue_deinit(void)
{
    // 检查是否已经初始化
    if (s_frame_queue.count == 0)
    {
        LOGD("%s, %d already deinit\n", __func__, __LINE__);
        return AVDK_ERR_OK;
    }

    // 释放队列资源
    // 遍历free_que队列，释放其中的消息
    frame_msg_t msg;
    while (rtos_pop_from_queue(&s_frame_queue.free_que, &msg, 0) == AVDK_ERR_OK)
    {
        if (msg.frame)
        {
            frame_buffer_t *frame = (frame_buffer_t *)msg.frame;
            // 根据图像格式选择合适的释放函数
            switch (s_frame_queue.format)
            {
                case IMAGE_MJPEG:
                case IMAGE_H264:
                    frame_buffer_encode_free(frame);
                    break;
                case IMAGE_YUV:
                    frame_buffer_display_free(frame);
                    break;
                default:
                    // 如果格式未知，使用默认释放函数
                    LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, s_frame_queue.format);
                    break;
            }
        }
    }

    // 遍历ready_que队列，释放其中的消息
    while (rtos_pop_from_queue(&s_frame_queue.ready_que, &msg, 0) == AVDK_ERR_OK)
    {
        if (msg.frame)
        {
            frame_buffer_t *frame = (frame_buffer_t *)msg.frame;
            // 根据图像格式选择合适的释放函数
            switch (s_frame_queue.format)
            {
                case IMAGE_MJPEG:
                case IMAGE_H264:
                    frame_buffer_encode_free(frame);
                    break;
                case IMAGE_YUV:
                    frame_buffer_display_free(frame);
                    break;
                default:
                    // 如果格式未知，使用默认释放函数
                    LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, s_frame_queue.format);
                    break;
            }
        }
    }

    rtos_deinit_queue(&s_frame_queue.free_que);
    rtos_deinit_queue(&s_frame_queue.ready_que);

    // 重置frame_queue结构
    s_frame_queue.count = 0;
    s_frame_queue.format = IMAGE_UNKNOW; // 假设存在IMAGE_NONE枚举值

    return AVDK_ERR_OK;
}

/**
 * @brief 从frame_queue中申请一个frame_buffer
 *
 * @param size 申请的frame大小
 * @return frame_buffer_t* 申请到的frame_buffer，如果申请失败则返回NULL
 */
frame_buffer_t *media_frame_queue_malloc(uint32_t size)
{
    frame_msg_t msg;
    frame_buffer_t *frame = NULL;

    // 检查是否已经初始化
    if (s_frame_queue.count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, s_frame_queue.format);
        return NULL;
    }

    // 从free_que中获取消息
    if (rtos_pop_from_queue(&s_frame_queue.free_que, &msg, 0) == AVDK_ERR_OK)
    {
        // 根据图像格式选择合适的申请函数
        switch (s_frame_queue.format)
        {
            case IMAGE_MJPEG:
            case IMAGE_H264:
                frame = frame_buffer_encode_malloc(size);
                break;
            case IMAGE_YUV:
                frame = frame_buffer_display_malloc(size);
                break;
            default:
                // 如果格式未知，返回NULL
                LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, s_frame_queue.format);
                // 将消息放回队列
                rtos_push_to_queue(&s_frame_queue.free_que, &msg, 0);
                return NULL;
        }

        // 如果申请frame失败，将消息放回队列
        if (frame == NULL)
        {
            LOGW("%s, %d malloc frame fail\n", __func__, __LINE__);
            // 将消息放回队列
            rtos_push_to_queue(&s_frame_queue.free_que, &msg, 0);
        }
    }

    // 如果是从JPEG或者YUV格式，可以从ready_que中取一个
    if (frame == NULL && (s_frame_queue.format == IMAGE_MJPEG || s_frame_queue.format == IMAGE_YUV))
    {
        if (rtos_pop_from_queue(&s_frame_queue.ready_que, &msg, 0) == AVDK_ERR_OK)
        {
            frame = (frame_buffer_t *)msg.frame;
        }
    }

    if (frame == NULL)
    {
        return NULL;
    }

    frame->fmt = 0;
    frame->timestamp = 0;
    frame->width = 0;
    frame->height = 0;
    frame->length = 0;
    frame->sequence = 0;
    frame->h264_type = 0;

    return frame;
}

/**
 * @brief 从frame_queue的ready队列中获取一个frame_buffer
 *
 * @param timeout 超时时间（毫秒）
 * @return frame_buffer_t* 获取到的frame_buffer，如果获取失败则返回NULL
 */
frame_buffer_t *media_frame_queue_get_frame(uint32_t timeout)
{
    frame_msg_t msg;
    frame_buffer_t *frame = NULL;

    // 检查是否已经初始化
    if (s_frame_queue.count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, s_frame_queue.format);
        return frame;
    }

    // 从ready_que中获取消息
    if (rtos_pop_from_queue(&s_frame_queue.ready_que, &msg, timeout) == AVDK_ERR_OK)
    {
        frame = (frame_buffer_t *)msg.frame;
    }

    return frame;
}

/**
 * @brief 将frame_buffer放回ready_que队列
 *
 * @param frame 要放回队列的frame_buffer
 * @return bk_err_t 操作结果
 */
bk_err_t media_frame_queue_complete(frame_buffer_t *frame)
{
    frame_msg_t msg;

    // 检查是否已经初始化
    if (s_frame_queue.count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, s_frame_queue.format);
        return BK_FAIL;
    }

    // 构造消息
    msg.frame = (uint32_t)frame;

    // 将消息放入ready_que队列
    if (rtos_push_to_queue(&s_frame_queue.ready_que, &msg, 0) != AVDK_ERR_OK)
    {
        LOGE("%s, %d rtos_push_to_queue fail\n", __func__, __LINE__);
        // 根据图像格式选择合适的释放函数
        media_frame_queue_free(frame);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief 根据图像格式释放frame_buffer，并将消息发送到free_que
 *
 * @param frame 要释放的frame_buffer
 * @return void
 */
void media_frame_queue_free(frame_buffer_t *frame)
{
    frame_msg_t msg;

    // 检查是否已经初始化
    if (s_frame_queue.count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, s_frame_queue.format);
        return;
    }

    // 根据图像格式选择合适的释放函数
    switch (s_frame_queue.format)
    {
        case IMAGE_MJPEG:
        case IMAGE_H264:
            frame_buffer_encode_free(frame);
            break;
        case IMAGE_YUV:
            frame_buffer_display_free(frame);
            break;
        default:
            // 如果格式未知，使用默认释放函数
            LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, s_frame_queue.format);
            return;
    }

    // 构造消息
    msg.frame = 0; // 释放frame后，消息中的frame指针应为NULL

    // 将消息放入free_que队列
    if (rtos_push_to_queue(&s_frame_queue.free_que, &msg, 0) != AVDK_ERR_OK)
    {
        LOGE("%s, %d rtos_push_to_queue fail\n", __func__, __LINE__);
    }
}
