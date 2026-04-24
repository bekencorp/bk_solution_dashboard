#ifndef _MEDIA_DATA_PROCESS_QUEUE_H_
#define _MEDIA_DATA_PROCESS_QUEUE_H_

#include <common/bk_err.h>
#include <components/media_types.h>
#include <components/avdk_utils/avdk_error.h>

/**
 * @brief Initialize all media frame queue data structures
 * 
 * @param format Image format
 * @return avdk_err_t Initialization result
 */
avdk_err_t media_frame_queue_init(image_format_t format);

/**
 * @brief Deinitialize all media frame queue data structures
 * 
 * @return avdk_err_t Deinitialization result
 */
avdk_err_t media_frame_queue_deinit(void);

/**
 * @brief Allocate a frame buffer
 * 
 * @param size Requested buffer size in bytes
 * @return frame_buffer_t* Pointer to allocated frame buffer, NULL on failure
 */
frame_buffer_t *media_frame_queue_malloc(uint32_t size);

/**
 * @brief Get a frame buffer from the ready queue
 * 
 * @param timeout Timeout value in milliseconds
 * @return frame_buffer_t* Retrieved frame buffer, NULL if timeout or error
 */
frame_buffer_t *media_frame_queue_get_frame(uint32_t timeout);

/**
 * @brief Return a frame buffer to the ready queue
 * 
 * @param frame Frame buffer to return to queue
 * @return avdk_err_t Operation result
 */
bk_err_t media_frame_queue_complete(frame_buffer_t *frame);

/**
 * @brief Free a frame buffer and send message to free queue
 * 
 * @param frame Frame buffer to free
 */
void media_frame_queue_free(frame_buffer_t *frame);

#endif // _MEDIA_DATA_PROCESS_QUEUE_H_
