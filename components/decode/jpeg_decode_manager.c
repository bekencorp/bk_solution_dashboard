/*
 * Copyright (c) 2023 - present, armino(Shanghai) Co., Ltd.
 * All rights reserved.
 *
 * Description: JPEG Decoder Manager Implementation File
 */

#include "jpeg_decode_manager.h"
#include <components/avdk_utils/avdk_check.h>
#include "frame_buffer.h"
#include <os/os.h>
#include <common/bk_err.h>
#include "media_data_process_que.h"
#include <components/bk_jpeg_decode/bk_jpeg_decode_types.h>
#include "network_provisioning.h"

// Ensure BK_ERR_INVAL is properly defined
#ifndef BK_ERR_INVAL
#define BK_ERR_INVAL -1
#endif

/**
 * @brief Global JPEG decode manager instance pointer
 * Used to access the decode manager in callback functions
 */
jpeg_decode_manager_t *g_jpeg_decode_manager = NULL;

#define TAG "jpeg_decode_manager"

#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

static bk_err_t in_complete(frame_buffer_t *in_frame)
{
    media_frame_queue_free(in_frame);
    return BK_OK;
}

static frame_buffer_t *out_malloc(uint32_t size)
{
    frame_buffer_t *frame_temp = NULL;
    // if (get_navigation_type() == NAVIGATION_TYPE_BT)
    // {
    //     if (frame == NULL) {
    //         frame_temp = frame_buffer_display_malloc(size);
    //         if (frame_temp == NULL) {
    //             LOGE("%s %d Failed to allocate output frame\n", __func__, __LINE__);
    //             return NULL;
    //         }
    //         frame = frame_temp;
    //     }
    //     else
    //     {
    //         frame_temp = frame;
    //     }
    // }
    // else
    // {
        frame_temp = frame_buffer_display_malloc(size);
        if (frame_temp == NULL) {
            LOGE("%s %d Failed to allocate output frame\n", __func__, __LINE__);
            return NULL;
        }
    // }
    return frame_temp;
}

/**
 * @brief decoder callback function
 *
 * @param format_type Format type
 * @param result Decoding result
 * @param out_frame Output frame
 * @return bk_err_t Operation result
 */
static bk_err_t out_complete(uint32_t format_type, uint32_t result, frame_buffer_t *out_frame)
{
    LOGV("decode complete, format: %d, result: %d %p\n", format_type, result, out_frame);

    // Get decode manager instance
    jpeg_decode_manager_t *manager = g_jpeg_decode_manager;
    if (manager == NULL) {
        LOGE("JPEG decode manager not initialized\n");
        return BK_ERR_INVAL;
    }

    if (manager->config.decode_cbs.out_complete) {
        manager->config.decode_cbs.out_complete(format_type, result, out_frame);
    }

    return BK_OK;
}

/**
 * @brief Decode thread function
 *
 * @param arg Thread argument, points to decoder manager instance
 */
