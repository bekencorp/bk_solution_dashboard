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
 * Description: Decode Stream Pipeline - JPEG backend.
 *
 * Owns everything JPEG-specific: the VCDEC JPEG decoder lifecycle, the
 * mjpegd FLEXA bond, SOF0/1/2 header parsing for admission/diagnostics, and
 * the latest-wins admission policy (JPEG frames are independent, so queued
 * older frames are drained to keep display latency low).
 */

#include "decode_stream_pipeline_internal.h"

/* ------------------------------------------------------------------ */
/* JPEG metadata parsing                                               */
/* ------------------------------------------------------------------ */
static uint16_t dsp_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static const char *dsp_jpeg_infer_sampling(const dsp_jpeg_meta_t *m)
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

/* Scan the SOF segment for size/sampling info. Returns 1 when a usable SOF
 * was found, 0 when the stream does not look like JPEG (or is truncated). */
static int dsp_parse_jpeg_meta(const uint8_t *buf, uint32_t len, dsp_jpeg_meta_t *m)
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

		seg_len = dsp_be16(&buf[i + 1U]);
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
			m->height = dsp_be16(&buf[seg_start + 1U]);
			m->width = dsp_be16(&buf[seg_start + 3U]);
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
/* Decoder lifecycle                                                   */
/* ------------------------------------------------------------------ */
static avdk_err_t dsp_jpeg_decoder_setup(struct decode_stream_pipeline_ctx *ctx)
{
	bk_jpeg_decode_flexa_config_t jpeg_cfg;
	avdk_err_t ret;

	if (ctx->jpeg.handle != NULL)
		return AVDK_ERR_OK; /* already alive */

	os_memset(&jpeg_cfg, 0, sizeof(jpeg_cfg));
	jpeg_cfg.frame_done_cb  = dsp_decoder_frame_done;
	jpeg_cfg.flexa_done_cb  = NULL; /* Injected by bond */
	jpeg_cfg.timeout_ms     = ctx->cfg.decode_timeout_ms;
	jpeg_cfg.out_width      = ctx->cfg.src_width;
	jpeg_cfg.out_height     = ctx->cfg.src_height;
	jpeg_cfg.out_format     = BK_PIXEL_FORMAT_NV12;
	jpeg_cfg.segment_height = (uint16_t)(ctx->cfg.flexa_lines / 16U);
	jpeg_cfg.segment_number = ctx->cfg.flexa_buff_cnt;

	ret = bk_jpeg_decode_flexa_ctlr_new(&ctx->jpeg.handle, &jpeg_cfg);
	if (ret != AVDK_ERR_OK) {
		LOGE("bk_jpeg_decode_new failed: %d\n", (int)ret);
		return ret;
	}

	ret = bk_jpeg_decode_init(ctx->jpeg.handle);
	if (ret != AVDK_ERR_OK) {
		LOGE("bk_jpeg_decode_init failed: %d\n", (int)ret);
		goto err_delete;
	}

	ret = bk_jpeg_decode_open(ctx->jpeg.handle);
	if (ret != AVDK_ERR_OK) {
		LOGE("bk_jpeg_decode_open failed: %d\n", (int)ret);
		goto err_deinit;
	}

	return AVDK_ERR_OK;

err_deinit:
	(void)bk_jpeg_decode_deinit(ctx->jpeg.handle);
err_delete:
	(void)bk_jpeg_decode_delete(ctx->jpeg.handle);
	ctx->jpeg.handle = NULL;
	return ret;
}

static void dsp_jpeg_decoder_teardown(struct decode_stream_pipeline_ctx *ctx)
{
	if (ctx->jpeg.handle == NULL)
		return;
	(void)bk_jpeg_decode_close(ctx->jpeg.handle);
	(void)bk_jpeg_decode_deinit(ctx->jpeg.handle);
	(void)bk_jpeg_decode_delete(ctx->jpeg.handle);
	ctx->jpeg.handle = NULL;
}

/* ------------------------------------------------------------------ */
/* FLEXA bond                                                          */
/* ------------------------------------------------------------------ */
static avdk_err_t dsp_jpeg_bond_start(struct decode_stream_pipeline_ctx *ctx)
{
	return bk_flexa_mjpegd_gpu_bond_start(&ctx->bond,
					      ctx->jpeg.handle,
					      ctx->gpu_handle);
}

static void dsp_jpeg_bond_stop(struct decode_stream_pipeline_ctx *ctx)
{
	if (ctx->bond == NULL)
		return;
	bk_flexa_mjpegd_gpu_bond_stop(ctx->bond);
	ctx->bond = NULL;
}

