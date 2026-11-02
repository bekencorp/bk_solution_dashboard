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
 * Description: Decode Stream Pipeline - codec-agnostic compressed video
 * stream -> VCDEC FLEXA decode -> GPU strip-parallel processing -> output
 * frame callback.
 *
 * Codecs are pluggable backends (JPEG, H264). The codec to start with is
 * selected via decode_stream_pipeline_config_t::decode_type; whichever codec
 * is not selected is created lazily on the first frame of that type, and the
 * pipeline switches backends (both directions) automatically.
 *
 * Design principles:
 *  - Opaque handle hides all implementation details
 *  - Callback-driven output, not bound to any specific display logic
 *  - Clear memory ownership: caller owns the compressed stream, pipeline owns
 *    the FLEXA ring, malloc_cb-allocated output frames are owned by the
 *    frame_display_cb receiver
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
typedef struct decode_stream_pipeline_ctx *decode_stream_pipeline_handle_t;

/* ------------------------------------------------------------------ */
/* Codec selection                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Initial codec of the pipeline.
 *
 * The selected codec's hardware decoder is created eagerly in create(); the
 * other codec's decoder is created lazily on the first frame of that type.
 * Regardless of this setting, the pipeline re-classifies every pushed frame
 * and switches backends automatically when the stream codec changes
 * (JPEG <-> H264, both directions).
 */
typedef enum {
	DECODE_STREAM_TYPE_AUTO = 0,  /**< Default: start as JPEG, auto-switch per frame */
	DECODE_STREAM_TYPE_JPEG,      /**< Start as JPEG; H264 decoder created lazily */
	DECODE_STREAM_TYPE_H264,      /**< Start as H264; JPEG decoder created lazily */
} decode_stream_type_t;

/* ------------------------------------------------------------------ */
/* Callback types                                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Output frame ready callback.
 *
 * Called by GPU controller when a full processed frame is ready.
 * Frame ownership is transferred to the callee; callee must free it.
 *
 * @warning Runs in GPU internal task context. Do NOT block.
 *
 * @param frame       GPU output frame base address.
 * @param frame_size  Frame size in bytes.
 * @param user_data   user_data passed at create time.
 */
typedef void (*dsp_frame_display_cb_t)(void *frame, uint32_t frame_size, void *user_data);

/**
 * @brief GPU output frame buffer allocator.
 *
 * @param size  Required buffer size in bytes.
 * @return      Allocated buffer pointer, or NULL on failure.
 */
typedef void *(*dsp_malloc_cb_t)(uint32_t size);

/**
 * @brief Compressed stream consumed callback (optional).
 *
 * Called after the decoder finishes one frame, notifying the caller that the
 * stream buffer can be released. May also fire from the producer thread when
 * a queued frame is dropped for latency (status = DSP_STATUS_FRAME_DROPPED)
 * or at stop (status = AVDK_ERR_SHUTDOWN).
 * If NULL, the caller must ensure the stream remains valid from push_frame
 * until the next push_frame call.
 *
 * @param stream     Bitstream pointer (same as push_frame first arg).
 * @param status     Decode result: 0 = success, non-zero = failure / drop.
 * @param owner      Opaque handle from push_frame (NULL if not used).
 *                   Typical: frame_buffer_t* when stream is fb->frame, for
 *                   media_frame_queue_free((frame_buffer_t *)owner).
 * @param user_data  user_data passed at create time.
 */
typedef void (*dsp_frame_consumed_cb_t)(const uint8_t *stream, int status,
					void *owner, void *user_data);

/** Status passed to frame_consumed_cb when a queued frame is dropped to reduce latency. */
#define DSP_STATUS_FRAME_DROPPED  1

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
	/* Initial codec: selects which hardware decoder is set up eagerly in
	 * create(). Auto-switching is always enabled. 0 = DECODE_STREAM_TYPE_AUTO. */
	decode_stream_type_t decode_type;

	/* Source resolution (compressed image size) */
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
	uint8_t  flexa_buff_cnt;           /**< Ring buffer slot count, default 3 */

	/* Memory management */
	dsp_malloc_cb_t        malloc_cb;          /**< GPU output frame allocator, must not be NULL */

	/* Callbacks */
	dsp_frame_display_cb_t  frame_display_cb;  /**< Frame ready callback, must not be NULL */
	dsp_frame_consumed_cb_t frame_consumed_cb; /**< Stream consumed callback, may be NULL */
	void *user_data;                           /**< Passed through to all callbacks */

	/* Decode task parameters (0 = use default) */
	uint32_t decode_timeout_ms;        /**< VCDEC decode timeout, default 1000ms */
	uint32_t task_priority;            /**< Decode task priority, default BEKEN_DEFAULT_WORKER_PRIORITY */
	uint32_t task_stack_size;          /**< Decode task stack size in bytes, default 4096 */
	uint32_t queue_depth;              /**< Frame queue depth, default 1 (JPEG drains before
					     * every push); forced up to 4 for H264 GOP ride-out */
} decode_stream_pipeline_config_t;

/* ------------------------------------------------------------------ */
/* Lifecycle API                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a pipeline instance.
 *
 * Allocates internal context, FLEXA ring, the decoder of the configured
 * decode_type (JPEG by default), GPU controller, and establishes the FLEXA
 * bond. On success the pipeline is in ready state; call start() to begin
 * accepting frames.
 *
 * @param[out] handle  Receives the opaque handle on success.
 * @param[in]  config  Configuration (deep-copied internally, caller may free after return).
 * @return AVDK_ERR_OK or error code.
 */
avdk_err_t decode_stream_pipeline_create(decode_stream_pipeline_handle_t *handle,
					 const decode_stream_pipeline_config_t *config);

/**
 * @brief Start the pipeline.
 *
 * Spawns the internal decode task thread. push_frame() can be called after this.
 */
avdk_err_t decode_stream_pipeline_start(decode_stream_pipeline_handle_t handle);

/**
 * @brief Push one compressed video frame (JPEG or H264 Annex-B access unit)
 *        with an optional per-frame release handle.
 *
 * The pipeline classifies every frame and auto-switches its internal decode
 * path when the stream codec changes (JPEG <-> H264, both directions); the
 * decoder of a newly entered codec is created lazily on first use.
 * JPEG frames are independent: queued-but-undecoded older frames are dropped
 * to keep display latency low. H264 frames keep their reference chain: after
 * a local loss the pipeline drops frames until the next IDR (compile-time
 * option DSP_H264_GOP_DROP_ENABLE).
 *
 * @param stream  Compressed bitstream (JPEG image or H264 Annex-B access unit).
 * @param len     Stream length in bytes.
 * @param owner   Opaque handle stored per queue entry and passed to
 *                frame_consumed_cb (e.g. frame_buffer_t* while stream is
 *                fb->frame). Pass NULL if not used.
 */
avdk_err_t decode_stream_pipeline_push_frame(decode_stream_pipeline_handle_t handle,
					     const uint8_t *stream,
					     uint32_t len,
					     void *owner);

/**
 * @brief Stop the pipeline.
 *
 * Stops accepting new frames and waits for the current frame to finish.
 */
avdk_err_t decode_stream_pipeline_stop(decode_stream_pipeline_handle_t handle);

/**
 * @brief Destroy the pipeline and release all resources.
 *
 * Must be called after stop(). The handle becomes invalid after this call.
 */
avdk_err_t decode_stream_pipeline_destroy(decode_stream_pipeline_handle_t handle);

#ifdef __cplusplus
}
#endif
