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
 * Description: Decode Stream Pipeline - H264 backend.
 *
 * Owns everything H264-specific: the VCDEC H264 decoder lifecycle, the h264d
 * FLEXA bond, Annex-B access unit classification (IDR / SPS / slice type),
 * and the GOP-aware admission policy (frames of a broken reference chain are
 * dropped until the next IDR instead of decoding into smeared blocks).
 */

#include "decode_stream_pipeline_internal.h"

/* Head of an H264 access unit scanned for NALU classification. */
#define DSP_H264_SCAN_BYTES           512U
#define DSP_H264_SCAN_MAX_NALU        8U
/* Set back to 1 to re-enable GOP-aware dropping. Kept off while the phone-side
 * frame loss is under investigation: the pipeline must not discard anything of
 * its own, otherwise local and remote losses cannot be told apart. */
#define DSP_H264_GOP_DROP_ENABLE      1

/* ------------------------------------------------------------------ */
/* Annex-B access unit classification                                  */
/* ------------------------------------------------------------------ */
/* Annex-B start code at buf[i]? Reports 4-byte form first so the NALU header
 * lands on the right byte for both 00 00 00 01 and 00 00 01. */
static int dsp_h264_start_code(const uint8_t *buf, uint32_t len, uint32_t i, uint32_t *sc_len)
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
} dsp_bitrd_t;

static uint32_t dsp_bitrd_u1(dsp_bitrd_t *r)
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
static uint32_t dsp_bitrd_ue(dsp_bitrd_t *r)
{
	uint32_t lead = 0U;
	uint32_t rest = 0U;
	uint32_t k;

	while (lead < 32U && dsp_bitrd_u1(r) == 0U)
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
		rest = (rest << 1) | dsp_bitrd_u1(r);
	}

	return ((1U << lead) - 1U) + rest;
}

/* Reads slice_type out of a slice header starting at buf[0] (NALU header
 * already consumed) and folds the 5..9 "all slices same type" range down. */
static uint8_t dsp_h264_slice_type(const uint8_t *buf, uint32_t len)
{
	dsp_bitrd_t rd;
	uint32_t    slice_type;

	rd.buf = buf;
	rd.len = len;
	rd.bit = 0U;

	(void)dsp_bitrd_ue(&rd);          /* first_mb_in_slice */
	slice_type = dsp_bitrd_ue(&rd);

	if (slice_type >= 5U)
	{
		slice_type -= 5U;
	}

	return (slice_type <= DSP_H264_SLICE_SI) ? (uint8_t)slice_type
						 : DSP_H264_SLICE_UNKNOWN;
}

static const char *dsp_h264_frame_kind(const dsp_h264_meta_t *m)
{
	if (m->has_idr)
	{
		return "IDR";
	}

	switch (m->slice_type)
	{
	case DSP_H264_SLICE_P:  return "P";
	case DSP_H264_SLICE_B:  return "B";
	case DSP_H264_SLICE_I:  return "I";
	case DSP_H264_SLICE_SP: return "SP";
	case DSP_H264_SLICE_SI: return "SI";
	default:                return "?";
	}
}

/*
 * Classify one access unit. Only the head of the AU is scanned: parameter sets,
 * SEI and the first slice header all sit there, and walking a full 40 KB AU on
 * the producer thread for every frame is not worth it.
 */
