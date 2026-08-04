#ifndef __DASHCAM_ASSITVIEW_H__
#define __DASHCAM_ASSITVIEW_H__

#include <stdbool.h>

void dashcam_assitview_init(void);
void dashcam_assitview_deinit(void);
void dashcam_assitview_start(void);
void dashcam_assitview_stop(void);
bool dashcam_assitview_is_active(void);

#endif