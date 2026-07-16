#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <common/bk_err.h>

bk_err_t display_ui_init(void);
bk_err_t display_ui_init_display_hw(void);
bk_err_t display_ui_start_lvgl(void);
void display_ui_register_cast_hooks_once(void);

uint8_t *lvgl_get_idle_framebuffer(int index, uint32_t *out_size);

/*
 * Page-scoped GPU compositing hook (see req5_design.md §13).
 * While attached, the LVGL flush copies each rendered frame into staging_buffer
 * (size = the LVGL framebuffer size) and sets *dirty_flag instead of driving the
 * DPU. The caller's CPU0 GPU worker composites staging+camera and pushes to DPU.
 * The copy is serialised against the worker's GPU read of the same buffer via
 * the GPU lock, so the CPU write never races the GPU read (that race wedged the
 * vg_lite command engine). Pass a buffer of at least the size reported by
 * display_ui_get_lvgl_dims().
 */
void display_ui_blend_attach(void *staging_buffer, uint32_t copy_size, volatile bool *dirty_flag);
void display_ui_blend_detach(void);
void display_ui_get_lvgl_dims(uint16_t *out_w, uint16_t *out_h, uint32_t *out_fb_size);

/* Push a composited frame straight to the DPU (used by the page-scoped GPU
 * compositor on its CPU0 worker). free_cb is invoked by the DPU when the frame
 * has been consumed. */
bk_err_t display_ui_blend_flush(void *frame_buffer, int (*free_cb)(void *args));