static void jpeg_decode_thread(void *arg)
{
    jpeg_decode_manager_t *manager = (jpeg_decode_manager_t *)arg;
    decode_input_msg_t msg;
    bk_err_t ret = BK_OK;

    LOGD("JPEG decode thread started\n");

    rtos_set_semaphore(&manager->decode_semaphore);

    while (manager->thread_status) {
        // Get a frame from input queue
        // ret = rtos_pop_from_queue(&manager->input_queue, &msg, BEKEN_WAIT_FOREVER);

        msg.frame = media_frame_queue_get_frame(50);
        if (msg.frame == NULL) {
            // LOGE("Failed to pop from queue: %d\n", ret);
            continue;
        }
        if (manager->is_init_decoder == 0) {
            bk_jpeg_decode_img_info_t img_info = {0};
            img_info.frame = msg.frame;
            ret = bk_get_jpeg_data_info(&img_info);
            if (ret != BK_OK) {
                LOGE("Failed to get image info: %d\n", ret);
                continue;
            }
            if (img_info.format == JPEG_FMT_YUV422) {
                manager->decoder_type = DECODER_TYPE_HW;
            }
            else
            {
                manager->decoder_type = DECODER_TYPE_SW;
            }
            if (manager->decoder_type == DECODER_TYPE_SW)
            {
                // Initialize CP1 decoder configuration
                manager->sw_config.core_id = JPEG_DECODE_CORE_ID_1 | JPEG_DECODE_CORE_ID_2;
                manager->sw_config.out_format = manager->config.out_format;
                manager->sw_config.byte_order = manager->config.byte_order;
                manager->sw_config.decode_cbs.out_complete = out_complete;
                manager->sw_config.decode_cbs.out_malloc = out_malloc;
                manager->sw_config.decode_cbs.in_complete = in_complete;
        
                // Create CP1 decoder instance
                ret = bk_software_jpeg_decode_on_multi_core_new(&manager->sw_decoder, &manager->sw_config);
                if (ret != BK_OK) {
                    LOGE("%s %d Failed to create CP1 decoder: %d\n", __func__, __LINE__, ret);
                    media_frame_queue_free(msg.frame);
                    continue;
                }
        
                // Open CP1 decoder
                ret = bk_jpeg_decode_sw_open(manager->sw_decoder);
                if (ret != BK_OK) {
                    LOGE("%s %d Failed to open CP1 decoder: %d\n", __func__, __LINE__, ret);
                    media_frame_queue_free(msg.frame);
                    continue;
                }
            }
            else
            {
                // Initialize HW decoder configuration
                manager->hw_config.decode_cbs.out_complete = out_complete;
                manager->hw_config.decode_cbs.out_malloc = out_malloc;
                manager->hw_config.decode_cbs.in_complete = in_complete;
                manager->hw_config.lines_per_block = 8;
                manager->hw_config.copy_method = 0;
                manager->hw_config.is_pingpong = 1;
                manager->hw_config.image_max_width = 1024;
                manager->hw_config.sram_buffer = NULL;
        
                // Create HW decoder instance
                ret = bk_hardware_jpeg_decode_opt_new(&manager->hw_decoder, &manager->hw_config);
                if (ret != BK_OK) {
                    LOGE("%s %d Failed to create HW decoder: %d\n", __func__, __LINE__, ret);
                    media_frame_queue_free(msg.frame);
                    continue;
                }
        
                // Open HW decoder
                ret = bk_jpeg_decode_hw_open(manager->hw_decoder);
                if (ret != BK_OK) {
                    LOGE("%s %d Failed to open HW decoder: %d\n", __func__, __LINE__, ret);
                    media_frame_queue_free(msg.frame);
                    continue;
                }
            }
            manager->is_init_decoder = 1;
        }
        if (manager->thread_status == 0) {
            media_frame_queue_free(msg.frame);
            continue;
        }

        if (manager->is_suspended) {
            media_frame_queue_free(msg.frame);
            continue;
        }
        if (g_jpeg_decode_manager->decoder_type == DECODER_TYPE_SW)
        {
            // Decode frame
            ret = bk_jpeg_decode_sw_decode_async(manager->sw_decoder, msg.frame);
            if (ret != BK_OK) {
                LOGE("Failed to decode frame: %d\n", ret);
                out_complete(PIXEL_FMT_YUYV, ret, msg.frame);
                continue;
            }
        }
        else
        {
            frame_buffer_t *out_frame = NULL;
            uint32_t size = 0;
            bk_jpeg_decode_img_info_t img_info = {0};
            img_info.frame = msg.frame;
            ret = bk_jpeg_decode_hw_get_img_info(manager->hw_decoder, &img_info);
            if (ret != BK_OK) {
                LOGE("Failed to get image info: %d\n", ret);
                media_frame_queue_free(msg.frame);
                continue;
            }
    
            size = img_info.width * img_info.height * 2;
            out_frame = out_malloc(size);
            if (out_frame == NULL) {
                LOGE("Failed to allocate output frame: %d\n", ret);
                media_frame_queue_free(msg.frame);
                continue;
            }
            // Decode frame
            ret = bk_jpeg_decode_hw_decode(manager->hw_decoder, msg.frame, out_frame);
            if (ret != BK_OK) {
                LOGE("Failed to decode frame: %d\n", ret);
                media_frame_queue_free(msg.frame);
                media_frame_queue_free(out_frame);
                continue;
            }
        }
    }

    LOGD("JPEG decode thread exited\n");
    rtos_set_semaphore(&manager->decode_semaphore);
    rtos_delete_thread(NULL);
}

/**
 * @brief Create JPEG decode manager instance
 *
 * @param config Manager configuration
 * @return jpeg_decode_manager_t* Manager instance pointer, NULL if failed
 */
jpeg_decode_manager_t *jpeg_decode_manager_create(jpeg_decode_manager_config_t *config)
{
    bk_err_t ret = BK_OK;
    jpeg_decode_manager_t *manager = NULL;

    // Parameter check
    if (config == NULL) {
        LOGE("Invalid config parameter\n");
        return NULL;
    }

    // Allocate memory for manager
    manager = os_malloc(sizeof(jpeg_decode_manager_t));
    if (manager == NULL) {
        LOGE("Failed to allocate memory for manager\n");
        return NULL;
    }

    // Initialize manager
    os_memset(manager, 0, sizeof(jpeg_decode_manager_t));
    os_memcpy(&manager->config, config, sizeof(jpeg_decode_manager_config_t));

    // Set global manager pointer
    g_jpeg_decode_manager = manager;

    ret = rtos_init_semaphore(&manager->decode_semaphore, 1);
    if (ret != BK_OK) {
        LOGE("Failed to init semaphore: %d\n", ret);
        goto error;
    }

    manager->thread_status = 1;
    // Create decode thread
    ret = rtos_create_thread(&manager->decode_thread, config->thread_priority,
                            "jpeg_decode_thread", jpeg_decode_thread,
                            config->thread_stack_size, manager);
    if (ret != BK_OK) {
        LOGE("Failed to create decode thread: %d\n", ret);
        goto error;
    }

    rtos_get_semaphore(&manager->decode_semaphore, BEKEN_WAIT_FOREVER);

    LOGD("JPEG decode manager created successfully\n");
    return manager;

error:
    // Clean up allocated resources
    if (manager) {
        if (manager->decode_semaphore) {
            rtos_deinit_semaphore(&manager->decode_semaphore);
        }
        if (g_jpeg_decode_manager->decoder_type == DECODER_TYPE_SW)
        {
            if (manager->sw_decoder) {
                bk_jpeg_decode_sw_close(manager->sw_decoder);
                bk_jpeg_decode_sw_delete(manager->sw_decoder);
            }
        }
        else
        {
            if (manager->hw_decoder) {
                bk_jpeg_decode_hw_close(manager->hw_decoder);
                bk_jpeg_decode_hw_delete(manager->hw_decoder);
            }
        }
        os_free(manager);
    }

    return NULL;
}

