#ifndef __DASHCAM_ASSITVIEW_H__
#define __DASHCAM_ASSITVIEW_H__

#include <stdbool.h>

typedef struct
{
    void (*before_lvgl_teardown)(void);
    void (*after_display_resume)(void);
} dashcam_assitview_hooks_t;

void dashcam_assitview_register_hooks(const dashcam_assitview_hooks_t *hooks);
void dashcam_assitview_init(void);
void dashcam_assitview_deinit(void);
void dashcam_assitview_start(void);
void dashcam_assitview_stop(void);
bool dashcam_assitview_is_active(void);

#endif