/* ------------------------------------------------------------------ */
/* Decode                                                              */
/* ------------------------------------------------------------------ */
static avdk_err_t dsp_jpeg_decode_frame(struct decode_stream_pipeline_ctx *ctx,
					const dsp_frame_entry_t *entry)
{
	bk_jpeg_decode_input_t input;

	if (ctx->jpeg.handle == NULL)
		return AVDK_ERR_INVAL;

	os_memset(&input, 0, sizeof(input));
	input.stream          = (uint8_t *)entry->stream;
	input.stream_len      = entry->len;
	input.out_buffer      = ctx->ring_buf;
	input.out_buffer_size = ctx->ring_size;

	return bk_jpeg_decode_frame(ctx->jpeg.handle, &input);
}

/* ------------------------------------------------------------------ */
/* Admission (latest-wins)                                             */
/* ------------------------------------------------------------------ */
/* JPEG frames are independent: keep only the newest so the display never
 * trails the source. */
static avdk_err_t dsp_jpeg_admit_frame(struct decode_stream_pipeline_ctx *ctx,
					const dsp_frame_meta_t *meta)
{
	(void)meta;
	dsp_report_stale_drops(dsp_drain_queue(ctx));
	return AVDK_ERR_OK;
}

static void dsp_jpeg_on_push_full(struct decode_stream_pipeline_ctx *ctx, uint32_t seq,
				  const dsp_frame_meta_t *meta, uint32_t len)
{
	(void)ctx;
	(void)seq;
	(void)meta;
	LOGW("[cast] push qfull %u\n", (unsigned)len);
}

/* ------------------------------------------------------------------ */
/* Classification + diagnostics                                        */
/* ------------------------------------------------------------------ */
static int dsp_jpeg_parse_meta(const uint8_t *buf, uint32_t len, dsp_frame_meta_t *meta)
{
	return dsp_parse_jpeg_meta(buf, len, &meta->jpeg);
}

#define DSP_DIAG_LOG_FIRST_N   10U
#define DSP_DIAG_LOG_EVERY_N   120U

static void dsp_jpeg_diag_frame(struct decode_stream_pipeline_ctx *ctx, uint32_t seq,
				const dsp_frame_meta_t *meta, uint32_t len)
{
	const dsp_jpeg_meta_t *m = &meta->jpeg;
	const char *sampling = dsp_jpeg_infer_sampling(m);
	int need_log = (seq <= DSP_DIAG_LOG_FIRST_N) || ((seq % DSP_DIAG_LOG_EVERY_N) == 0U) ||
		       (m->width != ctx->cfg.src_width) || (m->height != ctx->cfg.src_height);

	if (need_log) {
		LOGI("[diag] in#%u jpg=%ux%u sof=0x%02x samp=%s Y(%ux%u) Cb(%ux%u) Cr(%ux%u) cfg=%ux%u dst=%ux%u rot=%u cpr=%d scl=%d len=%u\n",
		     (unsigned)seq,
		     (unsigned)m->width, (unsigned)m->height, (unsigned)m->sof_marker, sampling,
		     (unsigned)m->h[0], (unsigned)m->v[0],
		     (unsigned)m->h[1], (unsigned)m->v[1],
		     (unsigned)m->h[2], (unsigned)m->v[2],
		     (unsigned)ctx->cfg.src_width, (unsigned)ctx->cfg.src_height,
		     (unsigned)ctx->cfg.dst_width, (unsigned)ctx->cfg.dst_height,
		     (unsigned)ctx->cfg.rotate_degree, (int)ctx->cfg.compress, (int)ctx->cfg.scale,
		     (unsigned)len);
	}
	if (m->width != ctx->cfg.src_width || m->height != ctx->cfg.src_height) {
		LOGE("[diag] in#%u size mismatch jpg=%ux%u cfg=%ux%u\n",
		     (unsigned)seq,
		     (unsigned)m->width, (unsigned)m->height,
		     (unsigned)ctx->cfg.src_width, (unsigned)ctx->cfg.src_height);
	}
}

/* ------------------------------------------------------------------ */
/* Backend table                                                       */
/* ------------------------------------------------------------------ */
const dsp_codec_ops_t dsp_jpeg_ops = {
	.name             = "jpeg",
	.decoder_setup    = dsp_jpeg_decoder_setup,
	.decoder_teardown = dsp_jpeg_decoder_teardown,
	.bond_start       = dsp_jpeg_bond_start,
	.bond_stop        = dsp_jpeg_bond_stop,
	.decode_frame     = dsp_jpeg_decode_frame,
	.admit_frame      = dsp_jpeg_admit_frame,
	.on_activate      = NULL,
	.on_push_full     = dsp_jpeg_on_push_full,
	.parse_meta       = dsp_jpeg_parse_meta,
	.diag_frame       = dsp_jpeg_diag_frame,
};
