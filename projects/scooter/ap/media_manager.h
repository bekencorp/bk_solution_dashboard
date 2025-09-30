#ifndef __CAMERA_MANAGER_H__
#define __CAMERA_MANAGER_H__

#include <components/avdk_utils/avdk_error.h>

/**
 * @brief Open UVC camera device
 * @details Initialize UVC camera hardware, configure video stream parameters, prepare for video data capture
 * @param pcWriteBuffer Output buffer pointer for returning operation result information
 * @param xWriteBufferLen Output buffer length to ensure no buffer overflow
 * @param argc Command line argument count for parsing configuration options
 * @param argv Command line argument array containing device configuration parameters
 * @return Returns AVDK_ERR_OK on success, specific error code on failure
 */
avdk_err_t uvc_camera_open(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

/**
 * @brief Close UVC camera device
 * @details Release camera resources, stop video stream capture, clean up related memory
 * @param pcWriteBuffer Output buffer pointer for returning operation result information
 * @param xWriteBufferLen Output buffer length to ensure no buffer overflow
 * @param argc Command line argument count
 * @param argv Command line argument array
 * @return Returns AVDK_ERR_OK on success, specific error code on failure
 */
avdk_err_t uvc_camera_close(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

/**
 * @brief Open H.264 encoder
 * @details Initialize H.264 encoder, configure encoding parameters (resolution, frame rate, bitrate, etc.)
 * @param pcWriteBuffer Output buffer pointer for returning operation result information
 * @param xWriteBufferLen Output buffer length to ensure no buffer overflow
 * @param argc Command line argument count for parsing encoder configuration options
 * @param argv Command line argument array containing encoder configuration parameters
 * @return Returns AVDK_ERR_OK on success, specific error code on failure
 */
avdk_err_t h264_encode_open(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

/**
 * @brief Close H.264 encoder
 * @details Release encoder resources, stop encoding process, clean up related memory
 * @param pcWriteBuffer Output buffer pointer for returning operation result information
 * @param xWriteBufferLen Output buffer length to ensure no buffer overflow
 * @param argc Command line argument count
 * @param argv Command line argument array
 * @return Returns AVDK_ERR_OK on success, specific error code on failure
 */
avdk_err_t h264_encode_close(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

/**
 * @brief Open H.264 stream storage function
 * @details Initialize storage system, create file handle, prepare to receive H.264 stream data and write to storage medium
 * @param name Storage file name, files will be renamed and stored in 0-0xFFFF_name format by default
 * @param timer_interval Timer interval (milliseconds), controls how often to create a new file for storage, values less than 1min are set to 1min
 * @return Returns AVDK_ERR_OK on success, specific error code on failure
 */
avdk_err_t h264e_storage_open(const char *name, uint32_t timer_interval);

/**
 * @brief Close H.264 stream storage function
 * @details Close storage file, release storage resources, ensure stream data is completely written
 * @return Returns AVDK_ERR_OK on success, specific error code on failure
 */
avdk_err_t h264e_storage_close(void);

#endif