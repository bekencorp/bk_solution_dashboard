#include "klok_h264_pp_osd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <components/bk_frame_buffer.h>
#include <components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h>
#include <os/mem.h>
#include <os/os.h>

#include "beken_ui.h"
#include "common/avdk_pixel_types.h"
#include "lv_vendor.h"
#include "lvgl.h"
#include "video_play_callbacks.h"
#include "video_play_gpu_postprocess.h"

#define TAG "klok_pp_osd"

#define KLOK_OSD_UI_WIDTH             (1280U)
#define KLOK_OSD_UI_HEIGHT            (720U)
#define KLOK_OSD_OVERLAY_HEIGHT       (280U)
#define KLOK_OSD_BUFFER_SIZE          (KLOK_OSD_UI_WIDTH * KLOK_OSD_OVERLAY_HEIGHT * 4U)
#define KLOK_OSD_OUTPUT_SLOTS         (2U)
#define KLOK_OSD_FRAME_DONE_TIMEOUT_MS (3000U)

typedef struct {
    volatile bool used;
    void *frame_buffer;
    avdk_err_t (*free_cb)(void *frame);
} klok_pp_osd_output_slot_t;

typedef struct {
    bk_display_ctlr_handle_t lcd_handle;
    beken_mutex_t mutex;
    beken_semaphore_t idle_sem;
    beken_semaphore_t frame_done_sem;
    volatile bool active;
    volatile bool entering;
    bool stopping;
    bool display_refresh_paused;
    uint32_t osd_users;
    uint32_t render_users;
    bool decoded_frame_pending;
    uint32_t pending_cancel_count;
    uint32_t pending_timeout_count;

    lv_obj_t *overlay_obj;
    lv_timer_t *overlay_timer;
    lv_draw_buf_t overlay_draw_buf;
    void *overlay_buffer;
    bool overlay_ready;
    bool overlay_updating;
    volatile bool overlay_dirty;

    klok_pp_osd_output_slot_t output_slots[KLOK_OSD_OUTPUT_SLOTS];
} klok_pp_osd_ctx_t;

static klok_pp_osd_ctx_t s_osd;

static void klok_pp_osd_pause_display_refresh_locked(void)
{
    if (s_osd.display_refresh_paused) {
        return;
    }

    lv_timer_t *refresh_timer = lv_display_get_refr_timer(NULL);
    if (refresh_timer != NULL) {
        /*
         * PP-OSD owns DPU output while active. Keep LVGL input/timers running,
         * but stop its compressed flush from submitting concurrent VG-Lite
         * work against the video postprocess worker.
         */
        lv_timer_pause(refresh_timer);
        s_osd.display_refresh_paused = true;
    }
}

static void klok_pp_osd_resume_display_refresh_locked(void)
{
    if (!s_osd.display_refresh_paused) {
        return;
    }

    lv_timer_t *refresh_timer = lv_display_get_refr_timer(NULL);
    if (refresh_timer != NULL) {
        lv_timer_resume(refresh_timer);
        lv_obj_t *screen = lv_screen_active();
        if (screen != NULL) {
            lv_obj_invalidate(screen);
        }
        lv_timer_ready(refresh_timer);
    }
    s_osd.display_refresh_paused = false;
}

static bool klok_pp_osd_obj_valid(lv_obj_t *obj)
{
    return obj != NULL && lv_obj_is_valid(obj);
}

static void klok_pp_osd_notify_idle_locked(void)
{
    if (s_osd.stopping &&
        s_osd.osd_users == 0U &&
        s_osd.render_users == 0U &&
        s_osd.idle_sem != NULL) {
        (void)rtos_set_semaphore(&s_osd.idle_sem);
    }
}

static void klok_pp_osd_complete_pending_frame_locked(void)
{
    if (!s_osd.decoded_frame_pending) {
        return;
    }

    s_osd.decoded_frame_pending = false;
    if (s_osd.frame_done_sem != NULL) {
        (void)rtos_set_semaphore(&s_osd.frame_done_sem);
    }
}

