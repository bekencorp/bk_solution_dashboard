/*
 * Casting: JPEG (PSRAM) -> jpeg_stream_pipeline -> bk_display_flush.
 *
 * Serial trace: grep "[cast]". Phases: 0=enter 1=lvgl-prep 2=relgpu 3=create 4=start
 * pj=app push fd=frame_display dec=decode jpgc=consumed
 * App JPEG enters via cast_jpeg_pipeline_push_frame_buffer() under s_cast_ops_mutex.
 */

#include "cast_jpeg_pipeline.h"

#include <os/os.h>
#include <os/mem.h>
#include <stdint.h>
#include <string.h>

#include <components/log.h>
#include <components/avdk_utils/avdk_error.h>
#include <common/bk_err.h>
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>
#include <modules/vg_lite_gpu/vg_lite.h>
#include "gpu_core.h"
#include "jpeg_stream_pipeline.h"

#define TAG "cast_jpeg"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static void cast_hooks_noop_void(void) {}
static void cast_hooks_noop_set_decompress(int enable)
{
	(void)enable;
}

static cast_jpeg_pipeline_hooks_t s_cast_hooks = {
	.pre_start = cast_hooks_noop_void,
	.first_frame_apply = cast_hooks_noop_void,
	.post_stop = cast_hooks_noop_void,
};

static cast_jpeg_video_recv_alloc_gate_fn s_video_recv_alloc_gate_fn;

void cast_jpeg_pipeline_set_video_recv_alloc_gate_fn(cast_jpeg_video_recv_alloc_gate_fn fn)
{
	s_video_recv_alloc_gate_fn = fn;
}

static void cast_video_recv_gate_call(int allow)
{
	if (s_video_recv_alloc_gate_fn) {
		s_video_recv_alloc_gate_fn(allow);
	}
}

void cast_jpeg_pipeline_register_hooks(const cast_jpeg_pipeline_hooks_t *hooks)
{
	if (hooks == NULL) {
		s_cast_hooks.pre_start = cast_hooks_noop_void;
		s_cast_hooks.first_frame_apply = cast_hooks_noop_void;
		s_cast_hooks.post_stop = cast_hooks_noop_void;
		return;
	}

	s_cast_hooks.pre_start = hooks->pre_start ? hooks->pre_start : cast_hooks_noop_void;
	s_cast_hooks.first_frame_apply = hooks->first_frame_apply ? hooks->first_frame_apply : cast_hooks_noop_void;
	s_cast_hooks.post_stop = hooks->post_stop ? hooks->post_stop : cast_hooks_noop_void;
}

static jpeg_stream_pipeline_handle_t s_pipeline;
static bk_display_ctlr_handle_t      s_disp;
static bk_display_ctlr_handle_t      s_pipeline_disp;
/* Apply ARGB+decompress on first GPU frame, not right after pipeline start — avoids
 * a window where the panel scans decompress mode while the last buffer is still
 * RGB565 (DC panel0 data underflow / apparent freeze). */
static volatile int s_cast_dpu_apply_on_first_frame;
static volatile uint8_t s_cast_pipeline_running;
static uint32_t     s_cast_frame_display_seq;
static uint32_t     s_pipeline_start_ms;
/* Incremented when cast_gpu_malloc_cb fails (e.g. bk_gpu_open extra allocs); checked after jpeg_stream_pipeline_create. */
static volatile uint32_t s_cast_gpu_malloc_fail_count;
static beken_semaphore_t s_cast_flush_sem;
static volatile uint32_t s_cast_flush_signal_seq;

static beken_mutex_t s_cast_ops_mutex;
static int           s_cast_ops_mutex_inited;

static void (*s_fb_release)(frame_buffer_t *fb);
static void cast_gpu_free_output(void *p);

