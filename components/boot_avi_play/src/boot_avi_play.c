
#include "boot_avi_play.h"

#include <os/os.h>
#include <os/mem.h>
#include <string.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_display.h>
#include <driver/dpu_types.h>

#if CONFIG_FATFS
#include "ff.h"
#if CONFIG_VFS
#include "bk_partition.h"
#endif
#endif

#include <modules/avilib.h>
#include "jpeg_stream_pipeline.h"
#include "boot_sd_mount.h"

#include "display_ui_cast_hooks.h"
#include "display_ui_cast_context.h"

#define TAG "boot_avi"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BOOT_AVI_FATFS_PATH     "1:/boot.avi"
#if CONFIG_VFS
#define BOOT_AVI_OPEN_PATH      PATH_SD_FILE("boot.avi")
#else
#define BOOT_AVI_OPEN_PATH      BOOT_AVI_FATFS_PATH
#endif
#define BOOT_AVI_JPEG_BUF_SIZE  (64 * 1024)
#define MAX_CONSECUTIVE_FAILURES 5
#define DQT1_SEGMENT_SIZE       69  /* FF DB 00 43 01 + 64 quant values */
#define BOOT_DISPLAY_FLUSH_WAIT_MS 500

#if CONFIG_FATFS

/*
 * Standard JPEG baseline Huffman tables (ITU-T T.81, Annex K).
 * MJPEG frames inside AVI containers typically omit DHT markers;
 * the hardware JPEG decoder (VCDEC) requires them. This segment
 * is injected after SOI when DHT is absent.
 *
 * Layout: FF C4 <len_hi> <len_lo> [DC0] [AC0] [DC1] [AC1]
 * Total size: 420 bytes.
 */
#define MJPEG_DHT_SIZE 420

static const uint8_t s_mjpeg_dht[MJPEG_DHT_SIZE] = {
    0xFF, 0xC4, 0x01, 0xA2,
    /* DC table 0 (luma): tc=0 th=0 */
    0x00,
    0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B,
    /* AC table 0 (luma): tc=1 th=0 */
    0x10,
    0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
    0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
    /* DC table 1 (chroma): tc=0 th=1 */
    0x01,
    0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B,
    /* AC table 1 (chroma): tc=1 th=1 */
    0x11,
    0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
    0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
    0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
};

static bool jpeg_has_dht(const uint8_t *data, uint32_t len)
{
    if (len < 4)
        return false;

    uint32_t i = 2;
    while (i + 4 <= len) {
        if (data[i] != 0xFF)
            return false;
        while (i < len && data[i] == 0xFF)
            i++;
        if (i >= len)
            return false;

        uint8_t marker = data[i++];
        if (marker == 0xC4)
            return true;
        if (marker == 0xDA || marker == 0xD9)
            return false;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;

        if (i + 2 > len)
            return false;
        uint16_t seg_len = (uint16_t)((data[i] << 8) | data[i + 1]);
        i += seg_len;
    }
    return false;
}

/*
 * VCDEC requires qt_present[1] for color JPEG even if all components
 * reference QT0. This function duplicates QT0 as QT1 via an injected
 * DQT segment right after the existing one(s).
 *
 * Returns the number of bytes inserted (0 or DQT1_SEGMENT_SIZE=69).
 */
static int jpeg_inject_qt1_if_missing(uint8_t *buf, uint32_t len, uint32_t capacity)
{
    uint32_t i = 2;
    bool qt0_found = false, qt1_found = false;
    uint8_t qt0_vals[64];
    uint32_t last_dqt_end = 0;

    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8)
        return 0;

    while (i + 4U <= len) {
        if (buf[i] != 0xFF)
            break;
        while (i < len && buf[i] == 0xFF)
            i++;
        if (i >= len)
            break;

        uint8_t marker = buf[i++];
        if (marker == 0xD9 || marker == 0xDA)
            break;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;
        if (i + 2U > len)
            break;

        uint16_t seg_len = (uint16_t)((buf[i] << 8) | buf[i + 1]);
        i += 2;
        if (seg_len < 2U || i + (seg_len - 2U) > len)
            break;
        uint32_t payload_len = seg_len - 2U;

        if (marker == 0xDB) {
            uint32_t di = 0;
            const uint8_t *seg = &buf[i];
            while (di + 65U <= payload_len) {
                uint8_t tq = seg[di] & 0x0F;
                if (tq == 0 && !qt0_found) {
                    memcpy(qt0_vals, &seg[di + 1], 64);
                    qt0_found = true;
                }
                if (tq == 1)
                    qt1_found = true;
                di += 65;
            }
            last_dqt_end = i + payload_len;
        }

        i += payload_len;
    }

    if (qt1_found || !qt0_found || last_dqt_end == 0)
        return 0;
    if (len + DQT1_SEGMENT_SIZE > capacity)
        return 0;

    memmove(&buf[last_dqt_end + DQT1_SEGMENT_SIZE],
            &buf[last_dqt_end],
            len - last_dqt_end);

    buf[last_dqt_end + 0] = 0xFF;
    buf[last_dqt_end + 1] = 0xDB;
    buf[last_dqt_end + 2] = 0x00;
    buf[last_dqt_end + 3] = 0x43; /* length = 67 */
    buf[last_dqt_end + 4] = 0x01; /* pq=0, tq=1 */
    memcpy(&buf[last_dqt_end + 5], qt0_vals, 64);

    return DQT1_SEGMENT_SIZE;
}

