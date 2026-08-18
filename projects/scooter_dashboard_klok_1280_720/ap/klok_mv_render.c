#include "klok_mv_render.h"

#include "klok_h264_pp_osd.h"
#include "klok_lvgl_preview.h"

bk_err_t klok_mv_render_init(bk_display_ctlr_handle_t lcd_handle)
{
    return klok_h264_pp_osd_init(lcd_handle);
}

void klok_mv_render_prepare_locked(void)
{
    klok_h264_pp_osd_prepare_locked();
}

bk_err_t klok_mv_render_enter_locked(void)
{
    /*
     * The preview owns two RGB565 buffers. Release them before allocating the
     * larger PP-OSD snapshot buffer when returning to full-screen playback.
     */
    klok_lvgl_preview_release_locked();
    return klok_h264_pp_osd_enter_locked();
}

void klok_mv_render_leave_locked(void)
{
    klok_h264_pp_osd_leave_locked();
}

bool klok_mv_render_is_active(void)
{
    return klok_h264_pp_osd_is_active();
}

bool klok_mv_render_handle_lvgl_flush(void *frame_buffer, int (*free_cb)(void *args))
{
    return klok_h264_pp_osd_handle_lvgl_flush(frame_buffer, free_cb);
}

bool klok_mv_render_push_video_take(void *pixel,
                                   uint32_t pixel_format,
                                   uint16_t width,
                                   uint16_t height,
                                   uint64_t pts_ms)
{
    return klok_h264_pp_osd_push_video_take(pixel, pixel_format, width, height, pts_ms);
}

void klok_mv_render_overlay_dirty(void)
{
    klok_h264_pp_osd_overlay_dirty();
}