static int cast_ops_mutex_ensure(void)
{
	if (s_cast_ops_mutex_inited)
		return 1;
	if (rtos_init_mutex(&s_cast_ops_mutex) == BK_OK) {
		s_cast_ops_mutex_inited = 1;
		return 1;
	}
	LOGE("[cast] mutex init failed\n");
	return 0;
}

void cast_jpeg_pipeline_set_frame_buffer_release(void (*fn)(frame_buffer_t *))
{
	s_fb_release = fn;
}

static void cast_release_fb(frame_buffer_t *fb)
{
	if (fb == NULL)
		return;
	if (s_fb_release)
		s_fb_release(fb);
	else
		LOGE("[cast] fb rel no cb %p\n", fb);
}

static int cast_flush_sem_init(void)
{
	if (s_cast_flush_sem)
		return 1;
	if (rtos_init_semaphore_ex(&s_cast_flush_sem, 1, 0) == BK_OK)
		return 1;
	LOGE("[cast] flush sem init failed\n");
	return 0;
}

static avdk_err_t cast_flush_signal_cb(void *frame)
{
	/*
	 * bk_display_flush() transfers ownership to the DPU.  This callback is the
	 * point at which the DPU has replaced the frame at VSync and no longer reads
	 * it, so only now may the GPU output buffer return to the pool.
	 */
	cast_gpu_free_output(frame);
	s_cast_flush_signal_seq++;
	if (s_cast_flush_sem)
		rtos_set_semaphore(&s_cast_flush_sem);
	return AVDK_ERR_OK;
}

bk_err_t cast_jpeg_pipeline_wait_display_flush(uint32_t timeout_ms)
{
	uint32_t start_seq;
	bk_err_t ret;

	if (!cast_flush_sem_init())
		return BK_FAIL;

	start_seq = s_cast_flush_signal_seq;
	do {
		ret = rtos_get_semaphore(&s_cast_flush_sem, timeout_ms);
		if (s_cast_flush_signal_seq != start_seq)
			return BK_OK;
	} while (ret == BK_OK);

	return ret;
}

/*
 * GPU flex path allocates a *second* full output buffer before calling
 * frame_done(); two ~2MB blocks do not fit the small psram_malloc heap
 * alongside FLEXA ring + JPEG copy + stacks — malloc fails and the SDK
 * skips the frame_done delivery entirely (no DPU flush, frozen standby UI).
 * Use the video mem slab (same pools as LVGL frame buffers).
 *
 * Session-scoped pool: fixed buffers for the whole cast session.
 * Three slots: GPU flex allocates the working buffer at thread start, then on
 * every frame the SDK does frame_malloc(new) before frame_done(old)
 * (bk_gpu_ctlr_default.c); a third slot covers transient overlap + slow
 * return of buffers to the pool.
 *
 * s_cast_gpu_pool_active must be 1 *before* jpeg_stream_pipeline_create(): inside
 * create, bk_gpu_open invokes malloc_cb for flex->dpu_frame_buffers; if active
 * were still 0, the callback would skip the pool and hit bk_frame_buffer_malloc
 * again (same 921600 pressure that pooling was meant to avoid).
 */
#define CAST_GPU_POOL_SLOTS 3

static void               *s_cast_gpu_pool_ptr[CAST_GPU_POOL_SLOTS];
static uint32_t            s_cast_gpu_pool_bytes;
static void               *s_cast_gpu_stack[CAST_GPU_POOL_SLOTS];
static int                 s_cast_gpu_stack_n;
static volatile uint8_t    s_cast_gpu_pool_active;

/*
 * Match bk_gpu_ctlr_default.c gpu_flex_data_frame_done / gpu_flex_main_entry
 * frame_size EXACTLY: the GPU aligns output width (compress only) and height up
 * to 16 (GPU_HIGHT_ALIGNMENT=0xF). If we omit that alignment the pool byte size
 * differs from the size the GPU asks malloc_cb for (e.g. 1024x600 -> pool 614400
 * vs GPU 622592 because 600 rounds up to 608), the pool is never hit, and every
 * frame falls back to bk_frame_buffer_malloc until the slab fragments/exhausts
 * (gpu_malloc U / FAIL).
 */