static beken_semaphore_t s_frame_consumed_sem = NULL;
static beken_semaphore_t s_display_flush_sem = NULL;
static volatile uint32_t s_frames_displayed = 0;
static volatile uint32_t s_display_flush_pending = 0;

static void *boot_gpu_malloc_cb(uint32_t size)
{
    void *p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
    if (!p)
        p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, size);
    if (!p)
        LOGE("gpu_malloc FAIL %u\n", (unsigned)size);
    return p;
}

static avdk_err_t boot_flush_done_cb(void *frame)
{
    if (frame)
        bk_frame_buffer_free(frame);

    if (s_display_flush_pending > 0)
        s_display_flush_pending--;

    if (s_display_flush_sem)
        rtos_set_semaphore(&s_display_flush_sem);

    return AVDK_ERR_OK;
}

static void boot_frame_display_cb(void *frame, uint32_t frame_size, void *user_data)
{
    bk_display_ctlr_handle_t disp = (bk_display_ctlr_handle_t)user_data;
    avdk_err_t flush_ret;

    (void)frame_size;
    if (disp && frame) {
        s_display_flush_pending++;
        flush_ret = bk_display_flush(disp, frame, boot_flush_done_cb);
        if (flush_ret != AVDK_ERR_OK) {
            LOGE("display flush failed: %d\n", (int)flush_ret);
            if (s_display_flush_pending > 0)
                s_display_flush_pending--;
            bk_frame_buffer_free(frame);
            if (s_display_flush_sem)
                rtos_set_semaphore(&s_display_flush_sem);
        }
    } else if (frame) {
        bk_frame_buffer_free(frame);
    }

    s_frames_displayed++;
}

static void boot_wait_display_flush_done(void)
{
    while (s_display_flush_pending > 0) {
        uint32_t pending = s_display_flush_pending;
        bk_err_t ret;

        if (!s_display_flush_sem) {
            rtos_delay_milliseconds(BOOT_DISPLAY_FLUSH_WAIT_MS);
            break;
        }

        ret = rtos_get_semaphore(&s_display_flush_sem, BOOT_DISPLAY_FLUSH_WAIT_MS);
        if (ret != BK_OK) {
            LOGW("wait display flush timeout, pending=%u\n", (unsigned)pending);
            break;
        }
    }
}

static void boot_frame_consumed_cb(const uint8_t *jpeg_stream, int status,
                                    void *jpeg_owner, void *user_data)
{
    (void)jpeg_stream;
    (void)jpeg_owner;
    (void)user_data;
    if (status != 0)
        LOGW("jpeg decode status=%d\n", status);
    if (s_frame_consumed_sem)
        rtos_set_semaphore(&s_frame_consumed_sem);
}

static bk_err_t dpu_switch_to_argb8888_decompress(void)
{
    /*
     * display_ui_init_display_hw() now brings the DPU up in ARGB8888 +
     * decompress (LVGL renders through the GPU-compress path), and it runs
     * before boot_avi_play(). boot_avi already produces ARGB8888-compressed
     * frames, so no runtime pixel-format switch is needed here anymore.
     */
    return BK_OK;
}

static bk_err_t dpu_restore_rgb565(void)
{
    /* DPU never left ARGB8888 + decompress; nothing to restore for LVGL. */
    return BK_OK;
}