static bool klok_pp_osd_provider_acquire(bk_h264_decode_osd_t *osd,
                                        void **token,
                                        void *user_data)
{
    klok_pp_osd_ctx_t *ctx = (klok_pp_osd_ctx_t *)user_data;
    if (ctx == NULL || osd == NULL || token == NULL || ctx->mutex == NULL) {
        return false;
    }

    for (;;) {
        /*
         * Do not start the next PP decode while VG-Lite is still reading the
         * previous fused RGB565 output. The frame decoder and display worker
         * otherwise run concurrently and can overlap the VCDEC PP and GPU
         * full-frame transactions.
         */
        rtos_lock_mutex(&ctx->mutex);
        if (!ctx->active || ctx->stopping || !ctx->overlay_ready ||
            ctx->overlay_updating || ctx->overlay_buffer == NULL) {
            rtos_unlock_mutex(&ctx->mutex);
            return false;
        }
        if (!ctx->decoded_frame_pending) {
            break;
        }
        rtos_unlock_mutex(&ctx->mutex);
        if (rtos_get_semaphore(&ctx->frame_done_sem,
                               KLOK_OSD_FRAME_DONE_TIMEOUT_MS) != BK_OK) {
            uint32_t timeout_count;
            rtos_lock_mutex(&ctx->mutex);
            timeout_count = ++ctx->pending_timeout_count;
            rtos_unlock_mutex(&ctx->mutex);
            BK_LOGW(TAG,
                    "OSD frame completion timeout #%u; keep waiting for owner\n",
                    (unsigned)timeout_count);
            continue;
        }
    }

    ctx->osd_users++;
    os_memset(osd, 0, sizeof(*osd));
    /*
     * Snapshot generation holds this mutex while writing the single OSD
     * buffer. Once acquired, the buffer remains pinned through decode.
     */
    osd->osd[0].enable = 1U;
    osd->osd[0].originX = 0;
    osd->osd[0].originY = 0;
    osd->osd[0].width = KLOK_OSD_UI_WIDTH;
    osd->osd[0].height = KLOK_OSD_OVERLAY_HEIGHT;
    osd->osd[0].alphaBlendEna = 1U;
    osd->osd[0].blendComponentBase = (uint8_t *)ctx->overlay_buffer;
    osd->osd[0].blendOriginX = 0;
    osd->osd[0].blendOriginY = 0;
    osd->osd[0].blendWidth = KLOK_OSD_UI_WIDTH;
    osd->osd[0].blendHeight = KLOK_OSD_OVERLAY_HEIGHT;
    *token = ctx->overlay_buffer;
    rtos_unlock_mutex(&ctx->mutex);
    return true;
}

static void klok_pp_osd_provider_release(void *token,
                                        bool frame_ready,
                                        void *user_data)
{
    klok_pp_osd_ctx_t *ctx = (klok_pp_osd_ctx_t *)user_data;
    (void)token;

    if (ctx == NULL || ctx->mutex == NULL) {
        return;
    }

    rtos_lock_mutex(&ctx->mutex);
    if (ctx->osd_users != 0U) {
        ctx->osd_users--;
    }
    if (frame_ready && ctx->active && !ctx->stopping) {
        while (ctx->frame_done_sem != NULL &&
               rtos_get_semaphore(&ctx->frame_done_sem, BEKEN_NO_WAIT) == BK_OK) {
        }
        ctx->decoded_frame_pending = true;
    }
    klok_pp_osd_notify_idle_locked();
    rtos_unlock_mutex(&ctx->mutex);
}

static int klok_pp_osd_output_done_cb(void *args)
{
    avdk_err_t (*free_cb)(void *frame) = NULL;

    /* DPU completion runs in ISR context, so this table is lock-free. */
    for (uint32_t i = 0; i < KLOK_OSD_OUTPUT_SLOTS; i++) {
        if (s_osd.output_slots[i].used &&
            s_osd.output_slots[i].frame_buffer == args) {
            free_cb = s_osd.output_slots[i].free_cb;
            s_osd.output_slots[i].used = false;
            s_osd.output_slots[i].frame_buffer = NULL;
            s_osd.output_slots[i].free_cb = NULL;
            break;
        }
    }

    return free_cb != NULL ? free_cb(args) : BK_OK;
}