static uint32_t cast_gpu_pool_expected_bytes(uint32_t dst_w, uint32_t dst_h, bool compress,
					     bk_pixel_format_t fmt)
{
	const uint32_t out_w  = compress ? ((dst_w + 0xFU) & ~0xFU) : dst_w;
	const uint32_t out_h  = (dst_h + 0xFU) & ~0xFU;
	const uint32_t row_px = compress ? (out_w / 4U) : out_w;

	return bk_pixel_size_get(fmt) * row_px * out_h;
}

static bool cast_gpu_pool_is_our_ptr(const void *p)
{
	if (p == NULL)
		return false;
	for (int i = 0; i < CAST_GPU_POOL_SLOTS; i++) {
		if (p == s_cast_gpu_pool_ptr[i])
			return true;
	}
	return false;
}

static void cast_gpu_pool_push(void *p)
{
	uint32_t flags;

	if (p == NULL)
		return;

	flags = rtos_enter_critical();
	/* Make release idempotent: some display error paths invoke the supplied
	 * callback before returning an error to the caller. */
	for (int i = 0; i < s_cast_gpu_stack_n; i++) {
		if (s_cast_gpu_stack[i] == p) {
			rtos_exit_critical(flags);
			return;
		}
	}
	if (s_cast_gpu_stack_n >= CAST_GPU_POOL_SLOTS) {
		rtos_exit_critical(flags);
		return;
	}
	s_cast_gpu_stack[s_cast_gpu_stack_n++] = p;
	rtos_exit_critical(flags);
}

static void *cast_gpu_pool_pop(void)
{
	uint32_t flags;
	void *p;

	flags = rtos_enter_critical();
	if (s_cast_gpu_stack_n <= 0) {
		rtos_exit_critical(flags);
		return NULL;
	}
	p = s_cast_gpu_stack[--s_cast_gpu_stack_n];
	rtos_exit_critical(flags);
	return p;
}

static void cast_gpu_pool_free_all(void)
{
	s_cast_gpu_pool_active = 0;
	s_cast_gpu_pool_bytes  = 0;
	s_cast_gpu_stack_n     = 0;

	for (int i = 0; i < CAST_GPU_POOL_SLOTS; i++) {
		if (s_cast_gpu_pool_ptr[i] != NULL) {
			bk_frame_buffer_free(s_cast_gpu_pool_ptr[i]);
			s_cast_gpu_pool_ptr[i] = NULL;
		}
	}
}

static bk_err_t cast_gpu_pool_alloc(uint32_t dst_w, uint32_t dst_h, bool compress,
				    bk_pixel_format_t dst_fmt)
{
	uint32_t sz = cast_gpu_pool_expected_bytes(dst_w, dst_h, compress, dst_fmt);

	cast_gpu_pool_free_all();

	for (int i = 0; i < CAST_GPU_POOL_SLOTS; i++) {
		void *p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, sz);

		if (p == NULL) {
			LOGW("[cast] gpu pool slot%u U %u\n", (unsigned)i, (unsigned)sz);
			p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, sz);
		}
		if (p == NULL) {
			LOGE("[cast] gpu pool slot%u FAIL %u\n", (unsigned)i, (unsigned)sz);
			cast_gpu_pool_free_all();
			return BK_FAIL;
		}
		/*
		 * The compressed GPU path writes the output in FLEXA strips.  Start
		 * every session from deterministic backing storage so an incomplete
		 * first strip/tile cannot expose stale PSRAM contents on the DPU.
		 */
		os_memset(p, 0, sz);
		s_cast_gpu_pool_ptr[i] = p;
	}

	s_cast_gpu_pool_bytes = sz;
	s_cast_gpu_stack_n    = 0;
	for (int i = 0; i < CAST_GPU_POOL_SLOTS; i++)
		cast_gpu_pool_push(s_cast_gpu_pool_ptr[i]);
	LOGI("[cast] gpu pool %u B x%u (dst %ux%u cpr=%d)\n",
	     (unsigned)sz, (unsigned)CAST_GPU_POOL_SLOTS, (unsigned)dst_w, (unsigned)dst_h,
	     (int)compress);
	return BK_OK;
}

