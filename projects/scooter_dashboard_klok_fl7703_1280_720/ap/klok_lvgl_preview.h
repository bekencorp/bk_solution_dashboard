#ifndef __KLOK_LVGL_PREVIEW_H__
#define __KLOK_LVGL_PREVIEW_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool klok_lvgl_preview_present(const void *pixel,
                               uint32_t pixel_format,
                               uint16_t width,
                               uint16_t height);
bool klok_lvgl_preview_present_fullscreen_take(void *pixel,
                                               uint32_t pixel_format,
                                               uint16_t width,
                                               uint16_t height);
void klok_lvgl_preview_release_locked(void);
void klok_lvgl_preview_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __KLOK_LVGL_PREVIEW_H__ */
