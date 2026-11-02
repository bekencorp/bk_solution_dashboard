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
 * Description: JPEG Stream Pipeline Implementation
 *
 * Internal flow:
 *   create:  FLEXA ring in PSRAM (BK7259 VCDEC FLEXA DMA; HSRAM ring caused MemFault in field).
 *            Keeps HSRAM for bk_get_gpu_flexa_buffer inside bk_gpu_init. JPEG -> GPU -> bond.
 *   start:   spawn decode task thread
 *   frame:   decode task pops queue -> bk_jpeg_decode_frame -> VCDEC FLEXA strip output
 *            -> bond auto-notifies GPU -> GPU strip-parallel processing -> frame_display callback
 *   stop:    terminate decode task
 *   destroy: bond stop -> GPU close/deinit/delete -> JPEG close/deinit/delete -> free ring -> free ctx
 *
 * Reference:
 *   doorbell: app_jpeg_decode.c + app_gpu.c (app_gpu_v2_turn_on)
 *   SDK:      bk_gpu_ctlr_default.c, bk_flexa_mjpeg_bond.c
 */

 #include "jpeg_stream_pipeline.h"

 #include <os/os.h>
 #include <os/mem.h>
 #include <string.h>
 #include <cache.h>
 #include <components/log.h>
 #include <common/avdk_pixel_types.h>
 #include <components/bk_decode/bk_jpeg_decode_ctlr.h>
 #include <components/bk_decode/bk_jpeg_decode_types.h>
 #include <components/bk_gpu_ctlr.h>
 #include <components/bk_gpu.h>
#include <components/bk_flexa_bond.h>

#include <cache.h>

#define TAG "jsp"
 
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
 /* Defaults                                                            */
 /* ------------------------------------------------------------------ */
 #define JSP_FLEXA_LINES_DEFAULT       16U
 #define JSP_FLEXA_BUFF_CNT_DEFAULT    3U
 #define JSP_DECODE_TIMEOUT_DEFAULT    1000U   /* ms */
 #define JSP_TASK_STACK_DEFAULT        (4U * 1024U)
 #define JSP_TASK_PRIORITY_DEFAULT     BEKEN_DEFAULT_WORKER_PRIORITY
 #define JSP_QUEUE_DEPTH_DEFAULT       1U
/* H264 cannot drop arbitrary frames, so it needs real queue slots to ride out
 * decode jitter. JPEG is unaffected: it drains the queue before every push, so
 * a deeper queue never holds more than the one frame just pushed. */
 #define JSP_QUEUE_DEPTH_H264_MIN      4U
/* Set back to 1 to re-enable GOP-aware dropping. Kept off while the phone-side
 * frame loss is under investigation: the pipeline must not discard anything of
 * its own, otherwise local and remote losses cannot be told apart. */
 #define JSP_H264_GOP_DROP_ENABLE      1
/* Head of an H264 access unit scanned for NALU classification. */
 #define JSP_H264_SCAN_BYTES           512U
 #define JSP_H264_SCAN_MAX_NALU        8U
 #define JSP_RING_ALIGNMENT            64U
 #define JSP_QUEUE_POP_TIMEOUT_MS      50U
 #define JSP_DIAG_LOG_FIRST_N          10U
 #define JSP_DIAG_LOG_EVERY_N          120U
 
 /* ------------------------------------------------------------------ */
 /* Frame queue entry                                                   */
 /* ------------------------------------------------------------------ */
 typedef struct {
	 const uint8_t *jpeg_stream;
	 uint32_t       jpeg_len;
	 void          *jpeg_owner;
 } jsp_frame_entry_t;
 
 typedef struct {
	 uint16_t width;
	 uint16_t height;
	 uint8_t  sof_marker;
	 uint8_t  precision;
	 uint8_t  components;
 	 uint8_t  h[3];
	 uint8_t  v[3];
 } jsp_jpeg_meta_t;

/* H264 NALU types we care about (nal_unit_type, ITU-T H.264 table 7-1). */
#define JSP_H264_NALU_SLICE   1U   /* non-IDR slice, needs its reference chain */
#define JSP_H264_NALU_IDR     5U   /* IDR slice, self-contained */
#define JSP_H264_NALU_SEI     6U
#define JSP_H264_NALU_SPS     7U
#define JSP_H264_NALU_PPS     8U
#define JSP_H264_NALU_AUD     9U

/* slice_type of the first slice, already folded down from the 5..9 range. */
#define JSP_H264_SLICE_P        0U
#define JSP_H264_SLICE_B        1U
#define JSP_H264_SLICE_I        2U
#define JSP_H264_SLICE_SP       3U
#define JSP_H264_SLICE_SI       4U
#define JSP_H264_SLICE_UNKNOWN  0xFFU

