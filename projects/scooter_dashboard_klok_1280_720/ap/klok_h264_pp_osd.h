#ifndef __KLOK_H264_PP_OSD_H__
#define __KLOK_H264_PP_OSD_H__

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_include.h>
#include <components/bk_display.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t klok_h264_pp_osd_init(bk_display_ctlr_handle_t lcd_handle);
void klok_h264_pp_osd_prepare_locked(void);
bk_err_t klok_h264_pp_osd_enter_locked(void);
void klok_h264_pp_osd_leave_locked(void);
bool klok_h264_pp_osd_is_active(void);
bool klok_h264_pp_osd_handle_lvgl_flush(void *frame_buffer, int (*free_cb)(void *args));
bool klok_h264_pp_osd_push_video_take(void *pixel,
                                     uint32_t pixel_format,
                                     uint16_t width,
                                     uint16_t height,
                                     uint64_t pts_ms);
void klok_h264_pp_osd_cancel_pending_frame(void);
void klok_h264_pp_osd_overlay_dirty(void);

#ifdef __cplusplus
}
#endif

#endif /* __KLOK_H264_PP_OSD_H__ */