static void *cast_gpu_malloc_cb(uint32_t size)
{
	void *p;

	if (s_cast_gpu_pool_active && s_cast_gpu_pool_bytes == size) {
		p = cast_gpu_pool_pop();
		if (p != NULL)
			return p;
		LOGW("[cast] gpu pool empty, fallback alloc %u\n", (unsigned)size);
	}

	p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);

	if (p == NULL) {
		LOGW("[cast] gpu_malloc U %u\n", (unsigned)size);
		p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, size);
	}
	if (p == NULL) {
		LOGE("[cast] gpu_malloc FAIL %u\n", (unsigned)size);
		s_cast_gpu_malloc_fail_count++;
	}
	return p;
}

static void cast_gpu_free_output(void *p)
{
	if (p == NULL)
		return;

	if (cast_gpu_pool_is_our_ptr(p)) {
		/*
		 * During teardown cast_gpu_pool_free_all() owns all pool allocations.
		 * A late DPU callback must not individually free one and leave its
		 * address in s_cast_gpu_pool_ptr[] for a second free.
		 */
		if (s_cast_gpu_pool_active) {
			cast_gpu_pool_push(p);
			LOGI("[cast] pool free %p avail %d/%d\n",
			     p, s_cast_gpu_stack_n, CAST_GPU_POOL_SLOTS);
		}
		return;
	}

	bk_frame_buffer_free(p);
}

/*
 * Re-render the whole surface to *opaque black* directly into the GPU output.
 *
 * frame is a VG_LITE_DEC_HV_SAMPLE compressed buffer, not linear ARGB, so
 * os_memset(frame, 0, ...) is not a legal compressed encoding — the DEC unit
 * decodes it to green garbage.  A real black frame must be produced by the GPU:
 * wrap the buffer with the exact same DEC/tiled/format attributes the flex path
 * uses, then vg_lite_clear() it to 0xFF000000.
 *
 * This runs inside the GPU frame_done callback (GPU worker thread) with the
 * engine idle between frames, so no extra bk_gpu_global_lock is needed. For a
 * uniform clear the tile-grid orientation is irrelevant (every tile is the same
 * solid-black cell), so an approximate width/height is safe as long as the total
 * covered area equals the allocated surface.
 */
static bool cast_gpu_render_black_compressed(void *frame)
{
	vg_lite_buffer_t buf;
	vg_lite_error_t  e;
	const uint32_t   ow   = (CAST_JPEG_DST_WIDTH  + 0xFU) & ~0xFU;
	const uint32_t   oh   = (CAST_JPEG_DST_HEIGHT + 0xFU) & ~0xFU;
	const bool       swap = (CAST_JPEG_ROTATE_DEG == 90U || CAST_JPEG_ROTATE_DEG == 270U);

	if (frame == NULL)
		return false;

	os_memset(&buf, 0, sizeof(buf));
	buf.width         = swap ? oh : ow;
	buf.height        = swap ? ow : oh;
	buf.format        = VG_LITE_BGRA8888; /* == gpu_format_convert(BK_PIXEL_FORMAT_ARGB8888) */
	buf.compress_mode = VG_LITE_DEC_HV_SAMPLE;
	buf.tiled         = VG_LITE_TILED;
	buf.screen_copy   = 1;

	/*
	 * Serialize against the flex worker's own vg_lite usage on the shared
	 * VG-Lite engine/command buffer. Although this callback runs on the GPU
	 * frame_done thread with the engine nominally idle, take the same global
	 * lock the flex path uses so a concurrent GPU user cannot corrupt state.
	 */
	if (bk_gpu_global_lock() != BK_OK) {
		LOGE("[cast] black lock fail\n");
		return false;
	}

	e = vg_lite_allocate_with_data(&buf, frame, NULL, NULL, NULL);
	if (e != VG_LITE_SUCCESS) {
		LOGE("[cast] black wrap %d\n", (int)e);
		bk_gpu_global_unlock();
		return false;
	}

	e = vg_lite_clear(&buf, NULL, 0xFF000000U);
	if (e == VG_LITE_SUCCESS)
		e = vg_lite_finish();

	(void)vg_lite_free_without_free_data(&buf);

	bk_gpu_global_unlock();

	if (e != VG_LITE_SUCCESS) {
		LOGE("[cast] black clear %d\n", (int)e);
		return false;
	}
	return true;
}