static int dsp_parse_h264_meta(const uint8_t *buf, uint32_t len, dsp_h264_meta_t *m)
{
	uint32_t scan_len;
	uint32_t nalu_seen = 0U;
	uint32_t i;

	if (buf == NULL || m == NULL)
	{
		return 0;
	}

	os_memset(m, 0, sizeof(*m));
	m->slice_type = DSP_H264_SLICE_UNKNOWN;

	if (len < 5U)
	{
		return 0;
	}

	scan_len = (len < DSP_H264_SCAN_BYTES) ? len : DSP_H264_SCAN_BYTES;

	/* Annex-B access units begin with a start code prefix (the 3- or 4-byte
	 * form). JPEG/SOI instead begins with 0xFF D8, so anchoring the first
	 * NALU to the head of the buffer rejects JPEG (and other non-H264 data)
	 * before the scan below can latch onto a coincidental 00 00 01 inside the
	 * entropy-coded segment. */
	for (i = 0; i < 4U; i++)
	{
		uint32_t sc0 = 0U;

		if (dsp_h264_start_code(buf, scan_len, i, &sc0))
		{
			break;
		}
	}
	if (i == 4U)
	{
		return 0;
	}

	for (i = 0; (i + 4U) <= scan_len && nalu_seen < DSP_H264_SCAN_MAX_NALU; )
	{
		uint32_t sc_len = 0U;
		uint8_t  nalu_hdr;
		uint8_t  nalu_type;

		if (!dsp_h264_start_code(buf, scan_len, i, &sc_len))
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
		case DSP_H264_NALU_IDR:
		case DSP_H264_NALU_SLICE:
			m->is_h264 = 1U;
			if (nalu_type == DSP_H264_NALU_IDR)
			{
				m->has_idr = 1U;
			}
			/* Only the first slice of the AU decides the reported frame type. */
			if (m->slice_type == DSP_H264_SLICE_UNKNOWN)
			{
				m->slice_type = dsp_h264_slice_type(&buf[i], scan_len - i);
			}
			break;
		case DSP_H264_NALU_SPS:
			m->has_sps = 1U;
			m->is_h264 = 1U;
			break;
		case DSP_H264_NALU_SEI:
		case DSP_H264_NALU_PPS:
		case DSP_H264_NALU_AUD:
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
/* Decoder lifecycle                                                   */
/* ------------------------------------------------------------------ */
static avdk_err_t dsp_h264_decoder_setup(struct decode_stream_pipeline_ctx *ctx)
{
	avdk_err_t ret;
	bk_h264_decode_flexa_config_t dec_cfg = DEFAULT_H264_DECODE_FLEXA_CONFIG;

	if (ctx->h264.handle != NULL)
		return AVDK_ERR_OK; /* already alive */

	dec_cfg.timeout_ms     = ctx->cfg.decode_timeout_ms;
	dec_cfg.out_width      = ctx->cfg.src_width;
	dec_cfg.out_height     = ctx->cfg.src_height;
	dec_cfg.out_format     = BK_PIXEL_FORMAT_NV12;
	dec_cfg.segment_height = (uint16_t)(ctx->cfg.flexa_lines / 16U);
	dec_cfg.segment_number = ctx->cfg.flexa_buff_cnt;
	dec_cfg.frame_done_cb  = dsp_decoder_frame_done;
	dec_cfg.frame_done_args = NULL;
	dec_cfg.flexa_done_cb  = NULL;
	dec_cfg.flexa_done_args = NULL;

	ret = bk_h264_decode_flexa_ctlr_new(&ctx->h264.handle, &dec_cfg);
	if (ret != AVDK_ERR_OK)
	{
		LOGE("%s: bk_h264_decode_new ret=%d\n", __func__, ret);
		return ret;
	}
	ret = bk_h264_decode_init(ctx->h264.handle);
	if (ret != AVDK_ERR_OK)
	{
		LOGE("%s: bk_h264_decode_init ret=%d\n", __func__, ret);
		goto err_delete;
	}
	ret = bk_h264_decode_open(ctx->h264.handle);
	if (ret != AVDK_ERR_OK)
	{
		LOGE("%s: bk_h264_decode_open ret=%d\n", __func__, ret);
		goto err_deinit;
	}

	return AVDK_ERR_OK;

err_deinit:
	(void)bk_h264_decode_deinit(ctx->h264.handle);
err_delete:
	(void)bk_h264_decode_delete(ctx->h264.handle);
	ctx->h264.handle = NULL;
	return ret;
}

static void dsp_h264_decoder_teardown(struct decode_stream_pipeline_ctx *ctx)
{
	if (ctx->h264.handle == NULL)
		return;
	(void)bk_h264_decode_close(ctx->h264.handle);
	(void)bk_h264_decode_deinit(ctx->h264.handle);
	(void)bk_h264_decode_delete(ctx->h264.handle);
	ctx->h264.handle = NULL;
}

/* ------------------------------------------------------------------ */
/* FLEXA bond                                                          */
/* ------------------------------------------------------------------ */
static avdk_err_t dsp_h264_bond_start(struct decode_stream_pipeline_ctx *ctx)
{
	return bk_flexa_h264d_gpu_bond_start(&ctx->bond,
					     ctx->h264.handle,
					     ctx->gpu_handle);
}

static void dsp_h264_bond_stop(struct decode_stream_pipeline_ctx *ctx)
{
	if (ctx->bond == NULL)
		return;
	bk_flexa_h264d_gpu_bond_stop(ctx->bond);
	ctx->bond = NULL;
}

/* ------------------------------------------------------------------ */
/* Decode                                                              */
/* ------------------------------------------------------------------ */
static avdk_err_t dsp_h264_decode_frame(struct decode_stream_pipeline_ctx *ctx,
					const dsp_frame_entry_t *entry)
{
	bk_h264_decode_input_t input;

	if (ctx->h264.handle == NULL)
		return AVDK_ERR_INVAL;

	os_memset(&input, 0, sizeof(input));
	input.stream          = (uint8_t *)entry->stream;
	input.stream_len      = entry->len;
	input.out_buffer      = ctx->ring_buf;
	input.out_buffer_size = ctx->ring_size;

	return bk_h264_decode_frame(ctx->h264.handle, &input);
}

/* ------------------------------------------------------------------ */
/* Admission (GOP-aware)                                               */
/* ------------------------------------------------------------------ */
/*
 * An IDR decodes standalone, so it both resets the drop state and makes every
 * queued frame stale -- draining there also guarantees the IDR itself gets a
 * slot. A non-IDR frame is only admitted while its reference chain is intact;
 * once a frame has been lost, feeding the decoder the rest of the GOP produces
 * smeared blocks instead of a clean freeze.
 */
static avdk_err_t dsp_h264_admit_frame(struct decode_stream_pipeline_ctx *ctx,
					const dsp_frame_meta_t *meta)
{
#if DSP_H264_GOP_DROP_ENABLE
	if (meta->h264.has_idr)
	{
		(void)dsp_drain_queue(ctx);

		if (ctx->h264.drop_until_idr)
		{
			LOGW("[cast] h264 resync on IDR (%u frames dropped since chain break)\n",
			     (unsigned)ctx->h264.gop_dropped);
			ctx->h264.drop_until_idr = 0;
			ctx->h264.gop_dropped    = 0;
		}
		return AVDK_ERR_OK;
	}

	if (ctx->h264.drop_until_idr)
	{
		ctx->h264.gop_dropped++;
		return AVDK_ERR_BUSY;
	}

	return AVDK_ERR_OK;
#else
	(void)ctx;
	(void)meta;
	return AVDK_ERR_OK;
#endif
}

/* ------------------------------------------------------------------ */
/* Activation                                                          */
/* ------------------------------------------------------------------ */
static void dsp_h264_on_activate(struct decode_stream_pipeline_ctx *ctx)
{
	/* A stream may join mid-GOP (e.g. the source switched codec, or the cast
	 * session started mid-broadcast): hold frames until the first IDR so the
	 * decoder never references frames it never received. A stream that
	 * starts with SPS/PPS/IDR is unaffected -- the IDR clears this on its
	 * first admit. */
	ctx->h264.drop_until_idr = 1;
	ctx->h264.gop_dropped    = 0;
}

static void dsp_h264_on_push_full(struct decode_stream_pipeline_ctx *ctx, uint32_t seq,
				  const dsp_frame_meta_t *meta, uint32_t len)
{
#if DSP_H264_GOP_DROP_ENABLE
	(void)seq;
	(void)meta;
	/* The decoder fell behind far enough to fill the queue. This frame is
	 * lost, so every following frame references something that never got
	 * decoded -- drop the rest of the GOP and resync on the next IDR. */
	if (!ctx->h264.drop_until_idr)
	{
		LOGW("[cast] h264 queue full, dropping until next IDR (len=%u)\n",
		     (unsigned)len);
		ctx->h264.drop_until_idr = 1;
		ctx->h264.gop_dropped    = 0;
	}
	ctx->h264.gop_dropped++;
#else
	(void)ctx;
	/* Every local loss is reported individually so it can be subtracted
	 * from the gap seen at the phone. */
	LOGW("[cast] h264 push qfull #%u %s len=%u\n",
	     (unsigned)seq, dsp_h264_frame_kind(&meta->h264), (unsigned)len);
#endif
}

/* ------------------------------------------------------------------ */
/* Classification + diagnostics                                        */
/* ------------------------------------------------------------------ */
static int dsp_h264_parse_meta(const uint8_t *buf, uint32_t len, dsp_frame_meta_t *meta)
{
	return dsp_parse_h264_meta(buf, len, &meta->h264);
}

static void dsp_h264_diag_frame(struct decode_stream_pipeline_ctx *ctx, uint32_t seq,
				const dsp_frame_meta_t *meta, uint32_t len)
{
	(void)ctx;
	LOGPF("[diag] in#%u h264 %s len=%u%s\n",
	      (unsigned)seq, dsp_h264_frame_kind(&meta->h264), (unsigned)len,
	      meta->h264.has_sps ? " +sps" : "");
}

/* ------------------------------------------------------------------ */
/* Backend table                                                       */
/* ------------------------------------------------------------------ */
const dsp_codec_ops_t dsp_h264_ops = {
	.name             = "h264",
	.decoder_setup    = dsp_h264_decoder_setup,
	.decoder_teardown = dsp_h264_decoder_teardown,
	.bond_start       = dsp_h264_bond_start,
	.bond_stop        = dsp_h264_bond_stop,
	.decode_frame     = dsp_h264_decode_frame,
	.admit_frame      = dsp_h264_admit_frame,
	.on_activate      = dsp_h264_on_activate,
	.on_push_full     = dsp_h264_on_push_full,
	.parse_meta       = dsp_h264_parse_meta,
	.diag_frame       = dsp_h264_diag_frame,
};
