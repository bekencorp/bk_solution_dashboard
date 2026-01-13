#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include "frame_buffer_que.h"
#include "frame_buffer.h"
#include <components/bk_video_pipeline/bk_video_pipeline.h>

#define TAG "h264e_cli"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

bk_video_pipeline_handle_t s_h264e_handle = NULL;

static frame_buffer_t *h264e_frame_malloc(uint32_t size)
{
    return frame_queue_malloc(IMAGE_H264, size);
}

static void h264e_frame_complete(frame_buffer_t *frame, bk_err_t result)
{
    if (frame->sequence % 30 == 0)
    {
        LOGD("%s: seq:%d, length:%d, format:H264, h264_type:%x\n", __func__, frame->sequence,
            frame->length, frame->h264_type);
    }

    if (result != BK_OK)
    {
        frame_queue_free(IMAGE_H264, frame);
    }
    else
    {
        frame_queue_complete(IMAGE_H264, frame);
    }
}

static const bk_h264e_callback_t h264e_cbs = {
    .malloc = h264e_frame_malloc,
    .complete = h264e_frame_complete,
};

static bk_err_t jpeg_complete(bk_err_t result, frame_buffer_t *out_frame)
{
    bk_err_t ret = BK_OK;
    frame_queue_free(IMAGE_MJPEG, out_frame);
    return ret;
}

static frame_buffer_t *jpeg_read(uint32_t timeout_ms)
{
    return frame_queue_get_frame(IMAGE_MJPEG, timeout_ms);
}

static const jpeg_callback_t jpeg_cbs = {
    .read = jpeg_read,
    .complete = jpeg_complete,
};

static bk_err_t display_frame_free_cb(void *frame)
{
    frame_buffer_display_free((frame_buffer_t *)frame);
    return BK_OK;
}

static bk_err_t decode_complete(dec_end_type_t format_type, bk_err_t result, frame_buffer_t *out_frame)
{
    bk_err_t ret = BK_FAIL;
    if (format_type == HW_DEC_END)
    {
        display_frame_free_cb(out_frame);
        return ret;
    }
    else if (format_type == SW_DEC_END)
    {
        if (result != BK_OK)
        {
            display_frame_free_cb(out_frame);
            return BK_OK;
        }
        else
        {
            display_frame_free_cb(out_frame);
        }
        return ret;
    }
    return ret;
}

static frame_buffer_t *decode_malloc(uint32_t size)
{
    return frame_buffer_display_malloc(size);
}

static bk_err_t decode_free(frame_buffer_t *frame)
{
    frame_buffer_display_free(frame);
    return BK_OK;
}

static const decode_callback_t decode_cbs = {
    .malloc = decode_malloc,
    .free = decode_free,
    .complete = decode_complete,
};

avdk_err_t h264_encode_open(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    bk_video_pipeline_config_t video_pipeline_config = {0};
    bk_video_pipeline_h264e_config_t h264e_config = BK_H264_ENCODE_864X480_30FPS_CONFIG();

    if (argc >= 5)
    {
        h264e_config.width = os_strtoul(argv[3], NULL, 10);
        h264e_config.height = os_strtoul(argv[4], NULL, 10);
    }

    if (h264e_config.width == 1280)
    {
        h264e_config.fps = FPS15;
    }

    // init frame queue
    frame_queue_init_all();

    video_pipeline_config.jpeg_cbs = &jpeg_cbs;
    video_pipeline_config.decode_cbs = &decode_cbs;

    avdk_err_t ret = bk_video_pipeline_new(&s_h264e_handle, &video_pipeline_config);
    if (ret != BK_OK)
    {
        LOGE("%s bk_video_pipeline_new failed\n", __func__);
        return ret;
    }

    h264e_config.h264e_cb = &h264e_cbs;
    ret = bk_video_pipeline_open_h264e(s_h264e_handle, &h264e_config);
    if (ret != BK_OK)
    {
        LOGE("%s bk_video_pipeline_open_h264e failed\n", __func__);
        bk_video_pipeline_delete(s_h264e_handle);
        s_h264e_handle = NULL;
    }

    return ret;
}

avdk_err_t h264_encode_close(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    avdk_err_t ret = AVDK_ERR_OK;
    if (s_h264e_handle)
    {
        ret = bk_video_pipeline_close_h264e(s_h264e_handle);
        if (ret != BK_OK)
        {
            LOGE("%s failed\n", __func__);
        }
        else
        {
            bk_video_pipeline_delete(s_h264e_handle);
            s_h264e_handle = NULL;
            LOGD("%s success\n", __func__);
        }
    }
    else
    {
        LOGD("%s h264e_handle is NULL, already closed\n", __func__);
    }

    return ret;
}