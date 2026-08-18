#ifndef __DASHCAM_GPU_POSTPROCESS_H__
#define __DASHCAM_GPU_POSTPROCESS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "common/bk_err.h"

bk_err_t dashcam_gpu_postprocess_nv12(const uint8_t *nv12,
                                      uint32_t src_width,
                                      uint32_t src_height,
                                      uint32_t dst_width,
                                      uint32_t dst_height,
                                      uint32_t rotation,
                                      void **out_frame);

void dashcam_gpu_postprocess_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_GPU_POSTPROCESS_H__ */
