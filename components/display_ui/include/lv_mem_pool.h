#ifndef LV_MEM_POOL_H
#define LV_MEM_POOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The single memory block that LVGL's builtin allocator builds its heap on.
 *
 * Only projects that set LV_USE_STDLIB_MALLOC to LV_STDLIB_BUILTIN need this;
 * the stock LV_STDLIB_CUSTOM forwards every lv_malloc() to the system heap and
 * takes no pool at all. It lives here rather than in the SDK's lv_mem_adapt.c
 * so that the pool policy - how big, which heap, released or retained across a
 * teardown - stays a product decision, and so an SDK update cannot silently
 * change it.
 *
 * Wiring, in ap/lv_conf_override.h:
 *
 *     #define LV_MEM_POOL_INCLUDE "lv_mem_pool.h"
 *     #define LV_MEM_POOL_ALLOC(size) display_ui_lv_pool_alloc(size)
 *
 * lv_mem_core_builtin.c is in the SDK and has no way to find this header on
 * its own, so ap/CMakeLists.txt adds this directory to __armino_lvgl's include
 * path next to where it sets LV_CONF_PATH.
 */

/*
 * Reserve the pool. Called by lv_mem_init() through LV_MEM_POOL_ALLOC, i.e.
 * once per lv_init().
 *
 * The heap is chosen by CONFIG_LVGL_MEM_USE_PSRAM. On HSRAM exhaustion it
 * falls back to PSRAM rather than returning NULL, because NULL is not
 * survivable here: lv_mem_init() hands the result straight to
 * lv_tlsf_create_with_pool(), whose lv_tlsf_create() accepts NULL and then
 * builds its control block at address 0. A degraded UI beats a fault whose
 * dump says nothing about the cause.
 *
 * Calling it again while a pool is already reserved returns the same block, so
 * that a project which never calls display_ui_lv_pool_free() keeps working
 * across an lv_deinit()/lv_init() pair. The size must match; a different
 * LV_MEM_SIZE against a live pool is a configuration error and returns NULL.
 */
void *display_ui_lv_pool_alloc(size_t size);

/*
 * Release the pool. Must run after lv_deinit() (which destroys the TLSF control
 * block living inside it) and before the next lv_init(), which reserves a fresh
 * block. display_ui_deinit_lvgl() is the only caller.
 *
 * Releasing rather than retaining is what makes a full LVGL teardown actually
 * hand its memory back: LV_MEM_SIZE is a single large block sitting in the
 * middle of the heap, so keeping it reserved leaves the freed space split in
 * two and starves large contiguous requests (the GPU's vg_lite heap) even when
 * the total free byte count looks ample.
 *
 * The trade-off: the next lv_init() has to win LV_MEM_SIZE contiguous bytes
 * back, and every pointer allocated from the old pool now dangles. Only tear
 * down when the memory is genuinely needed elsewhere, release that other user
 * before restoring LVGL, and make sure every module that caches an LVGL handle
 * drops it in its *_reset_after_lvgl_deinit().
 *
 * Safe to call when no pool is held; it returns immediately.
 */
void display_ui_lv_pool_free(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_MEM_POOL_H */