bk_err_t boot_avi_play(void)
{
    FILINFO fno;
    avi_t *avi = NULL;
    uint8_t *jpeg_buf = NULL;
    jpeg_stream_pipeline_handle_t pipeline = NULL;
    jpeg_stream_pipeline_config_t jsp_cfg;
    avdk_err_t jsp_err;
    long total_frames, frame_len;
    int avi_w, avi_h;
    uint16_t dpu_w, dpu_h;
    uint16_t gpu_dst_w, gpu_dst_h;
    uint16_t gpu_rotate_degree;
    uint32_t consecutive_fail = 0;
    uint32_t ok_count = 0;
    bool dht_checked = false, dht_present = false;

    if (!display_ui_get_dpu_handle()) {
        LOGE("display HW not ready\n");
        return BK_FAIL;
    }

    if (boot_sd_mount() != BK_OK) {
        LOGW("SD card not available, skip boot animation\n");
        return BK_OK;
    }

    if (f_stat(BOOT_AVI_FATFS_PATH, &fno) != FR_OK) {
        LOGW("boot.avi not found on SD card, skip\n");
        return BK_OK;
    }

    LOGI("found %s (%lu bytes)\n", BOOT_AVI_FATFS_PATH, (unsigned long)fno.fsize);

    avi = AVI_open_input_file(BOOT_AVI_OPEN_PATH, 1, AVI_MEM_SRAM);
    if (!avi) {
        LOGE("AVI_open_input_file(%s) failed\n", BOOT_AVI_OPEN_PATH);
        return BK_OK;
    }

    total_frames = AVI_video_frames(avi);
    avi_w = avi->width;
    avi_h = avi->height;
    {
        const bk_display_dsi_panel_t *panel = display_ui_get_panel_desc();
        if (panel != NULL) {
            dpu_w = panel->timing.h_size;
            dpu_h = panel->timing.v_size;
        } else {
            dpu_w = 0;
            dpu_h = 0;
        }
    }

    LOGI("avi %dx%d @ %.1f fps, %ld frames, dpu %ux%u\n",
         avi_w, avi_h, avi->fps, total_frames, dpu_w, dpu_h);

    if (total_frames <= 0) {
        LOGW("no video frames, skip\n");
        AVI_close(avi);
        return BK_OK;
    }

    gpu_dst_w = dpu_w;
    gpu_dst_h = dpu_h;
#if defined(CONFIG_SCOOTER_UI_ROTATION)
    switch (CONFIG_SCOOTER_UI_ROTATION)
    {
        case 0:
            gpu_rotate_degree = 0;
            break;
        case 90:
            gpu_rotate_degree = 270;
            break;
        case 180:
            gpu_rotate_degree = 180;
            break;
        case 270:
            gpu_rotate_degree = 90;
            break;
        default:
            LOGW("unsupported UI rotation=%d, disable boot AVI rotation\n",
                 CONFIG_SCOOTER_UI_ROTATION);
            gpu_rotate_degree = 0;
            break;
    }
    if ((gpu_rotate_degree == 90 || gpu_rotate_degree == 270) &&
        (avi_w != dpu_h || avi_h != dpu_w))
    {
        LOGW("UI rotation=%d but AVI %dx%d does not match rotated DPU %ux%u\n",
             CONFIG_SCOOTER_UI_ROTATION, avi_w, avi_h, dpu_w, dpu_h);
        gpu_rotate_degree = 0;
    }
#else
    gpu_rotate_degree = (avi_w == dpu_h && avi_h == dpu_w) ? 90 : 0;
#endif
    if (gpu_rotate_degree == 90 || gpu_rotate_degree == 270)
    {
        gpu_dst_w = (uint16_t)avi_w;
        gpu_dst_h = (uint16_t)avi_h;
    }
    LOGI("boot AVI GPU rotation=%u dst=%ux%u\n",
         gpu_rotate_degree, gpu_dst_w, gpu_dst_h);
    /*
     * Buffer layout for zero-copy DHT injection:
     *   [0 .. MJPEG_DHT_SIZE-1]       = reserved for SOI(2) + DHT(420-2=418)
     *   [MJPEG_DHT_SIZE .. end]        = raw JPEG frame read area
     *
     * If DHT is missing: write SOI at [0..1], copy DHT at [2..419],
     * the post-SOI data is already in place at [420+2..], which coincides
     * with [422..] — no memmove needed.
     */
    uint32_t jpeg_buf_capacity = BOOT_AVI_JPEG_BUF_SIZE + MJPEG_DHT_SIZE + DQT1_SEGMENT_SIZE;
    jpeg_buf = psram_malloc(jpeg_buf_capacity);
    if (!jpeg_buf) {
        LOGE("JPEG buffer alloc failed (%u)\n", jpeg_buf_capacity);
        AVI_close(avi);
        return BK_OK;
    }

    s_display_flush_pending = 0;
    if (rtos_init_semaphore_ex(&s_display_flush_sem, 1, 0) != BK_OK) {
        LOGE("display flush sem init failed\n");
        goto cleanup;
    }

    dpu_switch_to_argb8888_decompress();

    /*
     * GPU FLEXA quarter-turn rotation requires dst = src (same dimensions).
     * The rotation is handled purely by the GPU matrix transform + DPU decompress.
     * This matches the cast pipeline config (defconfig: CAST_JPEG_DST = CAST_JPEG_SRC).
     */
    os_memset(&jsp_cfg, 0, sizeof(jsp_cfg));
    jsp_cfg.src_width  = (uint16_t)avi_w;
    jsp_cfg.src_height = (uint16_t)avi_h;
    jsp_cfg.dst_width  = gpu_dst_w;
    jsp_cfg.dst_height = gpu_dst_h;
    jsp_cfg.rotate_degree = gpu_rotate_degree;
    jsp_cfg.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    jsp_cfg.compress = true;
    jsp_cfg.scale = true;
    jsp_cfg.malloc_cb = boot_gpu_malloc_cb;
    jsp_cfg.frame_display_cb = boot_frame_display_cb;
    jsp_cfg.frame_consumed_cb = boot_frame_consumed_cb;
    jsp_cfg.user_data = display_ui_get_dpu_handle();

    jsp_err = jpeg_stream_pipeline_create(&pipeline, &jsp_cfg);
    if (jsp_err != AVDK_ERR_OK || !pipeline) {
        LOGE("pipeline create failed: %d\n", (int)jsp_err);
        goto cleanup;
    }

    jsp_err = jpeg_stream_pipeline_start(pipeline);
    if (jsp_err != AVDK_ERR_OK) {
        LOGE("pipeline start failed: %d\n", (int)jsp_err);
        goto cleanup;
    }

    if (rtos_init_semaphore_ex(&s_frame_consumed_sem, 1, 0) != BK_OK) {
        LOGE("consumed sem init failed\n");
        goto cleanup;
    }
    s_frames_displayed = 0;

    LOGI("playback start\n");

    for (long i = 0; i < total_frames; i++) {
        uint8_t *push_ptr;
        uint32_t push_len;

        if (AVI_set_video_read_index(avi, i, &frame_len) != 0) {
            consecutive_fail++;
            if (consecutive_fail >= MAX_CONSECUTIVE_FAILURES)
                break;
            continue;
        }

        if (frame_len <= 0 || frame_len > BOOT_AVI_JPEG_BUF_SIZE) {
            consecutive_fail++;
            if (consecutive_fail >= MAX_CONSECUTIVE_FAILURES)
                break;
            continue;
        }

        uint8_t *raw = jpeg_buf + MJPEG_DHT_SIZE;
        uint32_t raw_capacity = jpeg_buf_capacity - MJPEG_DHT_SIZE;
        long bytes_read = AVI_read_next_video_frame(avi, (char *)raw, frame_len);
        if (bytes_read != frame_len) {
            consecutive_fail++;
            if (consecutive_fail >= MAX_CONSECUTIVE_FAILURES)
                break;
            continue;
        }

        int qt1_added = jpeg_inject_qt1_if_missing(raw, (uint32_t)frame_len, raw_capacity);
        frame_len += qt1_added;

        if (!dht_checked) {
            dht_present = jpeg_has_dht(raw, (uint32_t)frame_len);
            dht_checked = true;
        }

        if (dht_present) {
            push_ptr = raw;
            push_len = (uint32_t)frame_len;
        } else {
            jpeg_buf[0] = 0xFF;
            jpeg_buf[1] = 0xD8;
            memcpy(jpeg_buf + 2, s_mjpeg_dht, MJPEG_DHT_SIZE);
            push_ptr = jpeg_buf;
            push_len = (uint32_t)frame_len + MJPEG_DHT_SIZE;
        }

        jsp_err = jpeg_stream_pipeline_push_frame(pipeline, push_ptr,
                                                   push_len, NULL);
        if (jsp_err != AVDK_ERR_OK) {
            consecutive_fail++;
            if (consecutive_fail >= MAX_CONSECUTIVE_FAILURES)
                break;
            continue;
        }

        consecutive_fail = 0;
        ok_count++;

        rtos_get_semaphore(&s_frame_consumed_sem, 2000);
    }

    LOGI("playback finished (%u pushed, %u displayed / %ld total)\n",
         ok_count, (unsigned)s_frames_displayed, total_frames);

cleanup:
    if (pipeline) {
        jpeg_stream_pipeline_stop(pipeline);
        jpeg_stream_pipeline_destroy(pipeline);
    }

    if (jpeg_buf)
        psram_free(jpeg_buf);

    if (s_frame_consumed_sem) {
        rtos_deinit_semaphore(&s_frame_consumed_sem);
        s_frame_consumed_sem = NULL;
    }

    boot_wait_display_flush_done();

    dpu_restore_rgb565();

    if (s_display_flush_sem && s_display_flush_pending == 0) {
        rtos_deinit_semaphore(&s_display_flush_sem);
        s_display_flush_sem = NULL;
    }

    if (avi)
        AVI_close(avi);

    LOGI("boot AVI play done\n");
    return BK_OK;
}

#else

bk_err_t boot_avi_play(void)
{
    return BK_OK;
}

#endif
