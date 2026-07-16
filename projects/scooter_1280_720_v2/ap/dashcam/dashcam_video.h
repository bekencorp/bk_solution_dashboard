#ifndef __DASHCAM_VIDEO_H__
#define __DASHCAM_VIDEO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "lvgl.h"
#include "common/bk_err.h"

typedef enum
{
    DASHCAM_VIDEO_SOURCE_FRONT_MIPI = 0,
    DASHCAM_VIDEO_SOURCE_REAR_MIPI,
    DASHCAM_VIDEO_SOURCE_COUNT,
} dashcam_video_source_t;

typedef enum
{
    DASHCAM_VIDEO_FRAME_FORMAT_RGB565 = 0,
    DASHCAM_VIDEO_FRAME_FORMAT_NV12,
} dashcam_video_frame_format_t;

bk_err_t dashcam_video_start(lv_obj_t *parent);
bk_err_t dashcam_video_start_sink(lv_obj_t *parent);
void dashcam_video_stop(void);
bk_err_t dashcam_video_switch_next(void);
dashcam_video_source_t dashcam_video_get_source(void);
void dashcam_video_on_frame(const void *frame, uint32_t width, uint32_t height, uint32_t format);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_VIDEO_H__ */
