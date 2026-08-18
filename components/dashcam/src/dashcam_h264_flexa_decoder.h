#ifndef DASHCAM_H264_FLEXA_DECODER_H
#define DASHCAM_H264_FLEXA_DECODER_H

#include "components/bk_video_player/bk_video_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif

video_player_video_decoder_ops_t *dashcam_get_h264_flexa_decoder_ops(void);
avdk_err_t dashcam_h264_flexa_decoder_free_output_frame(void *frame);

#ifdef __cplusplus
}
#endif

#endif /* DASHCAM_H264_FLEXA_DECODER_H */