static void cast_frame_display_cb(void *frame, uint32_t frame_size, void *user_data)
{
	bk_display_ctlr_handle_t disp = (bk_display_ctlr_handle_t)user_data;
	int                      pending = s_cast_dpu_apply_on_first_frame;
	uint32_t                 n       = ++s_cast_frame_display_seq;
	avdk_err_t               flush_e = AVDK_ERR_OK;

	/*
	 * The first FLEXA/GPU output after opening the JPEG pipeline is a hardware
	 * warm-up frame and can contain incomplete compressed tiles. Replace its
	 * contents with a deterministic all-black compressed surface, then display
	 * that surface until the next valid navigation frame arrives.
	 */
	if (n == 1U) {
		bool black_ok = cast_gpu_render_black_compressed(frame);

		if (s_cast_dpu_apply_on_first_frame) {
			s_cast_hooks.first_frame_apply();
			s_cast_dpu_apply_on_first_frame = 0;
		}
		if (disp && frame && black_ok)
			flush_e = bk_display_flush(disp, frame, cast_flush_signal_cb);
		LOGW("[cast] fd1 warm-up black blk=%d sz%u r%d %p\n",
		     (int)black_ok, (unsigned)frame_size, (int)flush_e, frame);
		if (!disp || !frame || !black_ok || flush_e != AVDK_ERR_OK)
			cast_gpu_free_output(frame);
		return;
	}
	/*
	 * first_frame_apply()/DPU ARGB+decompress switch already happened on the
	 * n==1 warm-up frame above, so no apply is needed here for later frames.
	 */
	if (disp && frame)
		flush_e = bk_display_flush(disp, frame, cast_flush_signal_cb);
	else
		LOGW("[cast] fd%u !disp %p %p\n", (unsigned)n, disp, frame);

	LOGI("[cast] fd%u sz%u p%d r%d %p\n",
	     (unsigned)n, (unsigned)frame_size, pending, (int)flush_e, frame);
	if (disp && frame && flush_e != AVDK_ERR_OK) {
		LOGE("[cast] fd%u flush %d\n", (unsigned)n, (int)flush_e);
		/* A rejected frame is not owned by the DPU. Pool release is
		 * idempotent in case this backend invoked the callback on failure. */
		cast_gpu_free_output(frame);
	} else if (!disp && frame) {
		cast_gpu_free_output(frame);
	}
}

static void cast_frame_consumed_cb(const uint8_t *jpeg_stream, int status, void *jpeg_owner,
	void *user_data)
{
	(void)user_data;
	if (status == JSP_STATUS_FRAME_DROPPED)
		LOGW("[cast] jpgc frame dropped\n");
	else if (status != 0)
		LOGE("[cast] jpgc st=%d\n", status);
	if (jpeg_owner != NULL) {
		cast_release_fb((frame_buffer_t *)jpeg_owner);
		return;
	}
	if (jpeg_stream)
		psram_free((void *)jpeg_stream);
}

