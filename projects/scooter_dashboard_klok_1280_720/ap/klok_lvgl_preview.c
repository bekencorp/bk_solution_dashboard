#include "klok_lvgl_preview.h"

#include <common/bk_include.h>
#include <components/bk_frame_buffer.h>

#include "beken_ui.h"
#include "common/avdk_pixel_types.h"
#include "lv_vendor.h"
#include "lvgl.h"

#define KLOK_PREVIEW_WIDTH       (576U)
#define KLOK_PREVIEW_HEIGHT      (304U)
#define KLOK_PREVIEW_BUF_COUNT   (2U)
#define KLOK_PREVIEW_BUF_SIZE    (KLOK_PREVIEW_WIDTH * KLOK_PREVIEW_HEIGHT * 2U)

static uint8_t *s_preview_buf[KLOK_PREVIEW_BUF_COUNT];
static lv_image_dsc_t s_preview_dsc[KLOK_PREVIEW_BUF_COUNT];
static lv_obj_t *s_preview_image;
static uint8_t s_preview_index;

static bool klok_preview_obj_valid(lv_obj_t *obj)
{
    return obj != NULL && lv_obj_is_valid(obj);
}

static bool klok_preview_ensure_buffers_locked(void)
{
    for (uint32_t i = 0; i < KLOK_PREVIEW_BUF_COUNT; i++) {
        if (s_preview_buf[i] != NULL) {
            continue;
        }

        s_preview_buf[i] = bk_frame_buffer_malloc(
            MEM_SLAB_HEAP_UNCODED,
            KLOK_PREVIEW_BUF_SIZE);
        if (s_preview_buf[i] == NULL) {
            klok_lvgl_preview_release_locked();
            return false;
        }

        s_preview_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_preview_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_preview_dsc[i].header.w = KLOK_PREVIEW_WIDTH;
        s_preview_dsc[i].header.h = KLOK_PREVIEW_HEIGHT;
        s_preview_dsc[i].header.stride = KLOK_PREVIEW_WIDTH * 2U;
        s_preview_dsc[i].data_size = KLOK_PREVIEW_BUF_SIZE;
        s_preview_dsc[i].data = s_preview_buf[i];
    }

    return true;
}

static bool klok_preview_ensure_image_locked(void)
{
    lv_obj_t *active = lv_screen_active();
    lv_obj_t *panel = NULL;

    if (active == bk_lv_tool_ui.song_list) {
        panel = bk_lv_tool_ui.song_list_sl_video;
    } else if (active == bk_lv_tool_ui.klok_main) {
        panel = bk_lv_tool_ui.klok_main_video_box;
    }

    if (!klok_preview_obj_valid(panel)) {
        return false;
    }

    if (klok_preview_obj_valid(s_preview_image) &&
        lv_obj_get_parent(s_preview_image) != panel) {
        lv_image_set_src(s_preview_image, NULL);
        lv_obj_delete(s_preview_image);
        s_preview_image = NULL;
    }

    if (!klok_preview_obj_valid(s_preview_image)) {
        s_preview_image = lv_image_create(panel);
        lv_obj_set_pos(s_preview_image, 0, 0);
        lv_obj_set_size(s_preview_image,
                        KLOK_PREVIEW_WIDTH,
                        KLOK_PREVIEW_HEIGHT);
        lv_obj_set_style_radius(s_preview_image,
                                12,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_clip_corner(s_preview_image,
                                     true,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    return true;
}

static void klok_preview_rgb565_scale(const uint16_t *src,
                                      uint16_t src_width,
                                      uint16_t src_height,
                                      uint16_t *dst)
{
    for (uint32_t y = 0; y < KLOK_PREVIEW_HEIGHT; y++) {
        uint32_t src_y = (y * src_height) / KLOK_PREVIEW_HEIGHT;
        const uint16_t *src_row = src + src_y * src_width;
        uint16_t *dst_row = dst + y * KLOK_PREVIEW_WIDTH;

        for (uint32_t x = 0; x < KLOK_PREVIEW_WIDTH; x++) {
            uint32_t src_x = (x * src_width) / KLOK_PREVIEW_WIDTH;
            dst_row[x] = src_row[src_x];
        }
    }
}

bool klok_lvgl_preview_present(const void *pixel,
                               uint32_t pixel_format,
                               uint16_t width,
                               uint16_t height)
{
    if (pixel == NULL ||
        pixel_format != PIXEL_FMT_RGB565 ||
        width == 0U ||
        height == 0U) {
        return false;
    }

    if (!klok_preview_ensure_image_locked() ||
        !klok_preview_ensure_buffers_locked()) {
        return false;
    }

    uint8_t index = s_preview_index;
    s_preview_index =
        (uint8_t)((s_preview_index + 1U) % KLOK_PREVIEW_BUF_COUNT);

    klok_preview_rgb565_scale(pixel,
                              width,
                              height,
                              (uint16_t *)s_preview_buf[index]);
    lv_image_set_src(s_preview_image, &s_preview_dsc[index]);
    lv_obj_invalidate(s_preview_image);

    return true;
}

void klok_lvgl_preview_release_locked(void)
{
    if (klok_preview_obj_valid(s_preview_image)) {
        lv_image_set_src(s_preview_image, NULL);
        lv_obj_delete(s_preview_image);
    }
    s_preview_image = NULL;

    for (uint32_t i = 0; i < KLOK_PREVIEW_BUF_COUNT; i++) {
        if (s_preview_buf[i] != NULL) {
            bk_frame_buffer_free(s_preview_buf[i]);
            s_preview_buf[i] = NULL;
        }
        s_preview_dsc[i].data = NULL;
        s_preview_dsc[i].data_size = 0U;
    }
    s_preview_index = 0U;
}

void klok_lvgl_preview_clear(void)
{
    lv_vendor_disp_lock();
    klok_lvgl_preview_release_locked();
    lv_vendor_disp_unlock();
}