static bk_err_t klok_pp_osd_display_compressed_frame(void *frame_buffer)
{
    int slot = -1;

    for (uint32_t i = 0; i < KLOK_OSD_OUTPUT_SLOTS; i++) {
        if (!s_osd.output_slots[i].used) {
            s_osd.output_slots[i].frame_buffer = frame_buffer;
            s_osd.output_slots[i].free_cb = video_play_gpu_postprocess_free_frame;
            s_osd.output_slots[i].used = true;
            slot = (int)i;
            break;
        }
    }

    if (slot < 0) {
        (void)video_play_gpu_postprocess_free_frame(frame_buffer);
        return BK_FAIL;
    }

    avdk_err_t ret = bk_display_flush(s_osd.lcd_handle,
                                      frame_buffer,
                                      klok_pp_osd_output_done_cb);
    if (ret != AVDK_ERR_OK && s_osd.output_slots[slot].used) {
        s_osd.output_slots[slot].used = false;
        s_osd.output_slots[slot].frame_buffer = NULL;
        s_osd.output_slots[slot].free_cb = NULL;
        (void)video_play_gpu_postprocess_free_frame(frame_buffer);
        return BK_FAIL;
    }

    /* A synchronous callback may already have consumed the frame on failure. */
    return (ret == AVDK_ERR_OK || !s_osd.output_slots[slot].used) ? BK_OK : BK_FAIL;
}

static void klok_pp_osd_mark_dirty_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED ||
        code == LV_EVENT_RELEASED ||
        code == LV_EVENT_CLICKED ||
        code == LV_EVENT_VALUE_CHANGED ||
        code == LV_EVENT_STYLE_CHANGED ||
        code == LV_EVENT_SIZE_CHANGED ||
        code == LV_EVENT_CHILD_CHANGED) {
        s_osd.overlay_dirty = true;
    }
}

static void klok_pp_osd_attach_overlay_child(lv_obj_t *obj)
{
    if (!klok_pp_osd_obj_valid(obj) ||
        !klok_pp_osd_obj_valid(s_osd.overlay_obj) ||
        lv_obj_get_parent(obj) == s_osd.overlay_obj) {
        return;
    }

    lv_obj_set_parent(obj, s_osd.overlay_obj);
    lv_obj_add_event_cb(obj, klok_pp_osd_mark_dirty_event_cb, LV_EVENT_ALL, NULL);
}

