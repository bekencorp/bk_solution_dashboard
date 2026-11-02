#pragma once

#include <common/bk_err.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t notification_popup_init(lv_font_t *font);
void notification_popup_show(const char *title, const char *message);

#ifdef __cplusplus
}
#endif
