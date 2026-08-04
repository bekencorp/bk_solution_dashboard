/*
 * Copyright 2025-2026 Beken
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Description: JPEG Stream Pipeline - high-level interface for JPEG screen-cast.
 *
 * Full pipeline: JPEG input -> VCDEC FLEXA decode -> bk_gpu_ctlr strip-parallel
 * GPU compress -> output frame callback.
 *
 * Design principles:
 *  - Opaque handle hides all implementation details
 *  - Callback-driven output, not bound to any specific display logic
 *  - Clear memory ownership: caller owns JPEG stream, pipeline owns ring buffer,
 *    malloc_cb-allocated output frames are owned by frame_display_cb receiver
 */

 #pragma once

 #include <stdint.h>
 #include <stdbool.h>
 #include <components/avdk_utils/avdk_error.h>
 #include <common/avdk_pixel_types.h>
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* ------------------------------------------------------------------ */
 /* Opaque handle                                                       */
 /* ------------------------------------------------------------------ */
 typedef struct jpeg_stream_pipeline_ctx *jpeg_stream_pipeline_handle_t;
 
 /* ------------------------------------------------------------------ */
 /* Callback types                                                      */
 /* ------------------------------------------------------------------ */
 
 /**
  * @brief Output frame ready callback.
  *
  * Called by GPU controller when a full compressed frame is ready.
  * Frame ownership is transferred to the callee; callee must free it.
  *
  * @warning Runs in GPU internal task context. Do NOT block.
  *
  * @param frame       GPU output frame base address.
  * @param frame_size  Frame size in bytes.
  * @param user_data   user_data passed at create time.
  */
 typedef void (*jsp_frame_display_cb_t)(void *frame, uint32_t frame_size, void *user_data);
 
 /**
  * @brief GPU output frame buffer allocator.
  *
  * @param size  Required buffer size in bytes.
  * @return      Allocated buffer pointer, or NULL on failure.
  */
 typedef void *(*jsp_malloc_cb_t)(uint32_t size);
 
 /**
  * @brief JPEG stream consumed callback (optional).
  *
  * Called after VCDEC finishes decoding one frame, notifying the caller
  * that the JPEG stream buffer can be released.
  * If NULL, the caller must ensure the stream remains valid from push_frame
  * until the next push_frame call.
  *
  * @param jpeg_stream  JPEG bitstream pointer (same as push_frame / push_frame_ex first arg).
  * @param status       Decode result: 0 = success, non-zero = failure.
  * @param jpeg_owner   Opaque handle from push_frame_ex (NULL if push_frame was used).
  *                     Typical: frame_buffer_t* when jpeg_stream is fb->frame, for
  *                     media_frame_queue_free((frame_buffer_t *)jpeg_owner).
  * @param user_data    user_data passed at create time.
  */
 typedef void (*jsp_frame_consumed_cb_t)(const uint8_t *jpeg_stream, int status,
										 void *jpeg_owner, void *user_data);

 /** Status passed to frame_consumed_cb when a queued frame is dropped to reduce latency. */
 #define JSP_STATUS_FRAME_DROPPED  1

 /* ------------------------------------------------------------------ */
 /* Configuration                                                       */
 /* ------------------------------------------------------------------ */
 
 typedef struct {
	 /* Source resolution (JPEG image size) */
	 uint16_t src_width;
	 uint16_t src_height;
 
	 /* Destination resolution (GPU output / display) */
	 uint16_t dst_width;
	 uint16_t dst_height;
 
	 /* GPU parameters */
	 uint16_t rotate_degree;            /**< 0 or 90 */
	 bk_pixel_format_t dst_format;      /**< Typically BK_PIXEL_FORMAT_ARGB8888 */
	 bool     compress;                 /**< true: enable DPU HV sample compression */
	 bool     scale;                    /**< true: enable GPU scaling */
 
	 /* FLEXA ring buffer parameters (0 = use default) */
	 uint8_t  flexa_lines;              /**< Lines per strip, default 16 */
	 uint8_t  flexa_buff_cnt;           /**< Ring buffer slot count, default 2 */
 
	 /* Memory management */
	 jsp_malloc_cb_t        malloc_cb;          /**< GPU output frame allocator, must not be NULL */
 
	 /* Callbacks */
	 jsp_frame_display_cb_t  frame_display_cb;  /**< Frame ready callback, must not be NULL */
	 jsp_frame_consumed_cb_t frame_consumed_cb; /**< Stream consumed callback, may be NULL */
	 void *user_data;                           /**< Passed through to all callbacks */
 
	 /* Decode task parameters (0 = use default) */
	 uint32_t decode_timeout_ms;        /**< VCDEC decode timeout, default 1000ms */
	 uint32_t task_priority;            /**< Decode task priority, default BEKEN_DEFAULT_WORKER_PRIORITY */
	 uint32_t task_stack_size;          /**< Decode task stack size in bytes, default 4096 */
	 uint32_t queue_depth;              /**< Frame queue depth, default 4 */
} jpeg_stream_pipeline_config_t;
 
 /* ------------------------------------------------------------------ */
 /* Lifecycle API                                                       */
 /* ------------------------------------------------------------------ */
 
 /**
  * @brief Create a pipeline instance.
  *
  * Allocates internal context, initializes VCDEC decoder, GPU controller,
  * and establishes FLEXA bond. On success the pipeline is in ready state;
  * call start() to begin accepting frames.
  *
  * @param[out] handle  Receives the opaque handle on success.
  * @param[in]  config  Configuration (deep-copied internally, caller may free after return).
  * @return AVDK_ERR_OK or error code.
  */
 avdk_err_t jpeg_stream_pipeline_create(jpeg_stream_pipeline_handle_t *handle,
										const jpeg_stream_pipeline_config_t *config);
 
 /**
  * @brief Start the pipeline.
  *
  * Spawns the internal decode task thread. push_frame() can be called after this.
  */
 avdk_err_t jpeg_stream_pipeline_start(jpeg_stream_pipeline_handle_t handle);
 
 /**
  * @brief Push one JPEG frame with an optional per-frame release handle.
  *
  * Same as push_frame, but @p jpeg_owner is stored per queue entry and passed
  * to frame_consumed_cb (e.g. frame_buffer_t* while jpeg_stream is fb->frame).
  * Pass NULL if not used (equivalent to push_frame).
  */
 avdk_err_t jpeg_stream_pipeline_push_frame(jpeg_stream_pipeline_handle_t handle,
											   const uint8_t *jpeg_stream,
											   uint32_t jpeg_len,
											   void *jpeg_owner);
 
 /**
  * @brief Stop the pipeline.
  *
  * Stops accepting new frames and waits for the current frame to finish.
  */
 avdk_err_t jpeg_stream_pipeline_stop(jpeg_stream_pipeline_handle_t handle);
 
 /**
  * @brief Destroy the pipeline and release all resources.
  *
  * Must be called after stop(). The handle becomes invalid after this call.
  */
 avdk_err_t jpeg_stream_pipeline_destroy(jpeg_stream_pipeline_handle_t handle);
 
 #ifdef __cplusplus
 }
 #endif
 