static bk_err_t klok_pp_osd_prepare_overlay_object_locked(void)
{
    lv_obj_t *root = bk_lv_tool_ui.mv_play_mv_root;
    if (!klok_pp_osd_obj_valid(root) ||
        !klok_pp_osd_obj_valid(bk_lv_tool_ui.mv_play_video_img)) {
        return BK_ERR_NOT_INIT;
    }

    if (!klok_pp_osd_obj_valid(s_osd.overlay_obj)) {
        s_osd.overlay_obj = lv_obj_create(root);
        if (s_osd.overlay_obj == NULL) {
            return BK_ERR_NO_MEM;
        }
        lv_obj_set_pos(s_osd.overlay_obj, 0, 0);
        lv_obj_set_size(s_osd.overlay_obj,
                        KLOK_OSD_UI_WIDTH,
                        KLOK_OSD_OVERLAY_HEIGHT);
        lv_obj_set_style_bg_opa(s_osd.overlay_obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_osd.overlay_obj, 0, 0);
        lv_obj_set_style_pad_all(s_osd.overlay_obj, 0, 0);
        lv_obj_set_style_radius(s_osd.overlay_obj, 0, 0);
        lv_obj_remove_flag(s_osd.overlay_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_osd.overlay_obj, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *children[] = {
        bk_lv_tool_ui.mv_play_back_btn,
        bk_lv_tool_ui.mv_play_qr_card,
        bk_lv_tool_ui.mv_play_ctrl_next,
        bk_lv_tool_ui.mv_play_ctrl_pause,
        bk_lv_tool_ui.mv_play_ctrl_replay,
        bk_lv_tool_ui.mv_play_ctrl_duet,
        bk_lv_tool_ui.mv_play_ctrl_queue,
        bk_lv_tool_ui.mv_play_next_ic,
        bk_lv_tool_ui.mv_play_next_txt,
        bk_lv_tool_ui.mv_play_pause_ic,
        bk_lv_tool_ui.mv_play_pause_txt,
        bk_lv_tool_ui.mv_play_replay_ic,
        bk_lv_tool_ui.mv_play_replay_txt,
        bk_lv_tool_ui.mv_play_duet_ic,
        bk_lv_tool_ui.mv_play_duet_txt,
        bk_lv_tool_ui.mv_play_queue_ic,
        bk_lv_tool_ui.mv_play_queue_txt,
        bk_lv_tool_ui.mv_play_q_badge,
    };

    for (uint32_t i = 0; i < sizeof(children) / sizeof(children[0]); i++) {
        klok_pp_osd_attach_overlay_child(children[i]);
    }

    lv_obj_add_flag(bk_lv_tool_ui.mv_play_video_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_osd.overlay_obj);
    s_osd.overlay_dirty = true;
    return BK_OK;
}

static bk_err_t klok_pp_osd_allocate_buffer(void)
{
#if LV_USE_SNAPSHOT
    if (s_osd.overlay_buffer != NULL) {
        return BK_OK;
    }

    s_osd.overlay_buffer =
        bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, KLOK_OSD_BUFFER_SIZE);
    if (s_osd.overlay_buffer == NULL) {
        return BK_ERR_NO_MEM;
    }
    os_memset(s_osd.overlay_buffer, 0, KLOK_OSD_BUFFER_SIZE);

    if (lv_draw_buf_init(&s_osd.overlay_draw_buf,
                         KLOK_OSD_UI_WIDTH,
                         KLOK_OSD_OVERLAY_HEIGHT,
                         LV_COLOR_FORMAT_ARGB8888,
                         KLOK_OSD_UI_WIDTH * 4U,
                         s_osd.overlay_buffer,
                         KLOK_OSD_BUFFER_SIZE) != LV_RESULT_OK) {
        bk_frame_buffer_free(s_osd.overlay_buffer);
        s_osd.overlay_buffer = NULL;
        return BK_FAIL;
    }
    return BK_OK;
#else
    return BK_ERR_NOT_SUPPORT;
#endif
}

static void klok_pp_osd_free_buffer(void)
{
    if (s_osd.overlay_buffer != NULL) {
        bk_frame_buffer_free(s_osd.overlay_buffer);
        s_osd.overlay_buffer = NULL;
    }
    os_memset(&s_osd.overlay_draw_buf, 0, sizeof(s_osd.overlay_draw_buf));
}

static bk_err_t klok_pp_osd_refresh_overlay_locked(void)
{
#if LV_USE_SNAPSHOT
    if (!s_osd.overlay_dirty ||
        !klok_pp_osd_obj_valid(s_osd.overlay_obj) ||
        s_osd.overlay_buffer == NULL) {
        return BK_OK;
    }

    rtos_lock_mutex(&s_osd.mutex);
    if (s_osd.overlay_updating || s_osd.osd_users != 0U || s_osd.stopping) {
        rtos_unlock_mutex(&s_osd.mutex);
        return BK_OK;
    }

    s_osd.overlay_updating = true;
    s_osd.overlay_dirty = false;

    /*
     * This function runs from the LVGL thread while its display lock is
     * already held. Taking lv_vendor_disp_lock() again deadlocks before the
     * snapshot starts.
     */
    bool gpu_locked = lv_vendor_gpu_lock();
    lv_result_t result = lv_snapshot_take_to_draw_buf(s_osd.overlay_obj,
                                                       LV_COLOR_FORMAT_ARGB8888,
                                                       &s_osd.overlay_draw_buf);
    lv_vendor_gpu_unlock(gpu_locked);

    s_osd.overlay_updating = false;
    if (result == LV_RESULT_OK) {
        s_osd.overlay_ready = true;
    } else {
        s_osd.overlay_dirty = true;
    }
    rtos_unlock_mutex(&s_osd.mutex);

    return result == LV_RESULT_OK ? BK_OK : BK_FAIL;
#else
    return BK_ERR_NOT_SUPPORT;
#endif
}

static void klok_pp_osd_overlay_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_osd.active) {
        (void)klok_pp_osd_refresh_overlay_locked();
    }

    /*
     * VG-Lite and the preview image are owned by LVGL. Keep consuming video
     * frames on this thread after leaving full-screen playback so the song-list
     * preview continues to update.
     */
    video_play_display_process_one();
}

