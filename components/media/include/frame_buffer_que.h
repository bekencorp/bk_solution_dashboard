#ifndef _FRAME_BUFFER_QUE_H_
#define _FRAME_BUFFER_QUE_H_

#include <components/media_types.h>


/**
 * @brief Initialize all frame queue data structures
 * 
 * @return bk_err_t Initialization result
 */
bk_err_t frame_queue_init_all(void);

/**
 * @brief Deinitialize all frame queue data structures
 * 
 * @return bk_err_t Deinitialization result
 */
bk_err_t frame_queue_deinit_all(void);

/**
 * @brief Allocate a frame buffer
 * 
 * @param format Image format
 * @param size Requested buffer size in bytes
 * @return frame_buffer_t* Pointer to allocated frame buffer, NULL on failure
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);

/**
 * @brief Get a frame buffer from the ready queue
 * 
 * @param format Image format
 * @param timeout Timeout value in milliseconds
 * @return frame_buffer_t* Retrieved frame buffer, NULL if timeout or error
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);

/**
 * @brief Return a frame buffer to the ready queue
 * 
 * @param format Image format
 * @param frame Frame buffer to return to queue
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);

/**
 * @brief Free a frame buffer and send message to free queue
 * 
 * @param format Image format
 * @param frame Frame buffer to free
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);

#endif // _FRAME_BUFFER_QUEUE_H_
