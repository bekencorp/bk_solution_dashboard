#ifndef LV_CONF_OVERRIDE_H
#define LV_CONF_OVERRIDE_H

/*
 * Workaround: SDK Kconfig missing LV_USE_GIF / LV_USE_FS_FATFS entries.
 * Include the original lv_conf.h, then override the values we need.
 * TODO: Remove this file once SDK adds proper Kconfig entries.
 */

#include "lv_conf.h"

/*
 * Give LVGL its own pool instead of forwarding every lv_malloc() straight to
 * the system heap. The stock LV_STDLIB_CUSTOM has no pool at all, so
 * lv_mem_monitor() cannot report total_size, free_biggest_size or frag_pct -
 * and largest free block, not bytes used, is the number that actually hurts
 * here. display_ui_lv_pool_alloc() (components/display_ui) takes the whole block from HSRAM
 * or PSRAM per CONFIG_LVGL_MEM_USE_PSRAM.
 *
 * Kept here rather than in the SDK's lv_conf.h so an SDK update cannot silently
 * drop it, and so the other projects keep the stock allocator.
 *
 * LV_MEM_ADR and LV_MEM_POOL_EXPAND_SIZE are deliberately left alone:
 * lv_conf_internal.h defaults both to 0, which is what this needs, and
 * LV_MEM_ADR == 0 is also the condition under which it honours
 * LV_MEM_POOL_ALLOC at all.
 *
 * Lifecycle note: display_ui_deinit_lvgl() releases the pool through
 * display_ui_lv_pool_free() once lv_vendor_deinit() has run to completion, so the
 * whole LV_MEM_SIZE block returns to HSRAM and the next lv_init() takes a
 * fresh one that may well sit elsewhere. Every pointer
 * allocated before an lv_deinit() therefore dangles into memory the system
 * heap has reclaimed and re-issued (the H264 decoder ring and the GPU's
 * vg_lite heap are the intended recipients). Any module that caches an LVGL
 * allocation across the assist-view or clip-playback deinit/reinit cycle must
 * drop it in its *_reset_after_lvgl_deinit() (see beken_ui_after_lvgl_deinit).
 *
 * The pool is released rather than retained because keeping 170 KiB reserved
 * mid-heap leaves the rest of the freed HSRAM split in two, and the GPU needs
 * CONFIG_VG_LITE_GPU_CONTIGUOUS_MEM_SZ in one piece.
 */
#undef LV_USE_STDLIB_MALLOC
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN

#undef LV_MEM_SIZE
#define LV_MEM_SIZE (170 * 1024U)

#undef LV_MEM_POOL_INCLUDE
#define LV_MEM_POOL_INCLUDE "lv_mem_pool.h"

#undef LV_MEM_POOL_ALLOC
#define LV_MEM_POOL_ALLOC(size) display_ui_lv_pool_alloc(size)

#undef LV_USE_GIF
#define LV_USE_GIF 1

#undef LV_GIF_USE_PSRAM
#define LV_GIF_USE_PSRAM 1

#undef LV_USE_FS_FATFS
#define LV_USE_FS_FATFS 1

/*
 * Chinese glyphs are loaded at runtime from a TrueType file on the SD card
 * (S:/simhei_new.ttf) via LVGL's tiny_ttf (stb_truetype) engine, so no CJK
 * bitmap font is compiled into flash. Keep the built-in Source Han Sans SC CJK
 * subsets disabled (unreferenced).
 */
#undef LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 0

#undef LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 0

/* Runtime TrueType rendering for Chinese track metadata (see home_ui.c). The
 * .ttf is read once into PSRAM and built via lv_tiny_ttf_create_data, so the
 * file-streaming path is not needed and LV_TINY_TTF_FILE_SUPPORT stays off. */
#undef LV_USE_TINY_TTF
#define LV_USE_TINY_TTF 1

#undef LV_TINY_TTF_FILE_SUPPORT
#define LV_TINY_TTF_FILE_SUPPORT 0

/* No kerning cache. Every font here is CJK and is created with
 * LV_FONT_KERNING_NONE, so nothing should land in this cache - yet profiling
 * found 298 live entries holding 15KB, and worse, they were ~30-byte blocks
 * scattered across the pool: dropping them roughly doubled largest_free_block
 * (11KB -> 28KB), which mattered more than the bytes. 0 is a supported size,
 * not a trick: ttf_get_glyph_pair_kerning_width computes on the stack and
 * returns when max_size is 0. Raise this again if a font that actually needs
 * kerning is ever added. */
#undef LV_TINY_TTF_CACHE_KERNING_CNT
#define LV_TINY_TTF_CACHE_KERNING_CNT 0

#undef LV_FS_FATFS_LETTER
#define LV_FS_FATFS_LETTER 'S'

#undef LV_FS_FATFS_CACHE_SIZE
#define LV_FS_FATFS_CACHE_SIZE 512

/*
 * The SD card is FATFS physical drive 1 (DISK_NUMBER_SDIO_SD), but LVGL's
 * fsdrv strips only the "S:" prefix and passes the bare path to f_open(),
 * which would target the default drive 0. Prepend "1:" so "S:/foo" maps to
 * "1:/foo" on the SD card.
 */
#undef LV_FS_FATFS_PATH
#define LV_FS_FATFS_PATH "1:"

/* Enable the built-in TinyJPEG decoder so LVGL can read .jpg files directly
 * from the SD card (streaming MCU decode, no full-frame buffer). */
#undef LV_USE_TJPGD
#define LV_USE_TJPGD 1

/*
 * The effective stack is a QUARTER of this value: lv_thread_init() divides by
 * sizeof(StackType_t)=4, but the CONFIG_SOC_SMP path hands the result to
 * rtos_create_hsram_thread(), whose stack argument is in BYTES. So 16KB here
 * gives each swdraw thread 4KB, and LV_DRAW_SW_DRAW_UNIT_CNT=2 of them cost 8KB
 * of HSRAM. The stock 8KB default would leave 2KB and UsageFault.
 *
 * 4KB is tight on purpose, to keep those two stacks out of HSRAM, and it holds
 * only because no JPEG is ever decoded from a draw thread: TJPGD's
 * decoder_info() alone would put a 4KB work buffer on the stack. The home
 * background is decoded once into an RGB565 PSRAM bitmap - on the dedicated
 * "bg_preload" thread, which hands TJPGD a heap pool instead of a stack buffer,
 * or failing that on the UI thread in home_ui_install_bg() - and only that
 * bitmap is ever used as an image source. Raise this back to 64KB if a .jpg is
 * ever handed to an lv_image directly.
 *
 * Must be set here, not in defconfig: lv_conf.h hardcodes
 * LV_DRAW_THREAD_STACK_SIZE, so the Kconfig value is ignored.
 */
#undef LV_DRAW_THREAD_STACK_SIZE
#define LV_DRAW_THREAD_STACK_SIZE (16 * 1024)


#undef LV_USE_NATIVE_HELIUM_ASM
#define LV_USE_NATIVE_HELIUM_ASM 0

#undef LV_USE_DRAW_SW_ASM
#define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE

#endif /* LV_CONF_OVERRIDE_H */