bk_err_t klok_h264_pp_osd_init(bk_display_ctlr_handle_t lcd_handle)
{
    if (lcd_handle == NULL) {
        return BK_ERR_PARAM;
    }
    if (s_osd.mutex != NULL) {
        s_osd.lcd_handle = lcd_handle;
        return BK_OK;
    }

    os_memset(&s_osd, 0, sizeof(s_osd));
    s_osd.lcd_handle = lcd_handle;

    bk_err_t ret = rtos_init_mutex(&s_osd.mutex);
    if (ret != BK_OK) {
        return ret;
    }
    ret = rtos_init_semaphore_ex(&s_osd.idle_sem, 1, 0);
    if (ret != BK_OK) {
        rtos_deinit_mutex(&s_osd.mutex);
        s_osd.mutex = NULL;
        return ret;
    }
    ret = rtos_init_semaphore_ex(&s_osd.frame_done_sem, 1, 0);
    if (ret != BK_OK) {
        rtos_deinit_semaphore(&s_osd.idle_sem);
        rtos_deinit_mutex(&s_osd.mutex);
        s_osd.idle_sem = NULL;
        s_osd.mutex = NULL;
        return ret;
    }
    if (video_play_gpu_postprocess_reserve_lvgl_context() != AVDK_ERR_OK) {
        rtos_deinit_semaphore(&s_osd.frame_done_sem);
        rtos_deinit_semaphore(&s_osd.idle_sem);
        rtos_deinit_mutex(&s_osd.mutex);
        s_osd.frame_done_sem = NULL;
        s_osd.idle_sem = NULL;
        s_osd.mutex = NULL;
        return BK_ERR_NO_MEM;
    }

    const bk_video_player_h264_osd_provider_t provider = {
        .acquire = klok_pp_osd_provider_acquire,
        .release = klok_pp_osd_provider_release,
        .user_data = &s_osd,
    };
    if (bk_video_player_hw_h264_frame_decoder_set_osd_provider(&provider) != AVDK_ERR_OK) {
        rtos_deinit_semaphore(&s_osd.frame_done_sem);
        rtos_deinit_semaphore(&s_osd.idle_sem);
        rtos_deinit_mutex(&s_osd.mutex);
        s_osd.frame_done_sem = NULL;
        s_osd.idle_sem = NULL;
        s_osd.mutex = NULL;
        return BK_FAIL;
    }

    return BK_OK;
}

void klok_h264_pp_osd_prepare_locked(void)
{
    if (s_osd.mutex == NULL) {
        return;
    }

    rtos_lock_mutex(&s_osd.mutex);
    if (!s_osd.active) {
        s_osd.entering = true;
    }
    rtos_unlock_mutex(&s_osd.mutex);
}

