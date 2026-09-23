#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>

#include "lv_mem_pool.h"

#define TAG "lv_mem_pool"

static void *s_lv_mem_pool;
static size_t s_lv_mem_pool_size;

void *display_ui_lv_pool_alloc(size_t size)
{
    void *pool;

    if (s_lv_mem_pool != NULL)
    {
        if (size != s_lv_mem_pool_size)
        {
            BK_LOGE(TAG, "LVGL pool size mismatch: requested=%u reserved=%u\n",
                    (unsigned)size, (unsigned)s_lv_mem_pool_size);
            return NULL;
        }

        BK_LOGI(TAG, "LVGL pool reused: addr=%p size=%u\n",
                s_lv_mem_pool, (unsigned)s_lv_mem_pool_size);
        return s_lv_mem_pool;
    }

#if CONFIG_LVGL_MEM_USE_PSRAM
    pool = psram_malloc(size);

    if (pool == NULL)
    {
        BK_LOGE(TAG, "PSRAM LVGL pool alloc failed: size=%u free=%u min=%u\n",
                (unsigned)size,
                (unsigned)rtos_get_psram_free_heap_size(),
                (unsigned)rtos_get_psram_minimum_free_heap_size());
    }
    else
    {
        BK_LOGI(TAG, "PSRAM LVGL pool ready: addr=%p size=%u free=%u\n",
                pool,
                (unsigned)size,
                (unsigned)rtos_get_psram_free_heap_size());
        s_lv_mem_pool = pool;
        s_lv_mem_pool_size = size;
    }
#else
    pool = hsram_malloc(size);

    if (pool == NULL)
    {
        BK_LOGE(TAG, "HSRAM LVGL pool alloc failed: size=%u free=%u min=%u\n",
                (unsigned)size,
                (unsigned)rtos_get_hsram_free_heap_size(),
                (unsigned)rtos_get_hsram_minimum_free_heap_size());

        /*
         * Returning NULL is not survivable: lv_mem_init() feeds this straight
         * to lv_tlsf_create_with_pool(), whose lv_tlsf_create() accepts NULL
         * (it is ALIGN_SIZE-aligned) and then constructs the control block at
         * address 0. Fall back to PSRAM so the UI comes back degraded instead
         * of hard-faulting with a dump that says nothing about the real cause.
         * The error line above is the one that matters.
         */
        pool = psram_malloc(size);
        if (pool == NULL)
        {
            BK_LOGE(TAG, "PSRAM LVGL pool fallback failed too: size=%u\n",
                    (unsigned)size);
        }
        else
        {
            BK_LOGE(TAG, "LVGL pool fell back to PSRAM: addr=%p size=%u"
                    " (UI alive but slower; HSRAM lacks a contiguous block)\n",
                    pool, (unsigned)size);
            s_lv_mem_pool = pool;
            s_lv_mem_pool_size = size;
        }
    }
    else
    {
        BK_LOGI(TAG, "HSRAM LVGL pool ready: addr=%p size=%u free=%u\n",
                pool,
                (unsigned)size,
                (unsigned)rtos_get_hsram_free_heap_size());
        s_lv_mem_pool = pool;
        s_lv_mem_pool_size = size;
    }
#endif

    return pool;
}

void display_ui_lv_pool_free(void)
{
    void *pool = s_lv_mem_pool;
    size_t size = s_lv_mem_pool_size;

    if (pool == NULL)
    {
        return;
    }

    s_lv_mem_pool = NULL;
    s_lv_mem_pool_size = 0;

    /* os_free() dispatches on the address, so it takes the block back to
     * whichever heap display_ui_lv_pool_alloc() got it from - which is not
     * necessarily the one CONFIG_LVGL_MEM_USE_PSRAM asked for, since the HSRAM
     * path can fall back to PSRAM. */
    os_free(pool);
    BK_LOGI(TAG, "LVGL pool released: addr=%p size=%u hsram_free=%u psram_free=%u\n",
            pool, (unsigned)size,
            (unsigned)rtos_get_hsram_free_heap_size(),
            (unsigned)rtos_get_psram_free_heap_size());
}
