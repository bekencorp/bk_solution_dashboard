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
 * Description: Decode Stream Pipeline - codec-agnostic core.
 *
 * The core owns the frame queue, the decode task, the GPU controller and the
 * FLEXA ring buffer. Each codec (JPEG, H264) lives behind a dsp_codec_ops
 * backend table implemented in decode_stream_pipeline_{jpeg,h264}.c; the
 * context keeps exactly one active backend + one active FLEXA bond. The
 * initial backend is selected by decode_stream_pipeline_config_t::decode_type
 * (eager setup); the other codec's decoder is created lazily on the first
 * frame of that type, and frames are re-classified on every push so the
 * backend switches (both directions) automatically.
 *
 * Internal flow:
 *   create:  FLEXA ring in PSRAM (BK7259 VCDEC FLEXA DMA; HSRAM ring caused
 *            MemFault in field). Keeps HSRAM for bk_get_gpu_flexa_buffer
 *            inside bk_gpu_init. The decoder of the configured decode_type
 *            is brought up eagerly; the other one lazily on first use.
 *   start:   spawn decode task thread; (re)run backend activation policy
 *            (H264: hold frames until first IDR)
 *   frame:   decode task pops queue -> backend decode (codec_lock held) ->
 *            VCDEC FLEXA strip output -> bond auto-notifies GPU -> GPU
 *            strip-parallel processing -> frame_display callback
 *   stop:    terminate decode task
 *   destroy: active bond stop -> GPU close/deinit/delete -> both decoders
 *            close/deinit/delete -> free ring -> free ctx
 *
 * Reference:
 *   doorbell: app_jpeg_decode.c + app_gpu.c (app_gpu_v2_turn_on)
 *   SDK:      bk_gpu_ctlr_default.c, bk_flexa_mjpeg_bond.c
 */

#include "decode_stream_pipeline_internal.h"

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */
#define DSP_FLEXA_LINES_DEFAULT       16U
#define DSP_FLEXA_BUFF_CNT_DEFAULT    3U
#define DSP_DECODE_TIMEOUT_DEFAULT    1000U   /* ms */
#define DSP_TASK_STACK_DEFAULT        (4U * 1024U)
#define DSP_TASK_PRIORITY_DEFAULT     BEKEN_DEFAULT_WORKER_PRIORITY
#define DSP_QUEUE_DEPTH_DEFAULT       1U
/* H264 cannot drop arbitrary frames, so it needs real queue slots to ride out
 * decode jitter. The queue is allocated for the worst case up front (it cannot
 * grow at runtime) because even a JPEG-typed pipeline may lazy-switch to H264;
 * JPEG is unaffected: it drains the queue before every push, so the extra
 * slots never hold more than the one frame just pushed. */
#define DSP_QUEUE_DEPTH_H264_MIN      4U
#define DSP_RING_ALIGNMENT            64U
#define DSP_QUEUE_POP_TIMEOUT_MS      50U
#define DSP_DIAG_LOG_FIRST_N          10U
#define DSP_DIAG_LOG_EVERY_N          120U

/* ------------------------------------------------------------------ */
/* PSRAM FLEXA ring: VCDEC FLEXA path matches SDK jpeg_decoder_flexa_test (psram_malloc). */
/* ------------------------------------------------------------------ */
static void *dsp_psram_aligned_alloc(uint32_t alignment, uint32_t size, void **out_raw)
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

static void dsp_psram_aligned_free(void *ptr)
{
	if (ptr != NULL)
		psram_free(((void **)ptr)[-1]);
}

/* ------------------------------------------------------------------ */
/* GPU frame_done internal forwarding                                  */
/* Forwards the just-produced GPU output frame to the pipeline's       */
/* high-level frame_display_cb consumer.                               */
/* ------------------------------------------------------------------ */
static void dsp_gpu_frame_done(void *frame, uint32_t frame_size, void *args)
{
	struct decode_stream_pipeline_ctx *ctx = (struct decode_stream_pipeline_ctx *)args;
	if (ctx != NULL && ctx->cfg.frame_display_cb != NULL)
		ctx->cfg.frame_display_cb(frame, frame_size, ctx->cfg.user_data);
}

/* ------------------------------------------------------------------ */
/* Shared services used by the backends                                */
/* ------------------------------------------------------------------ */
/* Decoder frame_done callback (shared by both backends) */
void dsp_decoder_frame_done(int status, void *args)
{
	(void)args;
	if (status != 0)
		LOGW("decode frame done status=%d\n", status);
}

