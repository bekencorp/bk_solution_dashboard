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
 #include <components/log.h>
 #include <components/bk_decode/bk_jpeg_decode_ctlr.h>
 #include <components/bk_decode/bk_jpeg_decode_types.h>
 #include <components/bk_gpu_ctlr.h>
 #include <components/bk_gpu.h>
 #include <components/bk_flexa_bond.h>
 
 #define TAG "jsp"
 
 #define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
 #define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
 #define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
 
 /* ------------------------------------------------------------------ */
 /* Defaults                                                            */
 /* ------------------------------------------------------------------ */
 #define JSP_FLEXA_LINES_DEFAULT       16U
 #define JSP_FLEXA_BUFF_CNT_DEFAULT    2U
 #define JSP_DECODE_TIMEOUT_DEFAULT    1000U   /* ms */
 #define JSP_TASK_STACK_DEFAULT        (4U * 1024U)
 #define JSP_TASK_PRIORITY_DEFAULT     BEKEN_DEFAULT_WORKER_PRIORITY
 #define JSP_QUEUE_DEPTH_DEFAULT       1U
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
 
 /* ------------------------------------------------------------------ */
 /* Internal context (actual type behind the opaque handle)             */
 /* ------------------------------------------------------------------ */
 struct jpeg_stream_pipeline_ctx {
	 jpeg_stream_pipeline_config_t cfg;
 
	 /* Hardware handles */
	 bk_jpeg_decode_ctlr_handle_t  jpeg_handle;
	 bk_gpu_ctlr_handle_t          gpu_handle;
	 void                         *bond;
 
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
 
		 bk_jpeg_decode_input_t input;
		 os_memset(&input, 0, sizeof(input));
		 input.stream          = (uint8_t *)entry.jpeg_stream;
		 input.stream_len      = entry.jpeg_len;
		 input.out_buffer      = ctx->ring_buf;
		 input.out_buffer_size = ctx->ring_size;
 
		 ctx->dec_seq++;
		 ret = bk_jpeg_decode_frame(ctx->jpeg_handle, &input);
		 LOGI("[cast] dec#%u len=%u ret=%d\n",
			  (unsigned)ctx->dec_seq, (unsigned)entry.jpeg_len, (int)ret);
 
		 /* Notify caller that the stream has been consumed */
		 if (ctx->cfg.frame_consumed_cb != NULL)
			 ctx->cfg.frame_consumed_cb(entry.jpeg_stream, (int)ret, entry.jpeg_owner,
										ctx->cfg.user_data);
 
		 if (ret != AVDK_ERR_OK)
			 LOGE("bk_jpeg_decode_frame failed: %d\n", (int)ret);
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
	 ctx->ring_size = (uint32_t)aligned_w
					  * ctx->cfg.flexa_lines
					  * 3U / 2U
					  * ctx->cfg.flexa_buff_cnt;
 
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
		 jpeg_cfg.out_width      = config->dst_width;
		 jpeg_cfg.out_height     = config->dst_height;
		 jpeg_cfg.out_format     = config->dst_format;
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
			 goto err_close_jpeg;
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
	 ret = rtos_init_queue(&ctx->frame_queue,
						   "jsp_q",
						   sizeof(jsp_frame_entry_t),
						   ctx->cfg.queue_depth);
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
 
 avdk_err_t jpeg_stream_pipeline_push_frame(jpeg_stream_pipeline_handle_t handle,
											   const uint8_t *jpeg_stream,
											   uint32_t jpeg_len,
											   void *jpeg_owner)
 {
	 struct jpeg_stream_pipeline_ctx *ctx = handle;
	 jsp_jpeg_meta_t meta;
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

	 /* Drop stale queued frames so the display always shows the latest content. */
	 {
		 jsp_frame_entry_t stale;
		 uint32_t dropped = 0;
		 while (rtos_pop_from_queue(&ctx->frame_queue, &stale, BEKEN_NO_WAIT) == BK_OK) {
			 dropped++;
			 if (ctx->cfg.frame_consumed_cb != NULL)
				 ctx->cfg.frame_consumed_cb(stale.jpeg_stream, JSP_STATUS_FRAME_DROPPED,
											stale.jpeg_owner, ctx->cfg.user_data);
		 }
		 if (dropped > 0)
			 LOGW("[cast] drop %u stale frame(s) for low-latency\n", (unsigned)dropped);
	 }

	 bk_err_t ret = rtos_push_to_queue(&ctx->frame_queue, &entry, BEKEN_NO_WAIT);
	 if (ret != BK_OK)
	 {
		 LOGW("[cast] push qfull %u\n", (unsigned)jpeg_len);
		 return AVDK_ERR_BUSY;
	 }
 
	 if (jsp_parse_jpeg_meta(jpeg_stream, jpeg_len, &meta)) {
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
	 } else if (seq <= JSP_DIAG_LOG_FIRST_N || (seq % JSP_DIAG_LOG_EVERY_N) == 0U) {
		 LOGW("[diag] in#%u parse_jpeg_meta failed len=%u\n", (unsigned)seq, (unsigned)jpeg_len);
	 }
 
	 LOGI("[cast] push#%u q+%u\n", (unsigned)seq, (unsigned)jpeg_len);
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
	 if (ctx->bond != NULL)
	 {
		 bk_flexa_mjpegd_gpu_bond_stop(ctx->bond);
		 ctx->bond = NULL;
	 }
 
	 /* Close GPU */
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
 