typedef struct {
	uint8_t is_h264;    /* at least one well-formed NALU header was found */
	uint8_t has_idr;    /* access unit carries an IDR slice */
	uint8_t has_sps;
	uint8_t slice_type; /* JSP_H264_SLICE_*, UNKNOWN if no slice header parsed */
} jsp_h264_meta_t;
 
 /* ------------------------------------------------------------------ */
 /* Internal context (actual type behind the opaque handle)             */
 /* ------------------------------------------------------------------ */
 struct jpeg_stream_pipeline_ctx {
	 jpeg_stream_pipeline_config_t cfg;
 
	 /* Hardware handles */
	 bk_jpeg_decode_ctlr_handle_t  jpeg_handle;
	 bk_h264_decode_ctlr_handle_t h264_handle;
	 bk_gpu_ctlr_handle_t          gpu_handle;
	 void                         *bond;
	 void                         *h264_bond;
 
	 /* FLEXA ring buffer */
	 uint8_t  *ring_buf;          /* 64-byte aligned address */
	 void     *ring_raw;          /* Unaligned block from psram_malloc (for free) */
	 uint32_t  ring_size;
 
	 /* Decode task */
	 beken_thread_t    task_handle;
	 beken_semaphore_t task_sem;
	 beken_queue_t     frame_queue;
	 volatile uint8_t  running;
	 uint32_t          push_seq;
	 uint32_t          dec_seq;

	 /* H264 GOP-aware drop state: once the reference chain is broken, keep
	  * dropping until the next IDR rather than feeding the decoder frames whose
	  * references never arrived. */
	 uint8_t           h264_drop_until_idr;
	 uint32_t          h264_gop_dropped;
 };
 
 static uint16_t jsp_be16(const uint8_t *p)
 {
	 return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
 }
 
 static const char *jsp_infer_sampling(const jsp_jpeg_meta_t *m)
 {
	 if (m->components != 3)
		 return "non-3comp";
	 if (m->h[0] == 2U && m->v[0] == 2U && m->h[1] == 1U && m->v[1] == 1U && m->h[2] == 1U && m->v[2] == 1U)
		 return "420";
	 if (m->h[0] == 2U && m->v[0] == 1U && m->h[1] == 1U && m->v[1] == 1U && m->h[2] == 1U && m->v[2] == 1U)
		 return "422";
	 if (m->h[0] == 1U && m->v[0] == 1U && m->h[1] == 1U && m->v[1] == 1U && m->h[2] == 1U && m->v[2] == 1U)
		 return "444";
	 return "other";
 }
 
 static int jsp_parse_jpeg_meta(const uint8_t *buf, uint32_t len, jsp_jpeg_meta_t *m)
 {
	 uint32_t i;
 
	 if (buf == NULL || m == NULL || len < 4U)
		 return 0;
	 if (buf[0] != 0xFFU || buf[1] != 0xD8U)
		 return 0;
 
	 os_memset(m, 0, sizeof(*m));
	 i = 2U;
	 while (i + 3U < len) {
		 uint8_t marker;
		 uint16_t seg_len;
		 uint32_t seg_start;
		 uint32_t comp_start;
		 uint8_t comps;
		 uint8_t k;
 
		 if (buf[i] != 0xFFU) {
			 i++;
			 continue;
		 }
		 while (i + 1U < len && buf[i] == 0xFFU)
			 i++;
		 if (i >= len)
			 break;
 
		 marker = buf[i];
		 if (marker == 0xD9U || marker == 0xDAU)
			 break;
		 if ((marker >= 0xD0U && marker <= 0xD7U) || marker == 0x01U) {
			 i++;
			 continue;
		 }
		 if (i + 2U >= len)
			 break;
 
		 seg_len = jsp_be16(&buf[i + 1U]);
		 if (seg_len < 2U)
			 return 0;
		 seg_start = i + 3U;
		 if (seg_start + (uint32_t)(seg_len - 2U) > len)
			 return 0;
 
		 if (marker == 0xC0U || marker == 0xC1U || marker == 0xC2U) {
			 if (seg_len < 8U)
				 return 0;
			 m->sof_marker = marker;
			 m->precision = buf[seg_start];
			 m->height = jsp_be16(&buf[seg_start + 1U]);
			 m->width = jsp_be16(&buf[seg_start + 3U]);
			 comps = buf[seg_start + 5U];
			 m->components = comps;
			 comp_start = seg_start + 6U;
			 for (k = 0; k < 3U && k < comps; k++) {
				 uint32_t comp_off = comp_start + (uint32_t)k * 3U;
				 if (comp_off + 1U >= seg_start + (uint32_t)(seg_len - 2U))
					 break;
				 m->h[k] = (uint8_t)(buf[comp_off + 1U] >> 4);
				 m->v[k] = (uint8_t)(buf[comp_off + 1U] & 0x0FU);
			 }
			 return 1;
		 }
		 i = i + 1U + (uint32_t)seg_len;
	 }
	 return 0;
 }

/* Annex-B start code at buf[i]? Reports 4-byte form first so the NALU header
 * lands on the right byte for both 00 00 00 01 and 00 00 01. */
static int jsp_h264_start_code(const uint8_t *buf, uint32_t len, uint32_t i, uint32_t *sc_len)
{
	if ((i + 4U) <= len && buf[i] == 0x00U && buf[i + 1U] == 0x00U &&
		buf[i + 2U] == 0x00U && buf[i + 3U] == 0x01U)
	{
		*sc_len = 4U;
		return 1;
	}
	if ((i + 3U) <= len && buf[i] == 0x00U && buf[i + 1U] == 0x00U && buf[i + 2U] == 0x01U)
	{
		*sc_len = 3U;
		return 1;
	}
	return 0;
}

/*
 * Minimal bit reader used only on the first bytes of a slice header.
 * Emulation prevention bytes are deliberately not removed: first_mb_in_slice
 * and slice_type sit within a handful of bytes, far short of the 00 00 03
 * sequence that would need unescaping.
 */
typedef struct {
	const uint8_t *buf;
	uint32_t       len;
	uint32_t       bit;
} jsp_bitrd_t;

static uint32_t jsp_bitrd_u1(jsp_bitrd_t *r)
{
	uint32_t byte = r->bit >> 3;
	uint32_t v;

	if (byte >= r->len)
	{
		return 0U;
	}

	v = (uint32_t)((r->buf[byte] >> (7U - (r->bit & 7U))) & 1U);
	r->bit++;
	return v;
}

/* ue(v) exp-golomb. Returns 0 on truncated input; callers only use the value
 * for logging, so a bad read degrades to "looks like a P slice", not a fault. */
static uint32_t jsp_bitrd_ue(jsp_bitrd_t *r)
{
	uint32_t lead = 0U;
	uint32_t rest = 0U;
	uint32_t k;

	while (lead < 32U && jsp_bitrd_u1(r) == 0U)
	{
		if ((r->bit >> 3) >= r->len)
		{
			return 0U;
		}
		lead++;
	}

	if (lead >= 32U)
	{
		return 0U;
	}

	for (k = 0; k < lead; k++)
	{
		rest = (rest << 1) | jsp_bitrd_u1(r);
	}

	return ((1U << lead) - 1U) + rest;
}