/* Pop and release everything still queued. Returns how many frames were dropped. */
uint32_t dsp_drain_queue(struct decode_stream_pipeline_ctx *ctx)
{
	dsp_frame_entry_t stale;
	uint32_t dropped = 0;

	while (rtos_pop_from_queue(&ctx->frame_queue, &stale, BEKEN_NO_WAIT) == BK_OK)
	{
		dropped++;
		if (ctx->cfg.frame_consumed_cb != NULL)
			ctx->cfg.frame_consumed_cb(stale.stream, DSP_STATUS_FRAME_DROPPED,
						   stale.owner, ctx->cfg.user_data);
	}

	return dropped;
}

/* Low-latency stale drops are expected during casting; aggregate to avoid
 * per-frame log flood while keeping the drop rate observable. */
void dsp_report_stale_drops(uint32_t dropped)
{
	static uint32_t s_stale_drop_total    = 0;
	static uint32_t s_stale_drop_reported = 0;

	if (dropped == 0U)
		return;

	s_stale_drop_total += dropped;
	if (s_stale_drop_total - s_stale_drop_reported >= 128U)
	{
		LOGW("[cast] dropped %u stale frames for low-latency (total %u)\n",
		     (unsigned)(s_stale_drop_total - s_stale_drop_reported),
		     (unsigned)s_stale_drop_total);
		s_stale_drop_reported = s_stale_drop_total;
	}
}

/* ------------------------------------------------------------------ */
/* Codec switching                                                     */
/* ------------------------------------------------------------------ */
/* The other backend (two codecs today; extend to a list if a third one
 * ever lands). */
static const dsp_codec_ops_t *dsp_other_backend(const struct decode_stream_pipeline_ctx *ctx)
{
	return (ctx->ops == &dsp_jpeg_ops) ? &dsp_h264_ops : &dsp_jpeg_ops;
}

/*
 * Switch the active codec backend. No-op if already active. The queue is
 * drained (owners notified via frame_consumed_cb) so no stale frame of the
 * old codec rides the new bond. On failure the previous backend is restored;
 * if that fails too the pipeline is left bond-less and pushes keep failing
 * loudly until a later switch succeeds.
 *
 * Runs under ctx->codec_lock: the decode task holds the same lock for the
 * duration of decode_frame, so once we acquire it the old codec's decoder is
 * idle and its bond can be torn down safely.
 */
static avdk_err_t dsp_activate_backend(struct decode_stream_pipeline_ctx *ctx,
					const dsp_codec_ops_t *ops)
{
	avdk_err_t ret;

	if (ctx->ops == ops)
		return AVDK_ERR_OK;

	LOGI("codec switch: %s -> %s\n", ctx->ops->name, ops->name);

	rtos_lock_mutex(&ctx->codec_lock);

	/* Frames of the old codec must not ride the new bond. */
	(void)dsp_drain_queue(ctx);

	/* Disconnect the old codec before wiring the new one. */
	ctx->ops->bond_stop(ctx);

	ret = ops->decoder_setup(ctx);
	if (ret != AVDK_ERR_OK)
		goto err_revert;

	ret = ops->bond_start(ctx);
	if (ret != AVDK_ERR_OK)
		goto err_revert;

	ctx->ops = ops;
	if (ops->on_activate != NULL)
		ops->on_activate(ctx);

	rtos_unlock_mutex(&ctx->codec_lock);
	return AVDK_ERR_OK;

err_revert:
	LOGE("activate %s backend failed: %d, reverting to %s\n",
	     ops->name, (int)ret, ctx->ops->name);
	if (ctx->ops->bond_start(ctx) != AVDK_ERR_OK)
		LOGE("revert to %s bond failed, pipeline idle\n", ctx->ops->name);
	rtos_unlock_mutex(&ctx->codec_lock);
	return ret;
}

