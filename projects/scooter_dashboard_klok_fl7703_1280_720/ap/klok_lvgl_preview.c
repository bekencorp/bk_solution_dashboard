#include "klok_lvgl_preview.h"

#include <common/bk_include.h>
#include <components/bk_frame_buffer.h>

#include "beken_ui.h"
#include "common/avdk_pixel_types.h"
#include "lv_vendor.h"
#include "lvgl.h"

#define KLOK_PREVIEW_WIDTH       (576U)
#define KLOK_PREVIEW_HEIGHT      (304U)
#define KLOK_FULLSCREEN_WIDTH    (1280U)
#define KLOK_FULLSCREEN_HEIGHT   (720U)
#define KLOK_PREVIEW_BUF_COUNT   (2U)

static uint8_t *s_preview_buf[KLOK_PREVIEW_BUF_COUNT];
static lv_image_dsc_t s_preview_dsc[KLOK_PREVIEW_BUF_COUNT];
static lv_obj_t *s_preview_image;
static uint8_t s_preview_index;
static uint16_t s_preview_width;
static uint16_t s_preview_height;
static bool s_preview_fullscreen;
static void *s_fullscreen_frame[KLOK_PREVIEW_BUF_COUNT];
static lv_image_dsc_t s_fullscreen_dsc[KLOK_PREVIEW_BUF_COUNT];
static uint8_t s_fullscreen_index;

static bool klok_preview_obj_valid(lv_obj_t *obj)
{
    return obj != NULL && lv_obj_is_valid(obj);
}

static void klok_preview_free_buffers_locked(void)
{
    for (uint32_t i = 0; i < KLOK_PREVIEW_BUF_COUNT; i++) {
        if (s_preview_buf[i] != NULL) {
            bk_frame_buffer_free(s_preview_buf[i]);
            s_preview_buf[i] = NULL;
        }
        s_preview_dsc[i].data = NULL;
        s_preview_dsc[i].data_size = 0U;
    }
    s_preview_width = 0U;
    s_preview_height = 0U;
    s_preview_index = 0U;
}

static bool klok_preview_ensure_buffers_locked(uint16_t width, uint16_t height)
{
    const uint32_t buffer_size = (uint32_t)width * height * 2U;

    if (s_preview_width != width || s_preview_height != height) {
        klok_preview_free_buffers_locked();
    }

    for (uint32_t i = 0; i < KLOK_PREVIEW_BUF_COUNT; i++) {
        if (s_preview_buf[i] != NULL) {
            continue;
        }

        s_preview_buf[i] = bk_frame_buffer_malloc(
            MEM_SLAB_HEAP_UNCODED,
            buffer_size);
        if (s_preview_buf[i] == NULL) {
            klok_preview_free_buffers_locked();
            return false;
        }

        s_preview_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_preview_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_preview_dsc[i].header.w = width;
        s_preview_dsc[i].header.h = height;
        s_preview_dsc[i].header.stride = width * 2U;
        s_preview_dsc[i].data_size = buffer_size;
        s_preview_dsc[i].data = s_preview_buf[i];
    }

    s_preview_width = width;
    s_preview_height = height;
    return true;
}

