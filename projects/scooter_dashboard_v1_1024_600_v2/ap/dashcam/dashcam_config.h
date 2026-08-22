#ifndef __DASHCAM_CONFIG_H__
#define __DASHCAM_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dashcam build-time configuration.
 *
 * Review decisions (req5 §9):
 *  - Power-on continuous recording, segmented.
 *  - Dev/test build: 1-minute segments, capped at a small file count to avoid
 *    filling the SD card. Release build: ring/circular overwrite (oldest first).
 *  - Record resolution selected by macro; test phase reuses the dashcam live
 *    preview resolution.
 *  - Timestamp source: dev uses plan B (sequence + duration/size, uptime based),
 *    release uses plan A (SNTP/RTC wall clock). Controlled by DASHCAM_USE_WALL_CLOCK.
 *  - Audio recording requested; gated behind DASHCAM_RECORD_AUDIO until the ADK
 *    onboard-mic pipeline is linked into this project.
 */

/* 1 = release behaviour (circular overwrite), 0 = dev/test behaviour (file cap). */
#ifndef DASHCAM_RELEASE_BUILD
#define DASHCAM_RELEASE_BUILD          1
#endif

/* Live preview resolution (matches dashcam_sky_area canvas).
 *
 * Preview at 960x540 (clean 2:1 downscale of the 1920x1080 sensor). The
 * per-frame NV12->RGB565 conversion is offloaded to the vg_lite GPU
 * (~20ms at this size vs ~85ms on the CPU); the camera read (~78ms/frame)
 * is the remaining bottleneck. */
#ifndef DASHCAM_PREVIEW_WIDTH
#define DASHCAM_PREVIEW_WIDTH          960
#endif
#ifndef DASHCAM_PREVIEW_HEIGHT
#define DASHCAM_PREVIEW_HEIGHT         540
#endif
#ifndef DASHCAM_PREVIEW_CAPTURE_HEIGHT
/* VG-Lite aligns NV12 source height to 16 lines internally. Keep the visible
 * preview at 540, but ask ISP/read buffers for 544 lines so GPU Y/UV access
 * stays inside the allocated frame. */
#define DASHCAM_PREVIEW_CAPTURE_HEIGHT (((DASHCAM_PREVIEW_HEIGHT + 15) / 16) * 16)
#endif

/*
 * Record resolution. Test phase reuses the preview resolution (Q3). Override
 * these two macros (e.g. 1280x720) for a higher-quality release stream.
 */
/*
 * Record resolution (ISP MP -> H264 encoder). Decoupled from the preview (SP)
 * resolution so record + live preview can run together (req5 #3).
 *
 * The H264 encoder's reference/recon buffers are allocated from the same coded
 * slab heap (h264_encode_malloc -> MEM_SLAB_HEAP_CODED, 7MB) that already holds
 * the DPU display framebuffers (~2.7MB) and the encoded-data pool (~2.5MB).
 * Recording at the full 960x540 preview size overflows that budget
 * (vcenc_h264_init -> -6 EWL_MEMORY_ERROR), so record at 640x360 while previewing
 * at 960x540. Bump these once the coded-heap budget is reorganized for release.
 */
#ifndef DASHCAM_RECORD_WIDTH
#define DASHCAM_RECORD_WIDTH           1280
#endif
#ifndef DASHCAM_RECORD_HEIGHT
#define DASHCAM_RECORD_HEIGHT          720
#endif

#ifndef DASHCAM_RECORD_FPS
#define DASHCAM_RECORD_FPS             25
#endif

/* Board and panel integration used by the shared dashcam component. */
#define DASHCAM_CAMERA_PIN_RESET        ((uint8_t)-1)
#define DASHCAM_CAMERA_HMIRROR          0
#define DASHCAM_CAMERA_VFLIP            0
#define DASHCAM_ASSIST_ROTATION         180
#define DASHCAM_ASSIST_DST_WIDTH        1024
#define DASHCAM_ASSIST_DST_HEIGHT       600
#define DASHCAM_ASSIST_SCALE            1
#define DASHCAM_STORAGE_LABEL_WITH_SIZE 0