/* Reads slice_type out of a slice header starting at buf[0] (NALU header
 * already consumed) and folds the 5..9 "all slices same type" range down. */
static uint8_t jsp_h264_slice_type(const uint8_t *buf, uint32_t len)
{
	jsp_bitrd_t rd;
	uint32_t    slice_type;

	rd.buf = buf;
	rd.len = len;
	rd.bit = 0U;

	(void)jsp_bitrd_ue(&rd);          /* first_mb_in_slice */
	slice_type = jsp_bitrd_ue(&rd);

	if (slice_type >= 5U)
	{
		slice_type -= 5U;
	}

	return (slice_type <= JSP_H264_SLICE_SI) ? (uint8_t)slice_type
											 : JSP_H264_SLICE_UNKNOWN;
}

static const char *jsp_h264_frame_kind(const jsp_h264_meta_t *m)
{
	if (m->has_idr)
	{
		return "IDR";
	}

	switch (m->slice_type)
	{
	case JSP_H264_SLICE_P:  return "P";
	case JSP_H264_SLICE_B:  return "B";
	case JSP_H264_SLICE_I:  return "I";
	case JSP_H264_SLICE_SP: return "SP";
	case JSP_H264_SLICE_SI: return "SI";
	default:                return "?";
	}
}

/*
 * Classify one access unit. Only the head of the AU is scanned: parameter sets,
 * SEI and the first slice header all sit there, and walking a full 40 KB AU on
 * the producer thread for every frame is not worth it.
 */
static int jsp_parse_h264_meta(const uint8_t *buf, uint32_t len, jsp_h264_meta_t *m)
{
	uint32_t scan_len;
	uint32_t nalu_seen = 0U;
	uint32_t i;

	if (buf == NULL || m == NULL)
	{
		return 0;
	}

	os_memset(m, 0, sizeof(*m));
	m->slice_type = JSP_H264_SLICE_UNKNOWN;

	if (len < 5U)
	{
		return 0;
	}

	scan_len = (len < JSP_H264_SCAN_BYTES) ? len : JSP_H264_SCAN_BYTES;

	for (i = 0; (i + 4U) <= scan_len && nalu_seen < JSP_H264_SCAN_MAX_NALU; )
	{
		uint32_t sc_len = 0U;
		uint8_t  nalu_hdr;
		uint8_t  nalu_type;

		if (!jsp_h264_start_code(buf, scan_len, i, &sc_len))
		{
			i++;
			continue;
		}

		/* The start code may be the last thing that fits in the scan window;
		 * the header byte sits one past it. */
		if ((i + sc_len) >= scan_len)
		{
			break;
		}

		nalu_hdr = buf[i + sc_len];
		i += sc_len + 1U;

		/* forbidden_zero_bit must be 0; anything else is not a NALU header. */
		if ((nalu_hdr & 0x80U) != 0U)
		{
			continue;
		}

		nalu_type = (uint8_t)(nalu_hdr & 0x1FU);
		switch (nalu_type)
		{
		case JSP_H264_NALU_IDR:
		case JSP_H264_NALU_SLICE:
			m->is_h264 = 1U;
			if (nalu_type == JSP_H264_NALU_IDR)
			{
				m->has_idr = 1U;
			}
			/* Only the first slice of the AU decides the reported frame type. */
			if (m->slice_type == JSP_H264_SLICE_UNKNOWN)
			{
				m->slice_type = jsp_h264_slice_type(&buf[i], scan_len - i);
			}
			break;
		case JSP_H264_NALU_SPS:
			m->has_sps = 1U;
			m->is_h264 = 1U;
			break;
		case JSP_H264_NALU_SEI:
		case JSP_H264_NALU_PPS:
		case JSP_H264_NALU_AUD:
			m->is_h264 = 1U;
			break;
		default:
			break;
		}

		nalu_seen++;
	}

	return m->is_h264 ? 1 : 0;
}
 
 /* ------------------------------------------------------------------ */
 /* PSRAM FLEXA ring: VCDEC FLEXA path matches SDK jpeg_decoder_flexa_test (psram_malloc). */
 /* ------------------------------------------------------------------ */
 static void *jsp_psram_aligned_alloc(uint32_t alignment, uint32_t size, void **out_raw)
 {
	 uint32_t total = size + alignment - 1U + (uint32_t)sizeof(void *);
	 void    *raw   = psram_malloc(total);
 
	 if (raw == NULL) {
		 LOGE("psram flexa ring alloc failed need=%u\n", (unsigned)total);
		 return NULL;
	 }
 
	 uintptr_t start   = (uintptr_t)raw + sizeof(void *);
	 uintptr_t aligned = (start + (alignment - 1U)) & ~((uintptr_t)alignment - 1U);
	 ((void **)aligned)[-1] = raw;
 
	 if (out_raw)
		 *out_raw = raw;
	 return (void *)aligned;
 }
 
 static void jsp_psram_aligned_free(void *ptr)
 {
	 if (ptr != NULL)
		 psram_free(((void **)ptr)[-1]);
 }
 
