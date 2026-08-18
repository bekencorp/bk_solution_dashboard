#include "dashcam_gpu_postprocess.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/avdk_pixel_types.h"
#include "components/bk_frame_buffer.h"
#include "components/bk_hardware_ram.h"
#include "components/log.h"
#include "driver/hpdma.h"
#include "modules/vg_lite_gpu/vg_lite.h"
#include "os/mem.h"
#include "os/os.h"
#include "soc/reg_base.h"

#define TAG "d_gpu_pp"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define DASHCAM_GPU_ALIGN_BYTES       64U
#define DASHCAM_GPU_FRAME_PAD_BYTES   128U
#define DASHCAM_GPU_STRIP_LINES       8U
#define DASHCAM_GPU_DMA_TIMEOUT_MS    3000U

typedef struct
{
    uintptr_t pingpong_raw;
    uintptr_t buffers[2];
    uint32_t strip_bytes;
    uint8_t dst_idx;
    hpdma_id_t dma;
    void *link_table;
    beken_semaphore_t transfer_sem;
} dashcam_gpu_dma_t;

static beken_mutex_t s_gpu_mutex = NULL;
static dashcam_gpu_dma_t s_gpu_dma =
{
    .dma = HPDMA_ID_MAX,
};
static uint32_t s_logged_render_width;
static uint32_t s_logged_render_height;
static uint32_t s_logged_rotation = UINT32_MAX;

