#ifndef __KLOK_MV_RENDER_H__
#define __KLOK_MV_RENDER_H__

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_include.h>
#include <components/bk_display.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t klok_mv_render_init(bk_display_ctlr_handle_t lcd_handle);
void klok_mv_render_use_music_overlay(bool enable);
void klok_mv_render_prepare_locked(void);
bk_err_t klok_mv_render_enter_locked(void);
bk_err_t klok_mv_render_enter_pp_locked(void);
void klok_mv_render_leave_locked(void);
bool klok_mv_render_is_active(void);
bool klok_mv_render_is_pp_active(void);
bool klok_mv_render_handle_lvgl_flush(void *frame_buffer, int (*free_cb)(void *args));
bool klok_mv_render_push_video_take(void *pixel,
                                   uint32_t pixel_format,
                                   uint16_t width,
                                   uint16_t height,
                                   uint64_t pts_ms);
void klok_mv_render_overlay_dirty(void);
void klok_mv_render_music_pose(uint8_t angle_index, int8_t offset_y);

#ifdef __cplusplus
}
#endif

#endif /* __KLOK_MV_RENDER_H__ */