/* ------------------------------------------------------------------ */
/* GPU frame_done internal forwarding                                  */
/* Forwards the just-produced GPU output frame to the pipeline's       */
/* high-level frame_display_cb consumer.                               */
/* ------------------------------------------------------------------ */
static void jsp_gpu_frame_done(void *frame, uint32_t frame_size, void *args)
{
	 struct jpeg_stream_pipeline_ctx *ctx = (struct jpeg_stream_pipeline_ctx *)args;
	 if (ctx != NULL && ctx->cfg.frame_display_cb != NULL)
		 ctx->cfg.frame_display_cb(frame, frame_size, ctx->cfg.user_data);
}
 
 /* ------------------------------------------------------------------ */
 /* JPEG frame done callback                                            */
 /* ------------------------------------------------------------------ */
 static void jsp_jpeg_frame_done(int status, void *args)
 {
	 (void)args;
	 if (status != 0)
		 LOGW("decode frame done status=%d\n", status);
 }
 
 /* ------------------------------------------------------------------ */
 /* Decode task                                                         */
 /* ------------------------------------------------------------------ */
 static void jsp_decode_task(void *arg)
 {
	 struct jpeg_stream_pipeline_ctx *ctx = (struct jpeg_stream_pipeline_ctx *)arg;
	 jsp_frame_entry_t entry;
	 avdk_err_t ret;
 
	 LOGI("decode task started\n");
	 rtos_set_semaphore(&ctx->task_sem);
 
	 while (ctx->running)
	 {
		 ret = rtos_pop_from_queue(&ctx->frame_queue, &entry, JSP_QUEUE_POP_TIMEOUT_MS);
		 if (ret != BK_OK)
			 continue;
 
		 if (!ctx->running)
			 break;
 
		 if (entry.jpeg_stream == NULL || entry.jpeg_len == 0)
		 {
			 LOGW("empty frame, skip\n");
			 continue;
		 }
 
		 ctx->dec_seq++;
		 if (ctx->h264_bond != NULL)
		 {
			 bk_h264_decode_input_t input;
			 os_memset(&input, 0, sizeof(input));
			 input.stream          = (uint8_t *)entry.jpeg_stream;
			 input.stream_len      = entry.jpeg_len;
			 input.out_buffer      = ctx->ring_buf;
			 input.out_buffer_size = ctx->ring_size;

			 ret = bk_h264_decode_frame(ctx->h264_handle, &input);
			 if (ret != AVDK_ERR_OK)
				 LOGE("bk_h264_decode_frame failed: %d\n", (int)ret);
		 }
		 else if (ctx->bond != NULL)
		 {
			 bk_jpeg_decode_input_t input;
			 os_memset(&input, 0, sizeof(input));
			 input.stream          = (uint8_t *)entry.jpeg_stream;
			 input.stream_len      = entry.jpeg_len;
			 input.out_buffer      = ctx->ring_buf;
			 input.out_buffer_size = ctx->ring_size;

			 ret = bk_jpeg_decode_frame(ctx->jpeg_handle, &input);
			 if (ret != AVDK_ERR_OK)
				 LOGE("bk_jpeg_decode_frame failed: %d\n", (int)ret);
		 }
		 else
		 {
			 LOGW("no decoder bond, skip\n");
			 ret = AVDK_ERR_INVAL;
		 }
		 LOGPF("[cast] dec#%u len=%u ret=%d\n",
			  (unsigned)ctx->dec_seq, (unsigned)entry.jpeg_len, (int)ret);
 
		 /* Notify caller that the stream has been consumed */
		 if (ctx->cfg.frame_consumed_cb != NULL)
			 ctx->cfg.frame_consumed_cb(entry.jpeg_stream, (int)ret, entry.jpeg_owner,
										ctx->cfg.user_data);
	 }
 
	 LOGI("decode task exiting\n");
	 rtos_set_semaphore(&ctx->task_sem);
	 rtos_delete_thread(NULL);
 }
 
 /* ------------------------------------------------------------------ */
 /* Public API                                                          */
 /* ------------------------------------------------------------------ */
 
 avdk_err_t jpeg_stream_pipeline_create(jpeg_stream_pipeline_handle_t *handle,
										const jpeg_stream_pipeline_config_t *config)
 {
	 avdk_err_t ret;
 
	 if (handle == NULL || config == NULL)
		 return AVDK_ERR_INVAL;
	 if (config->malloc_cb == NULL || config->frame_display_cb == NULL)
	 {
		 LOGE("malloc_cb and frame_display_cb must not be NULL\n");
		 return AVDK_ERR_INVAL;
	 }
 
	 /* Allocate context */
	 struct jpeg_stream_pipeline_ctx *ctx =
		 (struct jpeg_stream_pipeline_ctx *)os_malloc(sizeof(*ctx));
	 if (ctx == NULL)
		 return AVDK_ERR_NOMEM;
	 os_memset(ctx, 0, sizeof(*ctx));
 
	 /* Deep copy config and fill defaults */
	 os_memcpy(&ctx->cfg, config, sizeof(*config));
	 if (ctx->cfg.flexa_lines    == 0) ctx->cfg.flexa_lines    = JSP_FLEXA_LINES_DEFAULT;
	 if (ctx->cfg.flexa_buff_cnt == 0) ctx->cfg.flexa_buff_cnt = JSP_FLEXA_BUFF_CNT_DEFAULT;
	 if (ctx->cfg.decode_timeout_ms == 0) ctx->cfg.decode_timeout_ms = JSP_DECODE_TIMEOUT_DEFAULT;
	 if (ctx->cfg.task_stack_size   == 0) ctx->cfg.task_stack_size   = JSP_TASK_STACK_DEFAULT;
	 if (ctx->cfg.task_priority     == 0) ctx->cfg.task_priority     = JSP_TASK_PRIORITY_DEFAULT;
	 if (ctx->cfg.queue_depth       == 0) ctx->cfg.queue_depth       = JSP_QUEUE_DEPTH_DEFAULT;
 
	/* ---- Step 1: FLEXA ring buffer ---- */
	uint16_t aligned_w = (config->src_width + 15U) & ~15U;
	ctx->ring_size = bk_image_size_get(aligned_w,
										(uint16_t)(ctx->cfg.flexa_lines * ctx->cfg.flexa_buff_cnt),
										BK_PIXEL_FORMAT_NV12);
 
	 ctx->ring_buf = (uint8_t *)jsp_psram_aligned_alloc(JSP_RING_ALIGNMENT,
													   ctx->ring_size,
													   &ctx->ring_raw);
	 if (ctx->ring_buf == NULL)
	 {
		 LOGE("flexa ring alloc failed (%u bytes PSRAM)\n", ctx->ring_size);
		 ret = AVDK_ERR_NOMEM;
		 goto err_free_ctx;
	 }
	 os_memset(ctx->ring_buf, 0, ctx->ring_size);
	 LOGI("flexa ring sz=%u psram=0x%08x src_align16=%u dst_align16=%u lines=%u cnt=%u\n",
		  (unsigned)ctx->ring_size, (unsigned)(uintptr_t)ctx->ring_buf,
		  (unsigned)aligned_w, (unsigned)((config->dst_width + 15U) & ~15U),
		  (unsigned)ctx->cfg.flexa_lines, (unsigned)ctx->cfg.flexa_buff_cnt);
	 /* ---- Step 2: JPEG decoder ---- */
	 {
		 bk_jpeg_decode_flexa_config_t jpeg_cfg;
		 os_memset(&jpeg_cfg, 0, sizeof(jpeg_cfg));
		 jpeg_cfg.frame_done_cb  = jsp_jpeg_frame_done;
		 jpeg_cfg.flexa_done_cb  = NULL; /* Injected by bond */
		 jpeg_cfg.timeout_ms     = ctx->cfg.decode_timeout_ms;
		 jpeg_cfg.out_width      = config->src_width;
		 jpeg_cfg.out_height     = config->src_height;
		 jpeg_cfg.out_format     = BK_PIXEL_FORMAT_NV12;
		 jpeg_cfg.segment_height = (uint16_t)(ctx->cfg.flexa_lines / 16U);
		 jpeg_cfg.segment_number = ctx->cfg.flexa_buff_cnt;
 
		 ret = bk_jpeg_decode_flexa_ctlr_new(&ctx->jpeg_handle, &jpeg_cfg);
		 if (ret != AVDK_ERR_OK)
		 {
			 LOGE("bk_jpeg_decode_new failed: %d\n", (int)ret);
			 goto err_free_ring;
		 }
 
		 ret = bk_jpeg_decode_init(ctx->jpeg_handle);
		 if (ret != AVDK_ERR_OK)
		 {
			 LOGE("bk_jpeg_decode_init failed: %d\n", (int)ret);
			 goto err_delete_jpeg;
		 }
 
		 ret = bk_jpeg_decode_open(ctx->jpeg_handle);
		 if (ret != AVDK_ERR_OK)
		 {
			 LOGE("bk_jpeg_decode_open failed: %d\n", (int)ret);
			 goto err_deinit_jpeg;
		 }
	 }
	 /* ---- Step 2.1: H264 decoder ---- */
	 {
		bk_h264_decode_flexa_config_t dec_cfg = DEFAULT_H264_DECODE_FLEXA_CONFIG;
		dec_cfg.timeout_ms     = ctx->cfg.decode_timeout_ms;
		dec_cfg.out_width      = config->src_width;
		dec_cfg.out_height     = config->src_height;
		dec_cfg.out_format     = BK_PIXEL_FORMAT_NV12;
		dec_cfg.segment_height = (uint16_t)(ctx->cfg.flexa_lines / 16U);
		dec_cfg.segment_number = ctx->cfg.flexa_buff_cnt;
		dec_cfg.frame_done_cb  = jsp_jpeg_frame_done;
		dec_cfg.frame_done_args= NULL;
		dec_cfg.flexa_done_cb  = NULL;
		dec_cfg.flexa_done_args= NULL;

		ret = bk_h264_decode_flexa_ctlr_new(&ctx->h264_handle, &dec_cfg);
		if (ret != AVDK_ERR_OK)
		{
			LOGE("bk_h264_decode_new failed: %d\n", (int)ret);
			goto err_close_jpeg;
		}
		ret = bk_h264_decode_init(ctx->h264_handle);
		if (ret != AVDK_ERR_OK)
		{
			LOGE("%s: bk_h264_decode_init ret=%d\n", __func__, ret);
			goto err_delete_h264;
		}
		ret = bk_h264_decode_open(ctx->h264_handle);
		if (ret != AVDK_ERR_OK)
		{
			LOGE("%s: bk_h264_decode_open ret=%d\n", __func__, ret);
			goto err_deinit_h264;
		}

	 }
	 /* ---- Step 3: GPU controller ---- */
	 {
		 bk_gpu_ctlr_config_t gpu_cfg;
		 os_memset(&gpu_cfg, 0, sizeof(gpu_cfg));
 
		 gpu_cfg.src_width      = config->src_width;
		 gpu_cfg.src_height     = config->src_height;
		 gpu_cfg.dst_width      = config->dst_width;
		 gpu_cfg.dst_height     = config->dst_height;
		 gpu_cfg.src_format     = BK_PIXEL_FORMAT_NV12;
		 gpu_cfg.dst_format     = config->dst_format;
		 gpu_cfg.rotate_degree  = config->rotate_degree;
		 gpu_cfg.compress       = config->compress;
		 gpu_cfg.scale          = config->scale;
 
		 gpu_cfg.flexa          = true;
		 gpu_cfg.flexa_lines    = ctx->cfg.flexa_lines;
		 gpu_cfg.flexa_buff_cnt = ctx->cfg.flexa_buff_cnt;
		 gpu_cfg.src_buffer     = ctx->ring_buf;
 
		 gpu_cfg.frame_malloc         = ctx->cfg.malloc_cb;
		 gpu_cfg.frame_free           = NULL;
		 gpu_cfg.flexa_line_done      = NULL;
		 gpu_cfg.flexa_line_done_args = NULL;
		 gpu_cfg.frame_done           = jsp_gpu_frame_done;
		 gpu_cfg.frame_done_args      = ctx;
 
		 ret = bk_gpu_ctlr_new(&ctx->gpu_handle, &gpu_cfg);
		 if (ret != AVDK_ERR_OK)
		 {
			 LOGE("bk_gpu_ctlr_new failed: %d\n", (int)ret);
			 goto err_close_h264;
		 }
 
		 ret = bk_gpu_init(ctx->gpu_handle);
		 if (ret != AVDK_ERR_OK)
		 {
			 LOGE("bk_gpu_init failed: %d\n", (int)ret);
			 goto err_delete_gpu;
		 }
 
		 ret = bk_gpu_open(ctx->gpu_handle);
		 if (ret != AVDK_ERR_OK)
		 {
			 LOGE("bk_gpu_open failed: %d\n", (int)ret);
			 goto err_deinit_gpu;
		 }
	 }
	 /* ---- Step 4: FLEXA bond ---- */
	 ret = bk_flexa_mjpegd_gpu_bond_start(&ctx->bond,
										  ctx->jpeg_handle,
										  ctx->gpu_handle);
	 if (ret != AVDK_ERR_OK)
	 {
		 LOGE("bond start failed: %d\n", (int)ret);
		 goto err_close_gpu;
	 }
	 /* ---- Step 5: Frame queue ---- */
	 {
		 uint32_t queue_depth = ctx->cfg.queue_depth;

		 if (queue_depth < JSP_QUEUE_DEPTH_H264_MIN)
			 queue_depth = JSP_QUEUE_DEPTH_H264_MIN;

		 ret = rtos_init_queue(&ctx->frame_queue,
							   "jsp_q",
							   sizeof(jsp_frame_entry_t),
							   queue_depth);
	 }
	 if (ret != BK_OK)
	 {
		 LOGE("queue init failed: %d\n", (int)ret);
		 goto err_stop_bond;
	 }
	 *handle = ctx;
	 LOGI("pipeline created: src=%ux%u dst=%ux%u rot=%u compress=%d\n",
		  config->src_width, config->src_height,
		  config->dst_width, config->dst_height,
		  config->rotate_degree, config->compress);
	 return AVDK_ERR_OK;
 
	 /* ---- Error rollback ---- */
 err_stop_bond:
	 bk_flexa_mjpegd_gpu_bond_stop(ctx->bond);
 err_close_gpu:
	 (void)bk_gpu_close(ctx->gpu_handle);
 err_deinit_gpu:
	 (void)bk_gpu_deinit(ctx->gpu_handle);
 err_delete_gpu:
	 (void)bk_gpu_delete(ctx->gpu_handle);
	 ctx->gpu_handle = NULL;
 err_close_h264:
	 (void)bk_h264_decode_close(ctx->h264_handle);
 err_deinit_h264:
	 (void)bk_h264_decode_deinit(ctx->h264_handle);
 err_delete_h264:
	 (void)bk_h264_decode_delete(ctx->h264_handle);
	 ctx->h264_handle = NULL;
 err_close_jpeg:
	 (void)bk_jpeg_decode_close(ctx->jpeg_handle);
 err_deinit_jpeg:
	 (void)bk_jpeg_decode_deinit(ctx->jpeg_handle);
 err_delete_jpeg:
	 (void)bk_jpeg_decode_delete(ctx->jpeg_handle);
	 ctx->jpeg_handle = NULL;
 err_free_ring:
	 jsp_psram_aligned_free(ctx->ring_buf);
	 ctx->ring_buf = NULL;
 err_free_ctx:
	 os_free(ctx);
	 return ret;
 }
 
 avdk_err_t jpeg_stream_pipeline_start(jpeg_stream_pipeline_handle_t handle)
 {
	 struct jpeg_stream_pipeline_ctx *ctx = handle;
	 avdk_err_t ret;
 
	 if (ctx == NULL)
		 return AVDK_ERR_INVAL;
	 if (ctx->running)
		 return AVDK_ERR_OK;
 
	 ret = rtos_init_semaphore(&ctx->task_sem, 1);
	 if (ret != BK_OK)
		 return (avdk_err_t)ret;
 
	 ctx->h264_drop_until_idr = 0;
	 ctx->h264_gop_dropped    = 0;
	 ctx->running = 1;
 
	 ret = rtos_create_thread(&ctx->task_handle,
									ctx->cfg.task_priority,
									"jsp_dec",
									(beken_thread_function_t)jsp_decode_task,
									ctx->cfg.task_stack_size,
									ctx);
	 if (ret != BK_OK)
	 {
		 LOGE("create decode task failed: %d\n", (int)ret);
		 ctx->running = 0;
		 rtos_deinit_semaphore(&ctx->task_sem);
		 return (avdk_err_t)ret;
	 }
 
	 /* Wait for task ready */
	 rtos_get_semaphore(&ctx->task_sem, BEKEN_WAIT_FOREVER);
 
	 LOGI("pipeline started\n");
	 return AVDK_ERR_OK;
 }
 