static uint32_t dashcam_gpu_align_up(uint32_t value, uint32_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

static uintptr_t dashcam_gpu_align_ptr(uintptr_t value, uintptr_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

static bk_err_t dashcam_gpu_lock(void)
{
    if (s_gpu_mutex == NULL)
    {
        if (rtos_init_mutex(&s_gpu_mutex) != BK_OK)
        {
            return BK_FAIL;
        }
    }

    rtos_lock_mutex(&s_gpu_mutex);
    return BK_OK;
}

static void dashcam_gpu_unlock(void)
{
    if (s_gpu_mutex != NULL)
    {
        rtos_unlock_mutex(&s_gpu_mutex);
    }
}

static void dashcam_gpu_dma_finish_cb(hpdma_id_t hpdma_id, void *user_data)
{
    (void)hpdma_id;

    if (user_data != NULL)
    {
        rtos_set_semaphore((beken_semaphore_t *)user_data);
    }
}

static void dashcam_gpu_dma_wait_idle(void)
{
    if (s_gpu_dma.dma >= HPDMA_ID_MAX)
    {
        return;
    }

    for (uint32_t wait_ms = 0; wait_ms < DASHCAM_GPU_DMA_TIMEOUT_MS; wait_ms++)
    {
        if (bk_hpdma_get_next_ll_addr(s_gpu_dma.dma) == 0U &&
            bk_hpdma_get_enable_status(s_gpu_dma.dma) == 0U)
        {
            return;
        }
        rtos_delay_milliseconds(1U);
    }

    LOGE("HPDMA channel %d still busy\n", (int)s_gpu_dma.dma);
}

static void dashcam_gpu_dma_deinit(void)
{
    if (s_gpu_dma.dma < HPDMA_ID_MAX)
    {
        dashcam_gpu_dma_wait_idle();
        (void)bk_hpdma_disable_finish_interrupt(s_gpu_dma.dma);
        (void)bk_hpdma_register_isr(s_gpu_dma.dma, NULL, NULL, NULL, NULL);
        bk_err_t ret = bk_hpdma_free(HPDMA_DEV_DTCM, s_gpu_dma.dma);
        if (ret == BK_ERR_HPDMA_TIMEOUT)
        {
            (void)bk_hpdma_force_reclaim(HPDMA_DEV_DTCM, s_gpu_dma.dma);
        }
        s_gpu_dma.dma = HPDMA_ID_MAX;
    }

    if (s_gpu_dma.link_table != NULL)
    {
        bk_hpdma_link_deinit(s_gpu_dma.link_table);
        s_gpu_dma.link_table = NULL;
    }
    if (s_gpu_dma.transfer_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_gpu_dma.transfer_sem);
        s_gpu_dma.transfer_sem = NULL;
    }
    if (s_gpu_dma.pingpong_raw != 0U)
    {
        hsram_free((void *)s_gpu_dma.pingpong_raw);
        s_gpu_dma.pingpong_raw = 0U;
    }

    s_gpu_dma.buffers[0] = 0U;
    s_gpu_dma.buffers[1] = 0U;
    s_gpu_dma.strip_bytes = 0U;
    s_gpu_dma.dst_idx = 0U;
    s_logged_render_width = 0U;
    s_logged_render_height = 0U;
    s_logged_rotation = UINT32_MAX;
}

static bk_err_t dashcam_gpu_dma_init(uint32_t strip_bytes)
{
    if (s_gpu_dma.pingpong_raw != 0U && s_gpu_dma.strip_bytes >= strip_bytes)
    {
        return BK_OK;
    }

    dashcam_gpu_dma_deinit();

    const uint32_t pingpong_size = (strip_bytes * 2U) + DASHCAM_GPU_ALIGN_BYTES;
    uintptr_t base = (uintptr_t)bk_get_gpu_output_buffer(pingpong_size);
    if (base == 0U)
    {
        LOGE("alloc GPU strip buffers failed, size=%u\n", (unsigned)pingpong_size);
        return BK_ERR_NO_MEM;
    }

    s_gpu_dma.pingpong_raw = base;
    s_gpu_dma.strip_bytes = strip_bytes;
    s_gpu_dma.dst_idx = 0U;
    os_memset((void *)base, 0, pingpong_size);
    s_gpu_dma.buffers[0] = dashcam_gpu_align_ptr(base, DASHCAM_GPU_ALIGN_BYTES);
    s_gpu_dma.buffers[1] = dashcam_gpu_align_ptr(base + strip_bytes,
                                                DASHCAM_GPU_ALIGN_BYTES);

    s_gpu_dma.link_table = bk_hpdma_link_init(1);
    if (s_gpu_dma.link_table == NULL)
    {
        dashcam_gpu_dma_deinit();
        return BK_FAIL;
    }

    s_gpu_dma.dma = bk_hpdma_alloc(HPDMA_DEV_DTCM);
    if (s_gpu_dma.dma >= HPDMA_ID_MAX)
    {
        dashcam_gpu_dma_deinit();
        return BK_FAIL;
    }

    (void)bk_hpdma_set_dest_burst_len(s_gpu_dma.dma, HPDMA_BURST_LEN_INC16);
    (void)bk_hpdma_set_src_burst_len(s_gpu_dma.dma, HPDMA_BURST_LEN_INC16);
    if (rtos_init_semaphore(&s_gpu_dma.transfer_sem, 1) != BK_OK)
    {
        dashcam_gpu_dma_deinit();
        return BK_FAIL;
    }

    (void)bk_hpdma_register_isr(s_gpu_dma.dma,
                                NULL,
                                NULL,
                                dashcam_gpu_dma_finish_cb,
                                &s_gpu_dma.transfer_sem);
    (void)bk_hpdma_enable_finish_interrupt(s_gpu_dma.dma);
    LOGI("strip buffers ready, bytes=%u dma=%d\n",
         (unsigned)strip_bytes,
         (int)s_gpu_dma.dma);
    return BK_OK;
}

static void dashcam_gpu_dma_normalize_sem(void)
{
    while (s_gpu_dma.transfer_sem != NULL &&
           rtos_get_semaphore(&s_gpu_dma.transfer_sem, BEKEN_NO_WAIT) == BK_OK)
    {
    }
}

static bk_err_t dashcam_gpu_dma_transfer(uint32_t src_addr,
                                         void *frame,
                                         uint32_t offset,
                                         uint32_t xsize,
                                         uint32_t ysize,
                                         uint32_t dst_step)
{
    hpdma_link_config_t cfg;
    os_memset(&cfg, 0, sizeof(cfg));
    cfg.src_addr = src_addr;
    cfg.dst_addr = (uint32_t)((uintptr_t)frame + offset);
    cfg.src_xsize = (uint16_t)xsize;
    cfg.src_ysize = (uint16_t)ysize;
    cfg.dst_xsize = (uint16_t)xsize;
    cfg.dst_ysize = (uint16_t)ysize;
    cfg.dst_step = (uint16_t)dst_step;
    cfg.finish_int_en = 1;

    dashcam_gpu_dma_normalize_sem();
    if (bk_hpdma_link_set_descs(s_gpu_dma.link_table, &cfg, 1) != BK_OK ||
        bk_hpdma_link_transfer(s_gpu_dma.dma,
                               (void *)SOC_SRAM_PERI_ADDR((uintptr_t)s_gpu_dma.link_table)) != BK_OK)
    {
        return BK_FAIL;
    }
    if (rtos_get_semaphore(&s_gpu_dma.transfer_sem,
                           DASHCAM_GPU_DMA_TIMEOUT_MS) != BK_OK)
    {
        LOGE("strip HPDMA timeout\n");
        return BK_ERR_TIMEOUT;
    }
    return BK_OK;
}

static void dashcam_gpu_strip_matrix_set(vg_lite_matrix_t *matrix,
                                          uint32_t rotation,
                                          uint32_t render_width,
                                          float scale_x,
                                          float scale_y,
                                          uint32_t strip_index)
{
    vg_lite_identity(matrix);
    if (rotation != 0U)
    {
        vg_lite_rotate((vg_lite_float_t)rotation, matrix);
    }
    vg_lite_scale(scale_x, scale_y, matrix);

    switch (rotation)
    {
        case 90:
            matrix->m[0][2] =
                (vg_lite_float_t)(strip_index * DASHCAM_GPU_STRIP_LINES);
            break;

        case 180:
            matrix->m[0][2] = (vg_lite_float_t)render_width;
            matrix->m[1][2] =
                (vg_lite_float_t)(strip_index * DASHCAM_GPU_STRIP_LINES);
            break;

        case 270:
            matrix->m[0][2] =
                -(vg_lite_float_t)((strip_index - 1U) * DASHCAM_GPU_STRIP_LINES);
            matrix->m[1][2] = (vg_lite_float_t)render_width;
            break;

        case 0:
        default:
            matrix->m[1][2] =
                -(vg_lite_float_t)((strip_index - 1U) * DASHCAM_GPU_STRIP_LINES);
            break;
    }
}

static void dashcam_gpu_strip_dma_layout(uint32_t rotation,
                                          uint32_t render_width,
                                          uint32_t render_height,
                                          uint32_t strip_index,
                                          uint32_t *offset,
                                          uint32_t *xsize,
                                          uint32_t *ysize,
                                          uint32_t *dst_step)
{
    if (rotation == 90U || rotation == 270U)
    {
        *offset = (rotation == 90U) ?
                  ((render_height - (strip_index * DASHCAM_GPU_STRIP_LINES)) * 4U) :
                  ((strip_index - 1U) * DASHCAM_GPU_STRIP_LINES * 4U);
        *xsize = DASHCAM_GPU_STRIP_LINES * 4U;
        *ysize = render_width / 4U;
        *dst_step = (render_height - DASHCAM_GPU_STRIP_LINES) * 4U;
    }
    else
    {
        *offset = (rotation == 180U) ?
                  ((render_height - (strip_index * DASHCAM_GPU_STRIP_LINES)) *
                   render_width) :
                  ((strip_index - 1U) * render_width * DASHCAM_GPU_STRIP_LINES);
        *xsize = render_width * 4U;
        *ysize = DASHCAM_GPU_STRIP_LINES / 4U;
        *dst_step = 0U;
    }
}

bk_err_t dashcam_gpu_postprocess_nv12(const uint8_t *nv12,
                                      uint32_t src_width,
                                      uint32_t src_height,
                                      uint32_t dst_width,
                                      uint32_t dst_height,
                                      uint32_t rotation,
                                      void **out_frame)
{
    if (nv12 == NULL || out_frame == NULL ||
        src_width == 0U || src_height == 0U ||
        dst_width == 0U || dst_height == 0U ||
        ((src_width | src_height | dst_width | dst_height) & 1U) != 0U ||
        (rotation != 0U && rotation != 90U &&
         rotation != 180U && rotation != 270U))
    {
        return BK_ERR_PARAM;
    }
    *out_frame = NULL;

    if (dashcam_gpu_lock() != BK_OK)
    {
        return BK_FAIL;
    }

    uint32_t render_width = (rotation == 90U || rotation == 270U) ?
                            dst_height : dst_width;
    uint32_t render_height = (rotation == 90U || rotation == 270U) ?
                             dst_width : dst_height;
    render_width = dashcam_gpu_align_up(render_width, 16U);
    render_height = dashcam_gpu_align_up(render_height, DASHCAM_GPU_STRIP_LINES);

    const uint32_t strip_bytes = render_width * DASHCAM_GPU_STRIP_LINES;
    bk_err_t ret = dashcam_gpu_dma_init(strip_bytes);
    if (ret != BK_OK)
    {
        dashcam_gpu_unlock();
        return ret;
    }

    const uint32_t frame_size = render_width * render_height;
    if (render_width != s_logged_render_width ||
        render_height != s_logged_render_height ||
        rotation != s_logged_rotation)
    {
        LOGI("frame src=%ux%u dst=%ux%u render=%ux%u rotation=%u\n",
             (unsigned)src_width,
             (unsigned)src_height,
             (unsigned)dst_width,
             (unsigned)dst_height,
             (unsigned)render_width,
             (unsigned)render_height,
             (unsigned)rotation);
        s_logged_render_width = render_width;
        s_logged_render_height = render_height;
        s_logged_rotation = rotation;
    }
    const uint32_t alloc_size = dashcam_gpu_align_up(frame_size +
                                                      DASHCAM_GPU_FRAME_PAD_BYTES,
                                                      DASHCAM_GPU_ALIGN_BYTES);
    void *gpu_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, alloc_size);
    if (gpu_frame == NULL)
    {
        dashcam_gpu_unlock();
        return BK_ERR_NO_MEM;
    }
    os_memset(gpu_frame, 0, alloc_size);

    vg_lite_buffer_t src_buf;
    vg_lite_buffer_t dst_buf;
    vg_lite_matrix_t matrix;
    os_memset(&src_buf, 0, sizeof(src_buf));
    os_memset(&dst_buf, 0, sizeof(dst_buf));
    os_memset(&matrix, 0, sizeof(matrix));

    src_buf.width = (vg_lite_uint32_t)src_width;
    src_buf.height = (vg_lite_uint32_t)src_height;
    src_buf.stride = (vg_lite_int32_t)src_width;
    src_buf.format = VG_LITE_NV12;
    src_buf.compress_mode = VG_LITE_DEC_DISABLE;
    src_buf.tiled = VG_LITE_LINEAR;
    src_buf.yuv.swizzle = VG_LITE_SWIZZLE_UV;
    src_buf.yuv.yuv2rgb = VG_LITE_YUV601;
    src_buf.yuv.uv_stride = (vg_lite_uint32_t)src_width;
    src_buf.yuv.uv_height = (vg_lite_uint32_t)(src_height / 2U);

    vg_lite_error_t vg_ret = vg_lite_allocate_with_data(&src_buf,
                                                        (void *)nv12,
                                                        (void *)(nv12 +
                                                                 (src_width * src_height)),
                                                        NULL,
                                                        NULL);
    if (vg_ret != VG_LITE_SUCCESS)
    {
        bk_frame_buffer_free(gpu_frame);
        dashcam_gpu_unlock();
        return BK_FAIL;
    }

    if (rotation == 90U || rotation == 270U)
    {
        dst_buf.width = DASHCAM_GPU_STRIP_LINES;
        dst_buf.height = render_width;
    }
    else
    {
        dst_buf.width = render_width;
        dst_buf.height = DASHCAM_GPU_STRIP_LINES;
    }
    dst_buf.format = VG_LITE_BGRA8888;
    dst_buf.compress_mode = VG_LITE_DEC_HV_SAMPLE;
    dst_buf.tiled = VG_LITE_TILED;

    vg_ret = vg_lite_allocate_with_data(&dst_buf,
                                        (void *)s_gpu_dma.buffers[s_gpu_dma.dst_idx],
                                        NULL,
                                        NULL,
                                        NULL);
    if (vg_ret != VG_LITE_SUCCESS)
    {
        (void)vg_lite_free_without_free_data(&src_buf);
        bk_frame_buffer_free(gpu_frame);
        dashcam_gpu_unlock();
        return BK_FAIL;
    }

    const float scale_x = (float)render_width / (float)src_width;
    const float scale_y = (float)render_height / (float)src_height;
    const uint32_t strip_count = render_height / DASHCAM_GPU_STRIP_LINES;

    for (uint32_t strip_index = 1U; strip_index <= strip_count; strip_index++)
    {
        dst_buf.memory =
            (vg_lite_pointer)(uintptr_t)s_gpu_dma.buffers[s_gpu_dma.dst_idx];
        dst_buf.address =
            SOC_SRAM_PERI_ADDR(s_gpu_dma.buffers[s_gpu_dma.dst_idx]);
        dashcam_gpu_strip_matrix_set(&matrix,
                                     rotation,
                                     render_width,
                                     scale_x,
                                     scale_y,
                                     strip_index);

        vg_ret = vg_lite_blit(&dst_buf,
                              &src_buf,
                              &matrix,
                              VG_LITE_BLEND_NONE,
                              0,
                              VG_LITE_FILTER_POINT);
        if (vg_ret == VG_LITE_SUCCESS)
        {
            vg_ret = vg_lite_finish();
        }
        if (vg_ret != VG_LITE_SUCCESS)
        {
            ret = BK_FAIL;
            break;
        }

        uint32_t offset;
        uint32_t xsize;
        uint32_t ysize;
        uint32_t dst_step;
        dashcam_gpu_strip_dma_layout(rotation,
                                     render_width,
                                     render_height,
                                     strip_index,
                                     &offset,
                                     &xsize,
                                     &ysize,
                                     &dst_step);
        ret = dashcam_gpu_dma_transfer((uint32_t)s_gpu_dma.buffers[s_gpu_dma.dst_idx],
                                       gpu_frame,
                                       offset,
                                       xsize,
                                       ysize,
                                       dst_step);
        if (ret != BK_OK)
        {
            break;
        }
        s_gpu_dma.dst_idx = 1U - s_gpu_dma.dst_idx;
    }

    (void)vg_lite_free_without_free_data(&dst_buf);
    (void)vg_lite_free_without_free_data(&src_buf);

    if (ret != BK_OK || vg_ret != VG_LITE_SUCCESS)
    {
        bk_frame_buffer_free(gpu_frame);
        dashcam_gpu_unlock();
        return (ret != BK_OK) ? ret : BK_FAIL;
    }

    *out_frame = gpu_frame;
    dashcam_gpu_unlock();
    return BK_OK;
}

void dashcam_gpu_postprocess_deinit(void)
{
    if (s_gpu_mutex == NULL || dashcam_gpu_lock() != BK_OK)
    {
        return;
    }

    dashcam_gpu_dma_deinit();
    dashcam_gpu_unlock();
}