static bool klok_preview_ensure_image_locked(void)
{
    lv_obj_t *active = lv_screen_active();
    lv_obj_t *panel = NULL;
    bool fullscreen = false;

    if (active == bk_lv_tool_ui.song_list) {
        panel = bk_lv_tool_ui.song_list_sl_video;
    } else if (active == bk_lv_tool_ui.klok_main) {
        panel = bk_lv_tool_ui.klok_main_video_box;
    } else if (active == bk_lv_tool_ui.music_player) {
        panel = bk_lv_tool_ui.music_player;
        fullscreen = true;
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
    }

    lv_obj_set_pos(s_preview_image, 0, 0);
    if (fullscreen) {
        lv_obj_set_size(s_preview_image,
                        KLOK_FULLSCREEN_WIDTH,
                        KLOK_FULLSCREEN_HEIGHT);
        lv_image_set_inner_align(s_preview_image, LV_IMAGE_ALIGN_DEFAULT);
        lv_image_set_scale_x(s_preview_image, LV_SCALE_NONE);
        lv_image_set_scale_y(s_preview_image, LV_SCALE_NONE);
        lv_obj_set_style_radius(s_preview_image, 0, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(s_preview_image, false, LV_PART_MAIN);
        lv_obj_move_to_index(s_preview_image, 0);
    } else {
        lv_obj_set_size(s_preview_image,
                        KLOK_PREVIEW_WIDTH,
                        KLOK_PREVIEW_HEIGHT);
        lv_image_set_inner_align(s_preview_image, LV_IMAGE_ALIGN_DEFAULT);
        lv_image_set_scale_x(s_preview_image, LV_SCALE_NONE);
        lv_image_set_scale_y(s_preview_image, LV_SCALE_NONE);
        lv_obj_set_style_radius(s_preview_image,
                                12,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_clip_corner(s_preview_image,
                                     true,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    s_preview_fullscreen = fullscreen;
    return true;
}

static void klok_preview_rgb565_scale(const uint16_t *src,
                                      uint16_t src_width,
                                      uint16_t src_height,
                                      uint16_t *dst,
                                      uint16_t dst_width,
                                      uint16_t dst_height)
{
    if (src_width == dst_width && src_height == dst_height) {
        os_memcpy(dst, src, (uint32_t)dst_width * dst_height * 2U);
        return;
    }

    for (uint32_t y = 0; y < dst_height; y++) {
        uint32_t src_y = (y * src_height) / dst_height;
        const uint16_t *src_row = src + src_y * src_width;
        uint16_t *dst_row = dst + y * dst_width;

        for (uint32_t x = 0; x < dst_width; x++) {
            uint32_t src_x = (x * src_width) / dst_width;
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

    if (!klok_preview_ensure_image_locked()) {
        return false;
    }

    const uint16_t target_width =
        s_preview_fullscreen ? KLOK_FULLSCREEN_WIDTH : KLOK_PREVIEW_WIDTH;
    const uint16_t target_height =
        s_preview_fullscreen ? KLOK_FULLSCREEN_HEIGHT : KLOK_PREVIEW_HEIGHT;
    if (!klok_preview_ensure_buffers_locked(target_width, target_height)) {
        return false;
    }

    uint8_t index = s_preview_index;
    s_preview_index =
        (uint8_t)((s_preview_index + 1U) % KLOK_PREVIEW_BUF_COUNT);

    klok_preview_rgb565_scale(pixel,
                              width,
                              height,
                              (uint16_t *)s_preview_buf[index],
                              target_width,
                              target_height);
    lv_image_set_src(s_preview_image, &s_preview_dsc[index]);
    lv_obj_invalidate(s_preview_image);

    return true;
}

bool klok_lvgl_preview_present_fullscreen_take(void *pixel,
                                               uint32_t pixel_format,
                                               uint16_t width,
                                               uint16_t height)
{
    if (pixel == NULL ||
        pixel_format != PIXEL_FMT_RGB565 ||
        width != KLOK_FULLSCREEN_WIDTH ||
        height != KLOK_FULLSCREEN_HEIGHT ||
        !klok_preview_ensure_image_locked() ||
        !s_preview_fullscreen) {
        return false;
    }

    const uint8_t index = s_fullscreen_index;
    s_fullscreen_index =
        (uint8_t)((s_fullscreen_index + 1U) % KLOK_PREVIEW_BUF_COUNT);

    /*
     * Keep two decoded frames alive so LVGL never reads a frame while the
     * decoder reuses it. Replacing the source is zero-copy; the slot being
     * reused was displayed at least one full LVGL cycle earlier.
     */
    if (s_fullscreen_frame[index] != NULL) {
        bk_frame_buffer_free(s_fullscreen_frame[index]);
    }
    s_fullscreen_frame[index] = pixel;
    s_fullscreen_dsc[index].header.cf = LV_COLOR_FORMAT_RGB565;
    s_fullscreen_dsc[index].header.magic = LV_IMAGE_HEADER_MAGIC;
    s_fullscreen_dsc[index].header.w = width;
    s_fullscreen_dsc[index].header.h = height;
    s_fullscreen_dsc[index].header.stride = width * 2U;
    s_fullscreen_dsc[index].data_size = (uint32_t)width * height * 2U;
    s_fullscreen_dsc[index].data = pixel;

    lv_image_set_src(s_preview_image, &s_fullscreen_dsc[index]);
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
        if (s_fullscreen_frame[i] != NULL) {
            bk_frame_buffer_free(s_fullscreen_frame[i]);
            s_fullscreen_frame[i] = NULL;
        }
        s_fullscreen_dsc[i].data = NULL;
        s_fullscreen_dsc[i].data_size = 0U;
    }
    s_fullscreen_index = 0U;
    klok_preview_free_buffers_locked();
    s_preview_fullscreen = false;
}

void klok_lvgl_preview_clear(void)
{
    lv_vendor_disp_lock();
    klok_lvgl_preview_release_locked();
    lv_vendor_disp_unlock();
}