bk_err_t klok_h264_pp_osd_enter_locked(void)
{
    if (s_osd.lcd_handle == NULL || s_osd.mutex == NULL) {
        return BK_ERR_NOT_INIT;
    }

    rtos_lock_mutex(&s_osd.mutex);
    if (s_osd.active) {
        s_osd.entering = false;
        s_osd.overlay_dirty = true;
        rtos_unlock_mutex(&s_osd.mutex);
        return BK_OK;
    }
    rtos_unlock_mutex(&s_osd.mutex);

    bk_err_t ret = klok_pp_osd_allocate_buffer();
    if (ret != BK_OK) {
        s_osd.entering = false;
        BK_LOGE(TAG, "OSD buffer allocation failed, ret=%d\n", ret);
        return ret;
    }
    ret = klok_pp_osd_prepare_overlay_object_locked();
    if (ret != BK_OK) {
        klok_pp_osd_free_buffer();
        s_osd.entering = false;
        BK_LOGE(TAG, "OSD object preparation failed, ret=%d\n", ret);
        return ret;
    }

    ret = klok_pp_osd_refresh_overlay_locked();
    if (ret != BK_OK) {
        klok_pp_osd_free_buffer();
        if (klok_pp_osd_obj_valid(bk_lv_tool_ui.mv_play_video_img)) {
            lv_obj_remove_flag(bk_lv_tool_ui.mv_play_video_img, LV_OBJ_FLAG_HIDDEN);
        }
        s_osd.entering = false;
        BK_LOGE(TAG, "initial OSD snapshot failed, ret=%d\n", ret);
        return ret;
    }
    klok_pp_osd_pause_display_refresh_locked();

    rtos_lock_mutex(&s_osd.mutex);
    s_osd.stopping = false;
    s_osd.active = true;
    s_osd.entering = false;
    s_osd.decoded_frame_pending = false;
    while (rtos_get_semaphore(&s_osd.frame_done_sem, BEKEN_NO_WAIT) == BK_OK) {
    }
    rtos_unlock_mutex(&s_osd.mutex);

    if (s_osd.overlay_timer == NULL) {
        s_osd.overlay_timer = lv_timer_create(klok_pp_osd_overlay_timer_cb, 33, NULL);
    } else {
        lv_timer_resume(s_osd.overlay_timer);
    }

    return BK_OK;
}

