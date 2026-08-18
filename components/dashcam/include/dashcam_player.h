#ifndef __DASHCAM_PLAYER_H__
#define __DASHCAM_PLAYER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "common/bk_err.h"

/* Snapshot of the clip currently being played, used to drive the playback
 * info overlay (req6 #4). All fields are 0 when unknown / not playing. */
typedef struct
{
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint64_t duration_ms;
    uint64_t position_ms;
    uint64_t file_size_bytes;
} dashcam_player_info_t;

bk_err_t dashcam_player_play(const char *path);

/* Stop decoding while keeping the engine open. Do not use this when returning
 * display ownership to LVGL; use dashcam_player_close() for that handoff. */
bk_err_t dashcam_player_stop(void);

/* Fully release the player engine (engine_stop -> close -> delete). This runs
 * decoder/GPU teardown and must complete before LVGL reacquires VG-Lite. */
bk_err_t dashcam_player_close(void);

bool dashcam_player_is_playing(void);

/* True while the engine is open (created but not yet dashcam_player_close()d). */
bool dashcam_player_is_open(void);

/* Fill static media info (resolution / duration / size) of the playing clip. */
bk_err_t dashcam_player_get_media_info(dashcam_player_info_t *info);
/* Current playback position in milliseconds (0 if unavailable). */
uint64_t dashcam_player_get_position_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_PLAYER_H__ */
