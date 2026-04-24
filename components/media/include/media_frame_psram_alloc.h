/*
 * Declarations for psram-backed frame allocators (media_frame_psram_alloc.c).
 * Not part of components/bk_frame_buffer.h slab API.
 */
#pragma once

#include <stdint.h>
#include <common/avdk_pixel_types.h>

#ifdef __cplusplus
extern "C" {
#endif

frame_buffer_t *frame_buffer_encode_malloc(uint32_t size);
void frame_buffer_encode_free(frame_buffer_t *frame);
frame_buffer_t *frame_buffer_display_malloc(uint32_t size);
void frame_buffer_display_free(frame_buffer_t *frame);

#ifdef __cplusplus
}
#endif
