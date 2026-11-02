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
 * Description: Decode Stream Pipeline - internal interface shared by the
 * core (decode_stream_pipeline.c) and the codec backends
 * (decode_stream_pipeline_jpeg.c / decode_stream_pipeline_h264.c).
 *
 * Nothing in this header is part of the public API.
 */

#pragma once

#include "decode_stream_pipeline.h"

#include <os/os.h>
#include <os/mem.h>
#include <string.h>
#include <components/log.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_decode/bk_jpeg_decode_ctlr.h>
#include <components/bk_decode/bk_jpeg_decode_types.h>
#include <components/bk_decode/bk_h264_decode_ctlr.h>
#include <components/bk_decode/bk_h264_decode_types.h>
#include <components/bk_gpu_ctlr.h>
#include <components/bk_gpu.h>
#include <components/bk_flexa_bond.h>

#undef TAG
#define TAG "dsp"

#undef LOGI
#undef LOGW
#undef LOGE
#undef LOGD
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* Per-frame trace: compiled OUT by default (avoid log flood regardless of
 * runtime log level). Set CAST_TRACE_PER_FRAME=1 to re-enable for debugging. */
#ifndef CAST_TRACE_PER_FRAME
#define CAST_TRACE_PER_FRAME 0
#endif
#if CAST_TRACE_PER_FRAME
#define LOGPF(...) BK_LOGI(TAG, ##__VA_ARGS__)
#else
#define LOGPF(...) do {} while (0)
#endif

/* ------------------------------------------------------------------ */
/* Per-codec frame metadata                                            */
/* ------------------------------------------------------------------ */
typedef struct {
	uint16_t width;
	uint16_t height;
	uint8_t  sof_marker;
	uint8_t  precision;
	uint8_t  components;
	uint8_t  h[3];
	uint8_t  v[3];
} dsp_jpeg_meta_t;

/* H264 NALU types we care about (nal_unit_type, ITU-T H.264 table 7-1). */
#define DSP_H264_NALU_SLICE   1U   /* non-IDR slice, needs its reference chain */
#define DSP_H264_NALU_IDR     5U   /* IDR slice, self-contained */
#define DSP_H264_NALU_SEI     6U
#define DSP_H264_NALU_SPS     7U
#define DSP_H264_NALU_PPS     8U
#define DSP_H264_NALU_AUD     9U

/* slice_type of the first slice, already folded down from the 5..9 range. */
#define DSP_H264_SLICE_P        0U
#define DSP_H264_SLICE_B        1U
#define DSP_H264_SLICE_I        2U
#define DSP_H264_SLICE_SP       3U
#define DSP_H264_SLICE_SI       4U
#define DSP_H264_SLICE_UNKNOWN  0xFFU

typedef struct {
	uint8_t is_h264;    /* at least one well-formed NALU header was found */
	uint8_t has_idr;    /* access unit carries an IDR slice */
	uint8_t has_sps;
	uint8_t slice_type; /* DSP_H264_SLICE_*, UNKNOWN if no slice header parsed */
} dsp_h264_meta_t;

typedef union {
	dsp_jpeg_meta_t jpeg;
	dsp_h264_meta_t h264;
} dsp_frame_meta_t;

/* ------------------------------------------------------------------ */
/* Frame queue entry (defined before the backend table so that         */
/* decode_frame can take a complete type)                              */
/* ------------------------------------------------------------------ */
typedef struct dsp_frame_entry {
	const uint8_t              *stream;
	uint32_t                    len;
	void                       *owner;
	const struct dsp_codec_ops *ops;   /* backend snapshot taken at push time */
} dsp_frame_entry_t;

/* ------------------------------------------------------------------ */
/* Codec backend interface                                             */
/* ------------------------------------------------------------------ */
struct decode_stream_pipeline_ctx;

/*
 * One codec = one backend. The pipeline core (queue, decode task, GPU, ring)
 * never branches on the codec; everything codec-specific hangs off this table.
 */