/* Preconditions: s_pipeline != NULL; caller holds s_cast_ops_mutex. */
static bk_err_t cast_pipeline_push_fb_locked(frame_buffer_t *fb)
{
	avdk_err_t err;

	LOGI("[cast] pjfb %u\n", (unsigned)fb->length);
	err = jpeg_stream_pipeline_push_frame(s_pipeline, fb->frame, fb->length, fb);
	if (err != AVDK_ERR_OK) {
		if (err == AVDK_ERR_BUSY)
			return BK_ERR_BUSY;
		LOGE("[cast] pjfbe %d\n", (int)err);
		return BK_FAIL;
	}
	return BK_OK;
}

bk_err_t cast_jpeg_pipeline_push_frame_buffer(frame_buffer_t *fb)
{
	bk_err_t ret;

	if (fb == NULL || fb->frame == NULL || fb->length == 0U) {
		LOGW("[cast] pjfb !arg\n");
		return BK_FAIL;
	}

	/* On any failure here the caller frees fb (media_frame_queue_free); do NOT free
	 * it again in the callee — the pipeline only owns fb once push succeeds. */
	if (s_pipeline == NULL) {
		LOGW("[cast] pjfb !pipe\n");
		return BK_FAIL;
	}

	if (!cast_ops_mutex_ensure()) {
		return BK_FAIL;
	}
	rtos_lock_mutex(&s_cast_ops_mutex);
	if (s_pipeline == NULL) {
		rtos_unlock_mutex(&s_cast_ops_mutex);
		return BK_FAIL;
	}
	ret = cast_pipeline_push_fb_locked(fb);
	rtos_unlock_mutex(&s_cast_ops_mutex);
	return ret;
}