/* Pop and release everything still queued. Returns how many frames were dropped. */
static uint32_t jsp_drain_queue(struct jpeg_stream_pipeline_ctx *ctx)
{
	 jsp_frame_entry_t stale;
	 uint32_t dropped = 0;

	 while (rtos_pop_from_queue(&ctx->frame_queue, &stale, BEKEN_NO_WAIT) == BK_OK)
	 {
		 dropped++;
		 if (ctx->cfg.frame_consumed_cb != NULL)
			 ctx->cfg.frame_consumed_cb(stale.jpeg_stream, JSP_STATUS_FRAME_DROPPED,
										stale.jpeg_owner, ctx->cfg.user_data);
	 }

	 return dropped;
}

/*
 * H264 admission control.
 *
 * An IDR decodes standalone, so it both resets the drop state and makes every
 * queued frame stale -- draining there also guarantees the IDR itself gets a
 * slot. A non-IDR frame is only admitted while its reference chain is intact;
 * once a frame has been lost, feeding the decoder the rest of the GOP produces
 * smeared blocks instead of a clean freeze.
 */
static avdk_err_t jsp_h264_admit_frame(struct jpeg_stream_pipeline_ctx *ctx,
									   const jsp_h264_meta_t *m)
{
#if JSP_H264_GOP_DROP_ENABLE
	 if (m->has_idr)
	 {
		 (void)jsp_drain_queue(ctx);

		 if (ctx->h264_drop_until_idr)
		 {
			 LOGW("[cast] h264 resync on IDR (%u frames dropped since chain break)\n",
				  (unsigned)ctx->h264_gop_dropped);
			 ctx->h264_drop_until_idr = 0;
			 ctx->h264_gop_dropped    = 0;
		 }
		 return AVDK_ERR_OK;
	 }

	 if (ctx->h264_drop_until_idr)
	 {
		 ctx->h264_gop_dropped++;
		 return AVDK_ERR_BUSY;
	 }

	 return AVDK_ERR_OK;
#else
	 (void)ctx;
	 (void)m;
	 return AVDK_ERR_OK;
#endif
}

 avdk_err_t jpeg_stream_pipeline_push_frame(jpeg_stream_pipeline_handle_t handle,
											   const uint8_t *jpeg_stream,
											   uint32_t jpeg_len,
											   void *jpeg_owner)
 {
	 struct jpeg_stream_pipeline_ctx *ctx = handle;
	 jsp_jpeg_meta_t meta;
	 jsp_h264_meta_t h264_meta;
	 int      jpeg_ok = 0;
	 uint32_t seq;
 
	 if (ctx == NULL || jpeg_stream == NULL || jpeg_len == 0)
		 return AVDK_ERR_INVAL;
	 if (!ctx->running)
		 return AVDK_ERR_GENERIC;
 
	 seq = ++ctx->push_seq;
	 jsp_frame_entry_t entry = {
		 .jpeg_stream = jpeg_stream,
		 .jpeg_len    = jpeg_len,
		 .jpeg_owner  = jpeg_owner,
	 };

	 /* ---- Classify the frame; the codec switch must happen before the frame is
	  * visible to the decode task, otherwise the task can pick it up while the
	  * old bond is still active. ---- */
	 os_memset(&h264_meta, 0, sizeof(h264_meta));
	 if (ctx->h264_bond != NULL)
	 {
		 (void)jsp_parse_h264_meta(jpeg_stream, jpeg_len, &h264_meta);
	 }
	 else
	 {
		 jpeg_ok = jsp_parse_jpeg_meta(jpeg_stream, jpeg_len, &meta);
		 if (!jpeg_ok && jsp_parse_h264_meta(jpeg_stream, jpeg_len, &h264_meta))
		 {
			 avdk_err_t bond_ret;

			 LOGI("h264 stream detected, switching mjpegd bond to h264d bond\n");
			 if (ctx->bond != NULL)
			 {
				 bk_flexa_mjpegd_gpu_bond_stop(ctx->bond);
				 ctx->bond = NULL;
			 }
			 bond_ret = bk_flexa_h264d_gpu_bond_start(&ctx->h264_bond, ctx->h264_handle, ctx->gpu_handle);
			 if (bond_ret != AVDK_ERR_OK)
			 {
				 LOGE("%s: bk_flexa_h264d_gpu_bond_start ret=%d\n", __func__, bond_ret);
				 ctx->h264_bond = NULL;
				 return bond_ret;
			 }
		 }
	 }

	 /* ---- Admission ---- */
	 if (ctx->h264_bond != NULL)
	 {
		 avdk_err_t admit = jsp_h264_admit_frame(ctx, &h264_meta);

		 if (admit != AVDK_ERR_OK)
			 return admit;
	 }
	 else
	 {
		 /* JPEG frames are independent: keep only the newest so the display
		  * never trails the source. */
		 uint32_t dropped = jsp_drain_queue(ctx);

		 if (dropped > 0) {
			 /* Low-latency stale drops are expected during casting; aggregate to
			  * avoid per-frame log flood while keeping the drop rate observable. */
			 static uint32_t s_stale_drop_total = 0;
			 static uint32_t s_stale_drop_reported = 0;
			 s_stale_drop_total += dropped;
			 if (s_stale_drop_total - s_stale_drop_reported >= 128U) {
				 LOGW("[cast] dropped %u stale frames for low-latency (total %u)\n",
					  (unsigned)(s_stale_drop_total - s_stale_drop_reported),
					  (unsigned)s_stale_drop_total);
				 s_stale_drop_reported = s_stale_drop_total;
			 }
		 }
	 }

	 bk_err_t ret = rtos_push_to_queue(&ctx->frame_queue, &entry, BEKEN_NO_WAIT);
	 if (ret != BK_OK)
	 {
		 if (ctx->h264_bond != NULL)
		 {
#if JSP_H264_GOP_DROP_ENABLE
			 /* The decoder fell behind far enough to fill the queue. This frame is
			  * lost, so every following frame references something that never got
			  * decoded -- drop the rest of the GOP and resync on the next IDR. */
			 if (!ctx->h264_drop_until_idr)
			 {
				 LOGW("[cast] h264 queue full, dropping until next IDR (len=%u)\n",
					  (unsigned)jpeg_len);
				 ctx->h264_drop_until_idr = 1;
				 ctx->h264_gop_dropped    = 0;
			 }
			 ctx->h264_gop_dropped++;
#else
			 /* Every local loss is reported individually so it can be subtracted
			  * from the gap seen at the phone. */
			 LOGW("[cast] h264 push qfull #%u %s len=%u\n",
				  (unsigned)seq, jsp_h264_frame_kind(&h264_meta), (unsigned)jpeg_len);
#endif
		 }
		 else
		 {
			 LOGW("[cast] push qfull %u\n", (unsigned)jpeg_len);
		 }
		 return AVDK_ERR_BUSY;
	 }
 
	 if (ctx->h264_bond != NULL) {
		LOGPF("[diag] in#%u h264 %s len=%u%s\n",
			  (unsigned)seq, jsp_h264_frame_kind(&h264_meta), (unsigned)jpeg_len,
			  h264_meta.has_sps ? " +sps" : "");
	 }
	 else if (jpeg_ok) {
		 const char *sampling = jsp_infer_sampling(&meta);
		 int need_log = (seq <= JSP_DIAG_LOG_FIRST_N) || ((seq % JSP_DIAG_LOG_EVERY_N) == 0U) ||
					(meta.width != ctx->cfg.src_width) || (meta.height != ctx->cfg.src_height);
 
		 if (need_log) {
			 LOGI("[diag] in#%u jpg=%ux%u sof=0x%02x samp=%s Y(%ux%u) Cb(%ux%u) Cr(%ux%u) cfg=%ux%u dst=%ux%u rot=%u cpr=%d scl=%d len=%u\n",
				  (unsigned)seq,
				  (unsigned)meta.width, (unsigned)meta.height, (unsigned)meta.sof_marker, sampling,
				  (unsigned)meta.h[0], (unsigned)meta.v[0],
				  (unsigned)meta.h[1], (unsigned)meta.v[1],
				  (unsigned)meta.h[2], (unsigned)meta.v[2],
				  (unsigned)ctx->cfg.src_width, (unsigned)ctx->cfg.src_height,
				  (unsigned)ctx->cfg.dst_width, (unsigned)ctx->cfg.dst_height,
				  (unsigned)ctx->cfg.rotate_degree, (int)ctx->cfg.compress, (int)ctx->cfg.scale,
				  (unsigned)jpeg_len);
		 }
		 if (meta.width != ctx->cfg.src_width || meta.height != ctx->cfg.src_height) {
			 LOGE("[diag] in#%u size mismatch jpg=%ux%u cfg=%ux%u\n",
				  (unsigned)seq,
				  (unsigned)meta.width, (unsigned)meta.height,
				  (unsigned)ctx->cfg.src_width, (unsigned)ctx->cfg.src_height);
		 }
	 }
	 else if (seq <= JSP_DIAG_LOG_FIRST_N || (seq % JSP_DIAG_LOG_EVERY_N) == 0U) {
		 LOGW("[diag] in#%u parse_jpeg_meta failed len=%u\n", (unsigned)seq, (unsigned)jpeg_len);
	 }
 
	 LOGPF("[cast] push#%u q+%u\n", (unsigned)seq, (unsigned)jpeg_len);
	 return AVDK_ERR_OK;
 }
 
 avdk_err_t jpeg_stream_pipeline_stop(jpeg_stream_pipeline_handle_t handle)
 {
	 struct jpeg_stream_pipeline_ctx *ctx = handle;
 
	 if (ctx == NULL)
		 return AVDK_ERR_INVAL;
	 if (!ctx->running)
		 return AVDK_ERR_OK;
 
	 ctx->running = 0;
 
	 /* Wait for decode task to exit */
	 rtos_get_semaphore(&ctx->task_sem, BEKEN_WAIT_FOREVER);
	 rtos_deinit_semaphore(&ctx->task_sem);
	 ctx->task_handle = NULL;
 
	 /* Drain remaining frames */
	 jsp_frame_entry_t entry;
	 while (rtos_pop_from_queue(&ctx->frame_queue, &entry, BEKEN_NO_WAIT) == BK_OK)
	 {
		 if (ctx->cfg.frame_consumed_cb != NULL)
			 ctx->cfg.frame_consumed_cb(entry.jpeg_stream, AVDK_ERR_SHUTDOWN, entry.jpeg_owner,
										ctx->cfg.user_data);
	 }
 
	 LOGI("pipeline stopped\n");
	 return AVDK_ERR_OK;
 }
 
 avdk_err_t jpeg_stream_pipeline_destroy(jpeg_stream_pipeline_handle_t handle)
 {
	 struct jpeg_stream_pipeline_ctx *ctx = handle;
 
	 if (ctx == NULL)
		 return AVDK_ERR_INVAL;
 
	 if (ctx->running)
	 {
		 LOGW("destroying running pipeline, forcing stop\n");
		 (void)jpeg_stream_pipeline_stop(handle);
	 }
 
	 /* Tear down bond */
	 if (ctx->h264_bond != NULL)
	 {
		 bk_flexa_h264d_gpu_bond_stop(ctx->h264_bond);
		 ctx->h264_bond = NULL;
	 }
	 if (ctx->bond != NULL)
	 {
		 bk_flexa_mjpegd_gpu_bond_stop(ctx->bond);
		 ctx->bond = NULL;
	 }
 
	 /* Close GPU.
	  *
	  * bk_gpu_deinit() -> gpu_ctlr_deinit() calls vg_lite_close() + bk_gpu_driver_deinit()
	  * on the *single global* GPU engine (not reference counted). If LVGL shares that
	  * engine (screencast), the caller MUST re-acquire it (lv_gpu_init) before letting
	  * LVGL draw again -- that is now the display layer's job (display_ui_cast_hooks
	  * resume), so this component stays LVGL-agnostic and just does a plain teardown. */
	 if (ctx->gpu_handle != NULL)
	 {
		 (void)bk_gpu_close(ctx->gpu_handle);
		 (void)bk_gpu_deinit(ctx->gpu_handle);
		 (void)bk_gpu_delete(ctx->gpu_handle);
		 ctx->gpu_handle = NULL;
	 }
 
	 /* Close JPEG decoder */
	 if (ctx->jpeg_handle != NULL)
	 {
		 (void)bk_jpeg_decode_close(ctx->jpeg_handle);
		 (void)bk_jpeg_decode_deinit(ctx->jpeg_handle);
		 (void)bk_jpeg_decode_delete(ctx->jpeg_handle);
		 ctx->jpeg_handle = NULL;
	 }
 
	 /* Close H264 decoder */
	 if (ctx->h264_handle != NULL)
	 {
		 (void)bk_h264_decode_close(ctx->h264_handle);
		 (void)bk_h264_decode_deinit(ctx->h264_handle);
		 (void)bk_h264_decode_delete(ctx->h264_handle);
		 ctx->h264_handle = NULL;
	 }
 
	 /* Free frame queue */
	 if (ctx->frame_queue != NULL)
		 rtos_deinit_queue(&ctx->frame_queue);
 
	 /* Free FLEXA ring buffer */
	 if (ctx->ring_buf != NULL)
	 {
		 jsp_psram_aligned_free(ctx->ring_buf);
		 ctx->ring_buf = NULL;
	 }
 
	 os_free(ctx);
	 LOGI("pipeline destroyed\n");
	 return AVDK_ERR_OK;
 }
 