void klok_h264_pp_osd_leave_locked(void)
{
    bool wait_for_idle = false;

    if (s_osd.mutex == NULL) {
        return;
    }
    rtos_lock_mutex(&s_osd.mutex);
    s_osd.entering = false;
    if (!s_osd.active && !s_osd.stopping && s_osd.overlay_buffer == NULL) {
        rtos_unlock_mutex(&s_osd.mutex);
        return;
    }
    s_osd.active = false;
    s_osd.stopping = true;
    klok_pp_osd_complete_pending_frame_locked();
    wait_for_idle = s_osd.osd_users != 0U || s_osd.render_users != 0U;
    rtos_unlock_mutex(&s_osd.mutex);

    if (wait_for_idle) {
        (void)rtos_get_semaphore(&s_osd.idle_sem, BEKEN_WAIT_FOREVER);
    }

    rtos_lock_mutex(&s_osd.mutex);
    s_osd.overlay_ready = false;
    s_osd.overlay_updating = false;
    s_osd.overlay_dirty = false;
    s_osd.stopping = false;
    rtos_unlock_mutex(&s_osd.mutex);

    klok_pp_osd_free_buffer();

    if (klok_pp_osd_obj_valid(bk_lv_tool_ui.mv_play_video_img)) {
        lv_obj_remove_flag(bk_lv_tool_ui.mv_play_video_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (klok_pp_osd_obj_valid(s_osd.overlay_obj)) {
        /*
         * The generated song-list page is close to the LVGL heap limit.
         * Release the detached MV controls before that page is constructed;
         * init_page_mv_play recreates them on the next entry.
         */
        lv_obj_delete(s_osd.overlay_obj);
    }
    s_osd.overlay_obj = NULL;
    klok_pp_osd_resume_display_refresh_locked();
}

bool klok_h264_pp_osd_is_active(void)
{
    bool active = false;
    if (s_osd.mutex != NULL) {
        rtos_lock_mutex(&s_osd.mutex);
        active = s_osd.active && !s_osd.stopping;
        rtos_unlock_mutex(&s_osd.mutex);
    }
    return active;
}

bool klok_h264_pp_osd_handle_lvgl_flush(void *frame_buffer, int (*free_cb)(void *args))
{
    bool suppress = false;

    if (frame_buffer == NULL || s_osd.mutex == NULL) {
        return false;
    }

    rtos_lock_mutex(&s_osd.mutex);
    suppress = s_osd.entering || (s_osd.active && !s_osd.stopping);
    rtos_unlock_mutex(&s_osd.mutex);
    if (!suppress) {
        return false;
    }

    if (free_cb != NULL) {
        free_cb(frame_buffer);
    }
    return true;
}

bool klok_h264_pp_osd_push_video_take(void *pixel,
                                     uint32_t pixel_format,
                                     uint16_t width,
                                     uint16_t height,
                                     uint64_t pts_ms)
{
    if (pixel == NULL ||
        pixel_format != PIXEL_FMT_RGB565 ||
        width != KLOK_OSD_UI_WIDTH ||
        height != KLOK_OSD_UI_HEIGHT ||
        s_osd.mutex == NULL) {
        return false;
    }

    rtos_lock_mutex(&s_osd.mutex);
    if (!s_osd.active || s_osd.stopping) {
        rtos_unlock_mutex(&s_osd.mutex);
        return false;
    }
    s_osd.render_users++;
    rtos_unlock_mutex(&s_osd.mutex);

    video_play_gpu_postprocess_frame_t output;
    os_memset(&output, 0, sizeof(output));

    bool gpu_locked = lv_vendor_gpu_lock();
    /*
     * VCDEC PP's packed 565 output is observed as B5:G6:R5 by VG-Lite.
     * Describe that layout explicitly so red and blue are not exchanged.
     */
    avdk_err_t ret = video_play_gpu_postprocess_bgr565_rotate(
        (const uint8_t *)pixel,
        width,
        height,
        (uint32_t)width * 2U,
        false,
        VIDEO_PLAY_ROTATE_90,
        &output);
    lv_vendor_gpu_unlock(gpu_locked);
    bk_frame_buffer_free(pixel);

    bool output_allowed = false;
    rtos_lock_mutex(&s_osd.mutex);
    klok_pp_osd_complete_pending_frame_locked();
    if (s_osd.render_users != 0U) {
        s_osd.render_users--;
    }
    output_allowed = s_osd.active && !s_osd.stopping;
    klok_pp_osd_notify_idle_locked();
    rtos_unlock_mutex(&s_osd.mutex);

    if (ret != AVDK_ERR_OK || output.data == NULL) {
        BK_LOGE(TAG, "RGB565 rotate/compress failed, ret=%d\n", ret);
        return true;
    }
    if (!output_allowed) {
        (void)video_play_gpu_postprocess_free_frame(output.data);
        return true;
    }

    (void)pts_ms;
    bk_err_t display_ret = klok_pp_osd_display_compressed_frame(output.data);

    if (display_ret != BK_OK) {
        BK_LOGW(TAG, "display submit failed, ret=%d\n", (int)display_ret);
    }
    return true;
}

void klok_h264_pp_osd_cancel_pending_frame(void)
{
    uint32_t cancel_count = 0U;

    if (s_osd.mutex == NULL) {
        return;
    }

    rtos_lock_mutex(&s_osd.mutex);
    if (s_osd.decoded_frame_pending) {
        klok_pp_osd_complete_pending_frame_locked();
        cancel_count = ++s_osd.pending_cancel_count;
    }
    rtos_unlock_mutex(&s_osd.mutex);

    if (cancel_count != 0U &&
        (cancel_count <= 5U || (cancel_count % 50U) == 0U)) {
        BK_LOGW(TAG,
                "canceled dropped OSD frame #%u\n",
                (unsigned)cancel_count);
    }
}

void klok_h264_pp_osd_overlay_dirty(void)
{
    s_osd.overlay_dirty = true;
}