typedef struct dsp_codec_ops {
	const char *name;

	/* Create + init + open the hardware decoder. Must be idempotent: a
	 * no-op when the decoder is already alive (backends are brought up
	 * lazily and survive codec switches). */
	avdk_err_t (*decoder_setup)(struct decode_stream_pipeline_ctx *ctx);

	/* Close + deinit + delete the decoder. No-op when not alive. */
	void (*decoder_teardown)(struct decode_stream_pipeline_ctx *ctx);

	/* Connect / disconnect the codec's FLEXA bond to the GPU. */
	avdk_err_t (*bond_start)(struct decode_stream_pipeline_ctx *ctx);
	void       (*bond_stop)(struct decode_stream_pipeline_ctx *ctx);

	/* Submit one frame to the decoder. Runs on the decode task with
	 * ctx->codec_lock held. */
	avdk_err_t (*decode_frame)(struct decode_stream_pipeline_ctx *ctx,
				   const dsp_frame_entry_t *entry);

	/* Producer-side admission control. Returns AVDK_ERR_OK to enqueue,
	 * an error (typically AVDK_ERR_BUSY) to reject the frame. */
	avdk_err_t (*admit_frame)(struct decode_stream_pipeline_ctx *ctx,
				  const dsp_frame_meta_t *meta);

	/* Called once after the backend becomes active. Optional. */
	void (*on_activate)(struct decode_stream_pipeline_ctx *ctx);

	/* Queue-full policy (what to do when rtos_push_to_queue fails). */
	void (*on_push_full)(struct decode_stream_pipeline_ctx *ctx, uint32_t seq,
			     const dsp_frame_meta_t *meta, uint32_t len);

	/* Classify a raw frame for this codec. Returns 1 and fills meta on
	 * match, 0 otherwise. */
	int (*parse_meta)(const uint8_t *buf, uint32_t len, dsp_frame_meta_t *meta);

	/* Per-frame diagnostics after a successful enqueue. */
	void (*diag_frame)(struct decode_stream_pipeline_ctx *ctx, uint32_t seq,
			   const dsp_frame_meta_t *meta, uint32_t len);
} dsp_codec_ops_t;

/* Backends implemented in decode_stream_pipeline_{jpeg,h264}.c. */
extern const dsp_codec_ops_t dsp_jpeg_ops;
extern const dsp_codec_ops_t dsp_h264_ops;

/* ------------------------------------------------------------------ */
/* Per-codec private state (owned by the matching backend file; the    */
/* core never touches these fields)                                    */
/* ------------------------------------------------------------------ */
typedef struct {
	bk_jpeg_decode_ctlr_handle_t handle;   /* NULL until first use */
} dsp_jpeg_state_t;

typedef struct {
	bk_h264_decode_ctlr_handle_t handle;   /* NULL until first use */
	uint8_t  drop_until_idr;               /* GOP chain broken: drop until next IDR */
	uint32_t gop_dropped;                  /* frames dropped since chain break */
} dsp_h264_state_t;

/* ------------------------------------------------------------------ */
/* Internal context (actual type behind the opaque handle)             */
/* ------------------------------------------------------------------ */
struct decode_stream_pipeline_ctx {
	decode_stream_pipeline_config_t cfg;

	/* Active codec backend (never NULL once create() succeeded). */
	const dsp_codec_ops_t *ops;

	/* Codec private states (see above). */
	dsp_jpeg_state_t jpeg;
	dsp_h264_state_t h264;

	bk_gpu_ctlr_handle_t gpu_handle;

	/* The one active FLEXA bond (owned by ctx->ops). */
	void *bond;

	/* FLEXA ring buffer */
	uint8_t *ring_buf;          /* 64-byte aligned address */
	void    *ring_raw;          /* Unaligned block from psram_malloc (for free) */
	uint32_t ring_size;

	/* Decode task */
	beken_thread_t    task_handle;
	beken_semaphore_t task_sem;
	beken_queue_t     frame_queue;
	/* Serializes backend switching (producer thread) against in-flight
	 * decodes (decode task): a codec switch must never tear down a bond
	 * while the other codec's decoder is mid-frame. */
	beken_mutex_t     codec_lock;
	volatile uint8_t  running;
	uint32_t          push_seq;
	uint32_t          dec_seq;
};

/* ------------------------------------------------------------------ */
/* Shared services provided by the core, used by the backends          */
/* ------------------------------------------------------------------ */

/* Pop and release everything still queued. Returns how many frames were
 * dropped. Owners are notified via cfg.frame_consumed_cb
 * (DSP_STATUS_FRAME_DROPPED). */
uint32_t dsp_drain_queue(struct decode_stream_pipeline_ctx *ctx);

/* Aggregated logging for low-latency stale drops (called by JPEG admit). */
void dsp_report_stale_drops(uint32_t dropped);

/* Decoder frame_done callback shared by both backends. */
void dsp_decoder_frame_done(int status, void *args);
