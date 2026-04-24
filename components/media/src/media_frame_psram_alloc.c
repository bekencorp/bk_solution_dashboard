/*
 * PSRAM/HSRAM-backed frame_buffer_t allocators for media_frame_queue (MJPEG/H264 path).
 */

#include <stdint.h>
#include <string.h>
#include <os/mem.h>
#include <os/os.h>
#include <components/log.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_frame_buffer.h>

#define TAG "fb-alloc"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

frame_buffer_t *frame_buffer_encode_malloc(uint32_t size)
{
    uint32_t total = sizeof(frame_buffer_t) + size + 32;
    frame_buffer_t *fb = (frame_buffer_t *)psram_malloc(total);
    if (!fb) {
        fb = (frame_buffer_t *)hsram_malloc(total);
    }
    if (!fb) {
        LOGE("encode_malloc %u failed\n", (unsigned)size);
        return NULL;
    }
    os_memset(fb, 0, sizeof(frame_buffer_t));
    fb->frame = (uint8_t *)(fb + 1);
    fb->size = size;
    return fb;
}

void frame_buffer_encode_free(frame_buffer_t *frame)
{
    if (frame) {
        psram_free(frame);
    }
}

frame_buffer_t *frame_buffer_display_malloc(uint32_t size)
{
    uint32_t total = sizeof(frame_buffer_t) + size + 32;
    frame_buffer_t *fb = (frame_buffer_t *)psram_malloc(total);
    if (!fb) {
        fb = (frame_buffer_t *)hsram_malloc(total);
    }
    if (!fb) {
        LOGE("display_malloc %u failed\n", (unsigned)size);
        return NULL;
    }
    os_memset(fb, 0, sizeof(frame_buffer_t));
    fb->frame = (uint8_t *)(fb + 1);
    fb->size = size;
    return fb;
}

void frame_buffer_display_free(frame_buffer_t *frame)
{
    if (frame) {
        psram_free(frame);
    }
}