bk_err_t cast_jpeg_pipeline_turn_on(bk_display_ctlr_handle_t disp)
{
	avdk_err_t                   err;
	jpeg_stream_pipeline_config_t cfg;

	/* Block WiFi unfragment malloc until pipeline is ready (or dup / error exit). */
	cast_video_recv_gate_call(0);

	if (!cast_ops_mutex_ensure()) {
		LOGE("[cast] on: mutex init failed\n");
		cast_video_recv_gate_call(1);
		return BK_FAIL;
	}

	rtos_lock_mutex(&s_cast_ops_mutex);

	if (disp == NULL) {
		rtos_unlock_mutex(&s_cast_ops_mutex);
		LOGE("[cast] 0 !disp\n");
		cast_video_recv_gate_call(1);
		return BK_FAIL;
	}

	if (s_pipeline != NULL) {
		if (s_cast_pipeline_running) {
			uint32_t age_ms = rtos_get_time() - s_pipeline_start_ms;
			rtos_unlock_mutex(&s_cast_ops_mutex);
			LOGW("[cast] on dup (seq=%u age=%u ms)\n",
			     (unsigned)s_cast_frame_display_seq, (unsigned)age_ms);
			cast_video_recv_gate_call(1);
			return BK_OK;
		}

		if (s_pipeline_disp == disp) {
			LOGI("[cast] reuse pipeline=%p\n", s_pipeline);
			s_disp = disp;
			s_cast_dpu_apply_on_first_frame = 0;
			s_cast_frame_display_seq        = 0;
			cast_flush_sem_init();
			LOGI("[cast] 1 cast-prep (reuse)\n");
			s_cast_hooks.pre_start();
			s_cast_gpu_pool_active = 1U;
			err = jpeg_stream_pipeline_start(s_pipeline);
			if (err != AVDK_ERR_OK) {
				LOGE("[cast] reuse start failed %d\n", (int)err);
				s_cast_gpu_pool_active = 0;
				s_disp = NULL;
				s_cast_hooks.post_stop();
				rtos_unlock_mutex(&s_cast_ops_mutex);
				cast_video_recv_gate_call(1);
				return BK_FAIL;
			}
			s_cast_pipeline_running = 1U;
			s_cast_dpu_apply_on_first_frame = 1;
			s_pipeline_start_ms = rtos_get_time();
			LOGI("[cast] 4 ok first_frame_pending=1 s_disp=%p\n", s_disp);
			cast_video_recv_gate_call(1);
			rtos_unlock_mutex(&s_cast_ops_mutex);
			return BK_OK;
		}

		LOGW("[cast] display changed, recreate pipeline %p -> %p\n", s_pipeline_disp, disp);
		(void)jpeg_stream_pipeline_destroy(s_pipeline);
		s_pipeline = NULL;
		s_pipeline_disp = NULL;
		cast_gpu_pool_free_all();
	}

	LOGI("[cast] 0 %p\n", disp);
	s_disp = disp;
	s_cast_dpu_apply_on_first_frame = 0;
	s_cast_frame_display_seq        = 0;
	cast_flush_sem_init();
	/* Product hook (e.g. release boot GPU buffer); LVGL stop may be deferred to first frame. */
	LOGI("[cast] 1 cast-prep (hook before pipeline create)\n");
	s_cast_hooks.pre_start();
	LOGI("[cast] 2 relgpu\n");

	os_memset(&cfg, 0, sizeof(cfg));
	cfg.src_width  = CAST_JPEG_SRC_WIDTH;
	cfg.src_height = CAST_JPEG_SRC_HEIGHT;
	cfg.dst_width  = CAST_JPEG_DST_WIDTH;
	cfg.dst_height = CAST_JPEG_DST_HEIGHT;
	cfg.rotate_degree = (uint16_t)CAST_JPEG_ROTATE_DEG;
	cfg.dst_format    = BK_PIXEL_FORMAT_ARGB8888;
	cfg.compress      = true;
	cfg.scale         = true;

	if (cast_gpu_pool_alloc((uint32_t)cfg.dst_width, (uint32_t)cfg.dst_height, cfg.compress,
				cfg.dst_format) != BK_OK) {
		LOGE("[cast] gpu pool alloc failed\n");
		s_disp = NULL;
		s_cast_hooks.post_stop();
		cast_video_recv_gate_call(1);
		rtos_unlock_mutex(&s_cast_ops_mutex);
		return BK_FAIL;
	}
	/*
	 * Enable pool for malloc_cb during jpeg_stream_pipeline_create → bk_gpu_open
	 * (flex dpu_frame_buffers), not only after create returns.
	 */
	s_cast_gpu_pool_active = 1U;

	/* Limit in-flight JPEG frames so WiFi thread does not pile malloc when decode lags. */
	cfg.queue_depth         = 1U;
	cfg.malloc_cb           = cast_gpu_malloc_cb;
	cfg.frame_display_cb    = cast_frame_display_cb;
	cfg.frame_consumed_cb   = cast_frame_consumed_cb;
	cfg.user_data           = disp;
	LOGI("[diag] cfg src=%ux%u dst=%ux%u rot=%u cpr=%d scl=%d src_align16=%u dst_align16=%u\n",
	     (unsigned)cfg.src_width, (unsigned)cfg.src_height,
	     (unsigned)cfg.dst_width, (unsigned)cfg.dst_height,
	     (unsigned)cfg.rotate_degree, (int)cfg.compress, (int)cfg.scale,
	     (unsigned)((cfg.src_width + 15U) & ~15U),
	     (unsigned)((cfg.dst_width + 15U) & ~15U));

	s_cast_gpu_malloc_fail_count = 0U;
	err = jpeg_stream_pipeline_create(&s_pipeline, &cfg);
	if (err != AVDK_ERR_OK || s_pipeline == NULL) {
		LOGE("[cast] 3e %d %p\n", (int)err, s_pipeline);
		s_pipeline = NULL;
		s_pipeline_disp = NULL;
		s_disp = NULL;
		s_cast_hooks.post_stop();
		cast_gpu_pool_free_all();
		cast_video_recv_gate_call(1);
		rtos_unlock_mutex(&s_cast_ops_mutex);
		return BK_FAIL;
	}
	s_pipeline_disp = disp;
	if (s_cast_gpu_malloc_fail_count != 0U) {
		LOGE("[cast] 3e gpu flex alloc failed during create (fail_cnt=%u), abort\n",
		     (unsigned)s_cast_gpu_malloc_fail_count);
		(void)jpeg_stream_pipeline_destroy(s_pipeline);
		s_pipeline = NULL;
		s_pipeline_disp = NULL;
		s_disp = NULL;
		s_cast_hooks.post_stop();
		cast_gpu_pool_free_all();
		cast_video_recv_gate_call(1);
		rtos_unlock_mutex(&s_cast_ops_mutex);
		return BK_FAIL;
	}

	LOGI("[cast] 3 ok %p\n", s_pipeline);
	err = jpeg_stream_pipeline_start(s_pipeline);
	if (err != AVDK_ERR_OK) {
		LOGE("[cast] 4e %d\n", (int)err);
		s_cast_gpu_pool_active = 0;
		(void)jpeg_stream_pipeline_destroy(s_pipeline);
		s_pipeline = NULL;
		s_pipeline_disp = NULL;
		s_disp = NULL;
		s_cast_hooks.post_stop();
		cast_gpu_pool_free_all();
		cast_video_recv_gate_call(1);
		rtos_unlock_mutex(&s_cast_ops_mutex);
		return BK_FAIL;
	}

	s_cast_dpu_apply_on_first_frame = 1;
	s_cast_pipeline_running = 1U;
	s_pipeline_start_ms = rtos_get_time();
	LOGI("[cast] 4 ok first_frame_pending=1 s_disp=%p\n", s_disp);

	LOGI("[cast] ok pipeline=%p\n", s_pipeline);
	cast_video_recv_gate_call(1);
	rtos_unlock_mutex(&s_cast_ops_mutex);
	return BK_OK;
}

