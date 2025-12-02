/*
 * Copyright (c) 2023 - present, armino(Shanghai) Co., Ltd.
 * All rights reserved.
 *
 * Description: JPEG Decoder Manager Header File
 */

#pragma once

#include <stdint.h>
#include <components/media_types.h>
#include <components/bk_jpeg_decode/bk_jpeg_decode_sw.h>
#include <components/bk_jpeg_decode/bk_jpeg_decode_hw.h>
#include <components/bk_jpeg_decode/bk_jpeg_decode_types_hw.h>
#include <os/os.h>

// Ensure BK_ERR_INVAL is properly defined
#ifndef BK_ERR_INVAL
#define BK_ERR_INVAL -1
#endif

// Include bk_err.h to ensure all error code definitions are available
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode task status enumeration
 */
typedef enum {
    DECODE_STATUS_IDLE = 0,      /*!< Idle status */
    DECODE_STATUS_BUSY,          /*!< Busy status */
} decode_status_t;

/**
 * @brief Decoded image node structure definition
 * Used to store decoded images in a linked list
 */
typedef struct decoded_image_node {
    frame_buffer_t *out_frame;   /*!< Decoded output frame */
    uint32_t sequence;           /*!< Image sequence number */
    struct decoded_image_node *next; /*!< Pointer to next node */
} decoded_image_node_t;

typedef struct decode_task_s decode_task_s;
typedef struct jpeg_decode_manager_t jpeg_decode_manager_t;

/**
 * @brief Decode task structure
 * Used to store decode task information in the decode task list
 */
typedef struct decode_task_s {
    frame_buffer_t *in_frame;    /*!< Input frame (JPEG data) */
    frame_buffer_t *out_frame;   /*!< Output frame (decoded data) */
    uint32_t sequence;           /*!< Sequence number */
    uint32_t core_id;            /*!< Core ID used (1 or 2) */
    jpeg_decode_manager_t *manager; /*!< Pointer to decode manager */
    decode_task_s *next;  /*!< Pointer to next task */
} decode_task_t;

/**
 * @brief Decode input queue message structure
 */
typedef struct {
    frame_buffer_t *frame;       /*!< Input JPEG image frame */
} decode_input_msg_t;

/**
 * @brief JPEG decode manager configuration structure
 */
typedef struct {
    uint32_t queue_size;                        /*!< Input queue size */
    uint32_t thread_priority;                   /*!< Decode thread priority */
    uint32_t thread_stack_size;                 /*!< Decode thread stack size */
    bk_jpeg_decode_callback_t decode_cbs;        /*!< Decode callback function */
    bk_jpeg_decode_sw_out_format_t out_format;     /*!< Output format */
    bk_jpeg_decode_byte_order_t byte_order;        /*!< Byte order */
} jpeg_decode_manager_config_t;

typedef enum {
    DECODER_TYPE_SW = 0,
    DECODER_TYPE_HW = 1,
} decoder_type_t;

/**
 * @brief JPEG decode manager structure
 */
typedef struct jpeg_decode_manager_t {
    /* Decoder instances */
    bk_jpeg_decode_sw_handle_t sw_decoder;  /*!< decoder */
    bk_jpeg_decode_sw_config_t sw_config;   /*!< decoder configuration */

    bk_jpeg_decode_hw_handle_t hw_decoder;  /*!< decoder hw */
    bk_jpeg_decode_hw_opt_config_t hw_config;  /*!< decoder hw configuration */

    /* Configuration information */
    jpeg_decode_manager_config_t config;  /*!< Manager configuration */

    /* Thread handles */
    beken_thread_t decode_thread;         /*!< Decode thread handle */
    beken_semaphore_t decode_semaphore;   /*!< Decode semaphore handle */
    uint8_t thread_status;

    uint8_t is_suspended;
    uint8_t is_init_decoder;
    uint8_t decoder_type; /* 0: software, 1: hardware */
} jpeg_decode_manager_t;

/**
 * @brief Global decode manager instance pointer
 * Used to access the decode manager in callback functions
 */
extern jpeg_decode_manager_t *g_jpeg_decode_manager;

/**
 * @brief Create JPEG decode manager instance
 *
 * @param config Manager configuration
 * @return jpeg_decode_manager_t* Manager instance pointer, NULL if failed
 */
jpeg_decode_manager_t *jpeg_decode_manager_create(jpeg_decode_manager_config_t *config);

/**
 * @brief Destroy JPEG decode manager instance
 *
 * @param manager Manager instance
 * @return bk_err_t Operation result
 */
bk_err_t jpeg_decode_manager_destroy(jpeg_decode_manager_t *manager);

/**
 * @brief Submit a frame of JPEG image to decoder
 *
 * @param manager Manager instance
 * @param frame JPEG image frame
 * @return bk_err_t Operation result
 */
bk_err_t jpeg_decode_manager_submit_frame(jpeg_decode_manager_t *manager, frame_buffer_t *frame);

/**
 * @brief Suspend JPEG decode manager
 *
 * @param manager Manager instance
 * @return bk_err_t Operation result
 */
bk_err_t jpeg_decode_manager_suspend(jpeg_decode_manager_t *manager);

/**
 * @brief Resume JPEG decode manager
 *
 * @param manager Manager instance
 * @return bk_err_t Operation result
 */
bk_err_t jpeg_decode_manager_resume(jpeg_decode_manager_t *manager);

#ifdef __cplusplus
}
#endif