/* ------------------------------------------------------------------ */
/* Decode task                                                         */
/* ------------------------------------------------------------------ */
static void dsp_decode_task(void *arg)
{
	struct decode_stream_pipeline_ctx *ctx = (struct decode_stream_pipeline_ctx *)arg;
	dsp_frame_entry_t entry;
	avdk_err_t ret;

	LOGI("decode task started\n");
	rtos_set_semaphore(&ctx->task_sem);

	while (ctx->running)
	{
		ret = rtos_pop_from_queue(&ctx->frame_queue, &entry, DSP_QUEUE_POP_TIMEOUT_MS);
		if (ret != BK_OK)
			continue;

		if (!ctx->running)
			break;

		if (entry.stream == NULL || entry.len == 0)
		{
			LOGW("empty frame, skip\n");
			continue;
		}

		ctx->dec_seq++;

		/* Dispatch on the backend snapshot taken at push time: the
		 * producer may switch codecs while this frame sits in the queue
		 * (switching drains the queue first and holds codec_lock, so a
		 * stale frame never rides the new bond and never races the
		 * switch itself). */
		rtos_lock_mutex(&ctx->codec_lock);
		ret = entry.ops->decode_frame(ctx, &entry);
		rtos_unlock_mutex(&ctx->codec_lock);

		if (ret != AVDK_ERR_OK)
			LOGE("decode frame failed (%s): %d\n", entry.ops->name, (int)ret);

		LOGPF("[cast] dec#%u len=%u ret=%d\n",
		      (unsigned)ctx->dec_seq, (unsigned)entry.len, (int)ret);

		/* Notify caller that the stream has been consumed */
		if (ctx->cfg.frame_consumed_cb != NULL)
			ctx->cfg.frame_consumed_cb(entry.stream, (int)ret, entry.owner,
						   ctx->cfg.user_data);
	}

	LOGI("decode task exiting\n");
	rtos_set_semaphore(&ctx->task_sem);
	rtos_delete_thread(NULL);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

avdk_err_t decode_stream_pipeline_create(decode_stream_pipeline_handle_t *handle,
					 const decode_stream_pipeline_config_t *config)
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
	struct decode_stream_pipeline_ctx *ctx =
		(struct decode_stream_pipeline_ctx *)os_malloc(sizeof(*ctx));
	if (ctx == NULL)
		return AVDK_ERR_NOMEM;
	os_memset(ctx, 0, sizeof(*ctx));

	/* Deep copy config and fill defaults */
	os_memcpy(&ctx->cfg, config, sizeof(*config));
	if (ctx->cfg.flexa_lines      == 0) ctx->cfg.flexa_lines      = DSP_FLEXA_LINES_DEFAULT;
	if (ctx->cfg.flexa_buff_cnt   == 0) ctx->cfg.flexa_buff_cnt   = DSP_FLEXA_BUFF_CNT_DEFAULT;
	if (ctx->cfg.decode_timeout_ms == 0) ctx->cfg.decode_timeout_ms = DSP_DECODE_TIMEOUT_DEFAULT;
	if (ctx->cfg.task_stack_size   == 0) ctx->cfg.task_stack_size   = DSP_TASK_STACK_DEFAULT;
	if (ctx->cfg.task_priority     == 0) ctx->cfg.task_priority     = DSP_TASK_PRIORITY_DEFAULT;
	if (ctx->cfg.queue_depth       == 0) ctx->cfg.queue_depth       = DSP_QUEUE_DEPTH_DEFAULT;

	/* ---- Step 1: FLEXA ring buffer ---- */
	uint16_t aligned_w = (config->src_width + 15U) & ~15U;
	ctx->ring_size = bk_image_size_get(aligned_w,
					   (uint16_t)(ctx->cfg.flexa_lines * ctx->cfg.flexa_buff_cnt),
					   BK_PIXEL_FORMAT_NV12);

	ctx->ring_buf = (uint8_t *)dsp_psram_aligned_alloc(DSP_RING_ALIGNMENT,
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

	/* ---- Step 2: initial codec backend (selected by decode_type; the
	 * other codec's decoder is created lazily on first switch). ---- */
	ctx->ops = (config->decode_type == DECODE_STREAM_TYPE_H264) ? &dsp_h264_ops
								    : &dsp_jpeg_ops;
	ret = ctx->ops->decoder_setup(ctx);
	if (ret != AVDK_ERR_OK)
		goto err_free_ring;

	/* ---- Step 3: GPU controller (codec-agnostic) ---- */
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
		gpu_cfg.frame_done           = dsp_gpu_frame_done;
		gpu_cfg.frame_done_args      = ctx;

		ret = bk_gpu_ctlr_new(&ctx->gpu_handle, &gpu_cfg);
		if (ret != AVDK_ERR_OK)
		{
			LOGE("bk_gpu_ctlr_new failed: %d\n", (int)ret);
			goto err_teardown_decoder;
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

	/* ---- Step 4: FLEXA bond (initial backend) ---- */
	ret = ctx->ops->bond_start(ctx);
	if (ret != AVDK_ERR_OK)
	{
		LOGE("bond start failed: %d\n", (int)ret);
		goto err_gpu_teardown;
	}

	/* ---- Step 5: Frame queue + codec lock ---- */
	{
		/* Allocated for the worst case (H264); the JPEG admission policy
		 * drains before every push, so extra slots never hold stale
		 * JPEG frames. */
		uint32_t queue_depth = ctx->cfg.queue_depth;

		if (queue_depth < DSP_QUEUE_DEPTH_H264_MIN)
			queue_depth = DSP_QUEUE_DEPTH_H264_MIN;

		ret = (avdk_err_t)rtos_init_queue(&ctx->frame_queue,
						  "dsp_q",
						  sizeof(dsp_frame_entry_t),
						  queue_depth);
		if (ret != BK_OK)
		{
			LOGE("queue init failed: %d\n", (int)ret);
			goto err_stop_bond;
		}
	}

	if (rtos_init_mutex(&ctx->codec_lock) != BK_OK)
	{
		LOGE("codec lock init failed\n");
		ret = AVDK_ERR_GENERIC;
		goto err_deinit_queue;
	}

	*handle = ctx;
	LOGI("pipeline created: codec=%s src=%ux%u dst=%ux%u rot=%u compress=%d\n",
	     ctx->ops->name,
	     config->src_width, config->src_height,
	     config->dst_width, config->dst_height,
	     config->rotate_degree, config->compress);
	return AVDK_ERR_OK;

	/* ---- Error rollback ---- */
err_deinit_queue:
	rtos_deinit_queue(&ctx->frame_queue);
err_stop_bond:
	ctx->ops->bond_stop(ctx);
err_gpu_teardown:
	(void)bk_gpu_close(ctx->gpu_handle);
err_deinit_gpu:
	(void)bk_gpu_deinit(ctx->gpu_handle);
err_delete_gpu:
	(void)bk_gpu_delete(ctx->gpu_handle);
	ctx->gpu_handle = NULL;
err_teardown_decoder:
	ctx->ops->decoder_teardown(ctx);
err_free_ring:
	dsp_psram_aligned_free(ctx->ring_buf);
	ctx->ring_buf = NULL;
err_free_ctx:
	os_free(ctx);
	return ret;
}

avdk_err_t decode_stream_pipeline_start(decode_stream_pipeline_handle_t handle)
{
	struct decode_stream_pipeline_ctx *ctx = handle;
	avdk_err_t ret;

	if (ctx == NULL)
		return AVDK_ERR_INVAL;
	if (ctx->running)
		return AVDK_ERR_OK;

	ret = (avdk_err_t)rtos_init_semaphore(&ctx->task_sem, 1);
	if (ret != BK_OK)
		return ret;

	ctx->running = 1;

	/* (Re)run the backend activation policy for the new session (H264:
	 * hold frames until the first IDR). */
	if (ctx->ops->on_activate != NULL)
		ctx->ops->on_activate(ctx);

	ret = (avdk_err_t)rtos_create_thread(&ctx->task_handle,
					     ctx->cfg.task_priority,
					     "dsp_dec",
					     (beken_thread_function_t)dsp_decode_task,
					     ctx->cfg.task_stack_size,
					     ctx);
	if (ret != BK_OK)
	{
		LOGE("create decode task failed: %d\n", (int)ret);
		ctx->running = 0;
		rtos_deinit_semaphore(&ctx->task_sem);
		return ret;
	}

	/* Wait for task ready */
	rtos_get_semaphore(&ctx->task_sem, BEKEN_WAIT_FOREVER);

	LOGI("pipeline started\n");
	return AVDK_ERR_OK;
}

avdk_err_t decode_stream_pipeline_push_frame(decode_stream_pipeline_handle_t handle,
					     const uint8_t *stream,
					     uint32_t len,
					     void *owner)
{
	struct decode_stream_pipeline_ctx *ctx = handle;
	dsp_frame_meta_t meta;
	const dsp_codec_ops_t *target;
	avdk_err_t admit;
	uint32_t seq;
	int classified;

	if (ctx == NULL || stream == NULL || len == 0)
		return AVDK_ERR_INVAL;
	if (!ctx->running)
		return AVDK_ERR_GENERIC;

	seq = ++ctx->push_seq;

	/* ---- Classify: try the active codec first, then the other one. A
	 * frame matching neither keeps the active backend (best-effort
	 * decode, same as before). ---- */
	classified = ctx->ops->parse_meta(stream, len, &meta);
	target = ctx->ops;
	if (!classified)
	{
		const dsp_codec_ops_t *other = dsp_other_backend(ctx);

		classified = other->parse_meta(stream, len, &meta);
		if (classified)
			target = other;
		else
			os_memset(&meta, 0, sizeof(meta));
	}

	/* ---- Codec switch must happen before the frame is visible to the
	 * decode task, otherwise the task can pick it up while the old bond
	 * is still active. ---- */
	if (target != ctx->ops)
	{
		avdk_err_t sw = dsp_activate_backend(ctx, target);

		if (sw != AVDK_ERR_OK)
			return sw;
	}

	/* ---- Admission: per-backend policy (JPEG latest-wins drain, H264
	 * GOP-aware drop). ---- */
	admit = ctx->ops->admit_frame(ctx, &meta);
	if (admit != AVDK_ERR_OK)
		return admit;

	dsp_frame_entry_t entry = {
		.stream = stream,
		.len    = len,
		.owner  = owner,
		.ops    = ctx->ops,
	};

	bk_err_t ret = rtos_push_to_queue(&ctx->frame_queue, &entry, BEKEN_NO_WAIT);
	if (ret != BK_OK)
	{
		ctx->ops->on_push_full(ctx, seq, &meta, len);
		return AVDK_ERR_BUSY;
	}

	/* ---- Diagnostics ---- */
	if (classified)
		ctx->ops->diag_frame(ctx, seq, &meta, len);
	else if (seq <= DSP_DIAG_LOG_FIRST_N || (seq % DSP_DIAG_LOG_EVERY_N) == 0U)
		LOGW("[diag] in#%u unknown frame format len=%u\n",
		     (unsigned)seq, (unsigned)len);

	LOGPF("[cast] push#%u q+%u\n", (unsigned)seq, (unsigned)len);
	return AVDK_ERR_OK;
}

avdk_err_t decode_stream_pipeline_stop(decode_stream_pipeline_handle_t handle)
{
	struct decode_stream_pipeline_ctx *ctx = handle;

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
	dsp_frame_entry_t entry;
	while (rtos_pop_from_queue(&ctx->frame_queue, &entry, BEKEN_NO_WAIT) == BK_OK)
	{
		if (ctx->cfg.frame_consumed_cb != NULL)
			ctx->cfg.frame_consumed_cb(entry.stream, AVDK_ERR_SHUTDOWN, entry.owner,
						   ctx->cfg.user_data);
	}

	LOGI("pipeline stopped\n");
	return AVDK_ERR_OK;
}

avdk_err_t decode_stream_pipeline_destroy(decode_stream_pipeline_handle_t handle)
{
	struct decode_stream_pipeline_ctx *ctx = handle;

	if (ctx == NULL)
		return AVDK_ERR_INVAL;

	if (ctx->running)
	{
		LOGW("destroying running pipeline, forcing stop\n");
		(void)decode_stream_pipeline_stop(handle);
	}

	/* Tear down the active bond (the inactive backend owns no bond). */
	if (ctx->ops != NULL)
		ctx->ops->bond_stop(ctx);

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

	/* Close both decoders; each teardown is a no-op unless that codec was
	 * ever activated (lazy creation). */
	dsp_jpeg_ops.decoder_teardown(ctx);
	dsp_h264_ops.decoder_teardown(ctx);

	/* Free codec lock + frame queue */
	if (ctx->codec_lock != NULL)
		rtos_deinit_mutex(&ctx->codec_lock);
	if (ctx->frame_queue != NULL)
		rtos_deinit_queue(&ctx->frame_queue);

	/* Free FLEXA ring buffer */
	if (ctx->ring_buf != NULL)
	{
		dsp_psram_aligned_free(ctx->ring_buf);
		ctx->ring_buf = NULL;
	}

	os_free(ctx);
	LOGI("pipeline destroyed\n");
	return AVDK_ERR_OK;
}