bk_err_t cast_jpeg_pipeline_turn_off(void)
{
	cast_video_recv_gate_call(0);
	LOGI("[cast] off\n");

	if (!cast_ops_mutex_ensure()) {
		LOGE("[cast] off: mutex init failed\n");
		return BK_FAIL;
	}
	rtos_lock_mutex(&s_cast_ops_mutex);

	s_cast_dpu_apply_on_first_frame = 0;
	s_cast_frame_display_seq = 0;
	if (s_pipeline != NULL && s_cast_pipeline_running) {
		(void)jpeg_stream_pipeline_stop(s_pipeline);
		s_cast_pipeline_running = 0;
	}
	s_disp = NULL;
	s_cast_gpu_pool_active = 0;

	/*
	 * Destroy the cast pipeline FIRST: this does a plain GPU teardown
	 * (bk_gpu_deinit -> vg_lite_close + GPU power off on the single global engine).
	 * post_stop() then re-acquires that engine for LVGL (lv_gpu_init) and restarts
	 * the LVGL task, so it must run AFTER the teardown, not before.
	 */
	if (s_pipeline != NULL) {
		(void)jpeg_stream_pipeline_destroy(s_pipeline);
		s_pipeline = NULL;
	}
	s_pipeline_disp = NULL;

	/*
	 * Repoint DPU / restore LVGL layer (re-init vg_lite + lv_vendor_start) inside
	 * post_stop before freeing the cast GPU pool; otherwise scanout may still use a
	 * pool buffer address after free.
	 */
	s_cast_hooks.post_stop();

	cast_gpu_pool_free_all();

	rtos_unlock_mutex(&s_cast_ops_mutex);
	return BK_OK;
}

bk_err_t cast_jpeg_pipeline_suspend(void)
{
	return BK_OK;
}

bk_err_t cast_jpeg_pipeline_resume(void)
{
	return BK_OK;
}