/*
 * Playback target resolution.
 *
 * Recorded clips are decoded to NV12 internally, then GPU-scaled to the panel
 * size, rotated according to DASHCAM_PLAYBACK_ROTATION and converted to compressed
 * ARGB8888. Playback temporarily stops LVGL and submits those frames to the DPU.
 */
#ifndef DASHCAM_PLAYBACK_WIDTH
#define DASHCAM_PLAYBACK_WIDTH         1024
#endif
#ifndef DASHCAM_PLAYBACK_HEIGHT
#define DASHCAM_PLAYBACK_HEIGHT        600
#endif

#ifndef DASHCAM_PLAYBACK_ROTATION
#define DASHCAM_PLAYBACK_ROTATION      180
#endif

/* Segment length and dev-phase file cap.
 *
 * Review decision (req5 §9.1/§9.4): dev/test build records 1-minute segments and
 * keeps at most 1000 clips so the SD card cannot be filled while developing. */
#ifndef DASHCAM_SEGMENT_SECONDS
#define DASHCAM_SEGMENT_SECONDS        60
#endif
#ifndef DASHCAM_DEV_MAX_FILES
#define DASHCAM_DEV_MAX_FILES          1000
#endif

/*
 * Delay (ms) before power-on continuous recording starts. The boot record
 * pipeline (ISP/flexa/H264) and the home speed-gauge's first redraw burst both
 * pull from the small AP SRAM heap; bringing them up at the same instant
 * starved LVGL draw allocations and asserted (LV_ASSERT_MALLOC in
 * lv_draw_add_task). Deferring the record start lets the home page render and
 * free its transient boot buffers first (req5 §9.1 OOM fix).
 */
#ifndef DASHCAM_BOOT_RECORD_DELAY_MS
#define DASHCAM_BOOT_RECORD_DELAY_MS   2000u
#endif

/*
 * Release recycling (req5 §11): a pure free-space ring. There is intentionally
 * no fixed file-count cap; segments are recycled (oldest first) only when the
 * card's free space drops below this floor.
 */
#ifndef DASHCAM_RECYCLE_MIN_FREE_MB
#define DASHCAM_RECYCLE_MIN_FREE_MB    200u
#endif

#ifndef DASHCAM_RECYCLE_UNLINK_RETRY_MAX
#define DASHCAM_RECYCLE_UNLINK_RETRY_MAX   8u
#endif

#ifndef DASHCAM_RECYCLE_MAX_DELETE
#define DASHCAM_RECYCLE_MAX_DELETE         32u
#endif

/*
 * Lost-cluster maintenance for intentional power-cut / dirty reboot tests:
 * reclaim only when .rec_dirty is set (or one-shot migration), overlapping the
 * existing DASHCAM_BOOT_RECORD_DELAY_MS window — no segment-boundary reclaim.
 */
#ifndef DASHCAM_BOOT_RECLAIM_LOST
#define DASHCAM_BOOT_RECLAIM_LOST          1
#endif
#ifndef DASHCAM_BOOT_RECLAIM_ONLY_IF_DIRTY
#define DASHCAM_BOOT_RECLAIM_ONLY_IF_DIRTY 1
#endif

/*
 * Wall-clock naming/labels (plan A, req5 §9.2/§11). Only enabled on a release
 * build that also has the SNTP->RTC sync feature compiled in; otherwise plan B
 * (boot-relative uptime + sequence) is used. Reference DASHCAM_USE_WALL_CLOCK
 * only from preprocessor #if (CONFIG_NTP_SYNC_RTC is undefined when unset).
 */
#ifndef DASHCAM_USE_WALL_CLOCK
#define DASHCAM_USE_WALL_CLOCK         (CONFIG_NTP_SYNC_RTC)
#endif

/*
 * Audio recording (Q5). Disabled until the ADK onboard-mic + audio_recorder_device
 * pipeline is ported/linked into this project (see req5_design.md follow-up).
 */
#ifndef DASHCAM_RECORD_AUDIO
#define DASHCAM_RECORD_AUDIO           0
#endif

#ifdef __cplusplus
}
#endif

#endif /* __DASHCAM_CONFIG_H__ */
