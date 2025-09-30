
#pragma once

#include <os/os.h>
#include <components/video_types.h>
#include <components/avdk_utils/avdk_error.h>

#include <trans_list.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_POOL_LEN			(1472 * 35)
#define VIDEO_RXNODE_SIZE		1472

typedef struct
{
    uint32_t wifi_transfer_frame_count;
    uint32_t wifi_transfer_frame_size;
    uint32_t wifi_transfer_rate;
} media_data_process_debug_info_t;

typedef struct {
	/// frame_buffer
	frame_buffer_t *frame;
	/// recoder the buff ptr of every time receive video packte
	uint8_t *buf_ptr;
	/// video buff receive state
	uint8_t start_buf;
	/// the packet count of one frame
	uint32_t frame_pkt_cnt;
} video_buffer_t;

typedef struct {
	struct trans_list_hdr hdr;
	void *buf_start;
	uint32_t buf_len;
} video_elem_t;

typedef struct {
	beken_semaphore_t sem;
	uint8_t *pool;
	video_elem_t elem[VIDEO_POOL_LEN / VIDEO_RXNODE_SIZE];
	struct trans_list free;
	struct trans_list ready;
} video_pool_t;

typedef struct {
	uint16_t width;
	uint16_t height;
	image_format_t fmt;
	video_send_type_t send_type;
} video_param_t;

typedef struct 
{
    frame_buffer_t *(*malloc)(image_format_t fmt, uint32_t size);
    void (*complete)(image_format_t fmt, frame_buffer_t *frame, int result);
} video_frame_callback_t;

void get_last_debug_info(media_data_process_debug_info_t *info);

avdk_err_t video_data_process_open(uint16_t width, uint16_t height, image_format_t format);

avdk_err_t video_data_process_close(void);

uint32_t video_data_receive_complete(uint8_t *data, uint32_t length, video_send_type_t type);

#ifdef __cplusplus
}
#endif