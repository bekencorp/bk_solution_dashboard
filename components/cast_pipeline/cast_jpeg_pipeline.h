/*
 * Copyright (c) 2023 - present, armino(Shanghai) Co., Ltd.
 * Casting path: JPEG -> jpeg_stream_pipeline -> DPU.
 */

#pragma once

#include <stdint.h>
#include <components/bk_display.h>
#include <common/bk_err.h>
#include <common/avdk_pixel_types.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_SCOOTER_CAST_JPEG_SRC_WIDTH)
#define CAST_JPEG_SRC_WIDTH   ((uint32_t)CONFIG_SCOOTER_CAST_JPEG_SRC_WIDTH)
#else
#define CAST_JPEG_SRC_WIDTH   1920U
#endif

#if defined(CONFIG_SCOOTER_CAST_JPEG_SRC_HEIGHT)
#define CAST_JPEG_SRC_HEIGHT  ((uint32_t)CONFIG_SCOOTER_CAST_JPEG_SRC_HEIGHT)
#else
#define CAST_JPEG_SRC_HEIGHT  1080U
#endif

#if defined(CONFIG_SCOOTER_CAST_JPEG_ROTATE_DEG)
#define CAST_JPEG_ROTATE_DEG  ((uint16_t)CONFIG_SCOOTER_CAST_JPEG_ROTATE_DEG)
#else
#define CAST_JPEG_ROTATE_DEG  90U
#endif

#if defined(CONFIG_SCOOTER_CAST_JPEG_DST_WIDTH)
#define CAST_JPEG_DST_WIDTH   ((uint32_t)CONFIG_SCOOTER_CAST_JPEG_DST_WIDTH)
#else
#define CAST_JPEG_DST_WIDTH   1920U
#endif

#if defined(CONFIG_SCOOTER_CAST_JPEG_DST_HEIGHT)
#define CAST_JPEG_DST_HEIGHT  ((uint32_t)CONFIG_SCOOTER_CAST_JPEG_DST_HEIGHT)
#else
#define CAST_JPEG_DST_HEIGHT  1080U
#endif

typedef struct {
	/* Called before creating and starting the cast pipeline. */
	void (*pre_start)(void);
	/* Called on first displayed frame to apply deferred display state. */
	void (*first_frame_apply)(void);
	/* Called when cast stops or startup fails, for state restore/cleanup. */
	void (*post_stop)(void);
} cast_jpeg_pipeline_hooks_t;

void cast_jpeg_pipeline_register_hooks(const cast_jpeg_pipeline_hooks_t *hooks);

/**
 * Optional: block WiFi unfragment frame_buffer allocation during cast teardown/setup
 * to avoid racing malloc/send with jpeg_stream_pipeline_destroy. Media registers
 * media_bk_net_cast_video_alloc_gate; allow 0 = deny malloc, 1 = allow.
 */
typedef void (*cast_jpeg_video_recv_alloc_gate_fn)(int allow);
void cast_jpeg_pipeline_set_video_recv_alloc_gate_fn(cast_jpeg_video_recv_alloc_gate_fn fn);

/**
 * One-time setup: switch LVGL/DPU to casting mode, create and start pipeline.
 * @param disp  bk_display controller from vendor_config.args (DPU path).
 */
bk_err_t cast_jpeg_pipeline_turn_on(bk_display_ctlr_handle_t disp);

/** Stop pipeline, destroy, restore LVGL/DPU for UI. */
bk_err_t cast_jpeg_pipeline_turn_off(void);

/**
 * Wait for a DPU flush interrupt observed by the cast display callback.
 * Used during teardown to avoid switching DPU format while a cast frame is
 * still pending in the DPU update slot.
 */
bk_err_t cast_jpeg_pipeline_wait_display_flush(uint32_t timeout_ms);

bk_err_t cast_jpeg_pipeline_suspend(void);
bk_err_t cast_jpeg_pipeline_resume(void);

/**
 * Register how to release frame_buffer_t used as jpeg_owner (e.g. media_frame_queue_free).
 * Call from media after media_frame_queue_init (e.g. media_bk_network_transfer_init).
 */
void cast_jpeg_pipeline_set_frame_buffer_release(void (*fn)(frame_buffer_t *));

/**
 * Push one MJPEG frame using @p fb->frame / fb->length; ownership transfers to the pipeline
 * (zero-copy). Requires set_frame_buffer_release() for consumed-side free.
 * Serialized by an internal mutex (safe from multiple threads / SMP).
 */
bk_err_t cast_jpeg_pipeline_push_frame_buffer(frame_buffer_t *fb);

#ifdef __cplusplus
}
#endif