bk_err_t jpeg_decode_manager_suspend(jpeg_decode_manager_t *manager)
{
    manager->is_suspended = 1;
    return BK_OK;
}

bk_err_t jpeg_decode_manager_resume(jpeg_decode_manager_t *manager)
{
    manager->is_suspended = 0;
    return BK_OK;
}


/**
 * @brief Destroy JPEG decode manager instance
 *
 * @param manager Manager instance
 * @return bk_err_t Operation result
 */
bk_err_t jpeg_decode_manager_destroy(jpeg_decode_manager_t *manager)
{
    bk_err_t ret = BK_OK;

    // Parameter check
    if (manager == NULL) {
        LOGE("%s %d Invalid manager parameter\n", __func__, __LINE__);
        return BK_ERR_INVAL;
    }

    // Send exit message to decode thread
    manager->thread_status = 0;

    LOGD("%s %d wait for thread exit\n", __func__, __LINE__);
    // Wait for thread to exit
    rtos_get_semaphore(&manager->decode_semaphore, BEKEN_WAIT_FOREVER);

    frame_buffer_t *frame = NULL;
    do {
        frame = media_frame_queue_get_frame(500);
        if (frame) {
            media_frame_queue_free(frame);
        }
    } while (frame);

    if (g_jpeg_decode_manager->decoder_type == DECODER_TYPE_SW)
    {
        // Close and delete CP1 decoder
        ret = bk_jpeg_decode_sw_close(manager->sw_decoder);
        if (ret != BK_OK) {
            LOGE("%s %d Failed to close decoder: %d\n", __func__, __LINE__, ret);
        }

        ret = bk_jpeg_decode_sw_delete(manager->sw_decoder);
        if (ret != BK_OK) {
            LOGE("%s %d Failed to delete decoder: %d\n", __func__, __LINE__, ret);
        }
    }
    else
    {
        ret = bk_jpeg_decode_hw_close(manager->hw_decoder);
        if (ret != BK_OK) {
            LOGE("%s %d Failed to close decoder: %d\n", __func__, __LINE__, ret);
        }

        ret = bk_jpeg_decode_hw_delete(manager->hw_decoder);
        if (ret != BK_OK) {
            LOGE("%s %d Failed to delete decoder: %d\n", __func__, __LINE__, ret);
        }
    }

    // Delete semaphore
    ret = rtos_deinit_semaphore(&manager->decode_semaphore);
    if (ret != BK_OK) {
        LOGE("%s %d Failed to deinit semaphore: %d\n", __func__, __LINE__, ret);
    }

    // Release manager memory
    os_free(manager);

    LOGD("JPEG decode manager destroyed successfully\n");
    return BK_OK;
}

/**
 * @brief Submit a frame of JPEG image to decoder
 *
 * @param manager Manager instance
 * @param frame JPEG image frame
 * @return bk_err_t Operation result
 */
bk_err_t jpeg_decode_manager_submit_frame(jpeg_decode_manager_t *manager, frame_buffer_t *frame)
{
    bk_err_t ret = BK_OK;

    // Parameter check
    if (manager == NULL || frame == NULL) {
        LOGE("Invalid parameters\n");
        return BK_ERR_INVAL;
    }

    // Decode frame
    if (manager->decoder_type == DECODER_TYPE_SW)
    {
        ret = bk_jpeg_decode_sw_decode_async(manager->sw_decoder, frame);
        if (ret != BK_OK) {
            LOGE("Failed to decode frame: %d\n", ret);
        }
    }
    else
    {
        frame_buffer_t *out_frame = NULL;
        uint32_t size = 0;
        bk_jpeg_decode_img_info_t img_info = {0};
        img_info.frame = frame;
        ret = bk_jpeg_decode_hw_get_img_info(manager->hw_decoder, &img_info);
        if (ret != BK_OK) {
            LOGE("Failed to get image info: %d\n", ret);
            return ret;
        }
        size = img_info.width * img_info.height * 2;
        out_frame = out_malloc(size);
        if (out_frame == NULL) {
            LOGE("Failed to allocate output frame: %d\n", ret);
            return ret;
        }
        ret = bk_jpeg_decode_hw_decode(manager->hw_decoder, frame, out_frame);
        if (ret != BK_OK) {
            LOGE("Failed to decode frame: %d\n", ret);
        }
    }

    LOGD("Frame submitted to decode queue, sequence: %d\n", frame->sequence);
    return ret;
}