#include "ota_ui.h"

#include <stdint.h>
#include "beken_ui.h"
#include "components/log.h"
#include "lvgl.h"

#define TAG "ota_ui"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* ---------- OTA progress demo: drive the arc and percent label together ---------- */
#define OTA_PROGRESS_PERIOD_MS  120
#define OTA_PROGRESS_STEP       1

static lv_timer_t *s_ota_progress_timer = NULL;
static int32_t s_ota_progress_value = 0;

static void ota_ui_progress_apply(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (ui->ota_update == NULL || !lv_obj_is_valid(ui->ota_update))
    {
        return;
    }

    if (ui->ota_update_prog_ring != NULL && lv_obj_is_valid(ui->ota_update_prog_ring))
    {
        lv_arc_set_value(ui->ota_update_prog_ring, s_ota_progress_value);
    }

    if (ui->ota_update_pct_num != NULL && lv_obj_is_valid(ui->ota_update_pct_num))
    {
        lv_label_set_text_fmt(ui->ota_update_pct_num, "%" LV_PRId32, s_ota_progress_value);
    }

    if (ui->ota_update_stat_lbl != NULL && lv_obj_is_valid(ui->ota_update_stat_lbl))
    {
        lv_label_set_text(ui->ota_update_stat_lbl,
                          s_ota_progress_value >= 100 ? "升级完成" : "升级中");
    }

    LOGD("progress apply value=%" LV_PRId32 "\n", s_ota_progress_value);
}

static void ota_ui_progress_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    s_ota_progress_value += OTA_PROGRESS_STEP;
    if (s_ota_progress_value > 100)
    {
        s_ota_progress_value = 0;
    }

    ota_ui_progress_apply();
}

void ota_ui_enter(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    LOGD("enter\n");

    if (s_ota_progress_timer)
    {
        lv_timer_delete(s_ota_progress_timer);
        s_ota_progress_timer = NULL;
    }

    if (ui->ota_update == NULL || !lv_obj_is_valid(ui->ota_update))
    {
        LOGD("ota_update page not valid, skip\n");
        return;
    }

    s_ota_progress_value = 0;
    ota_ui_progress_apply();
    s_ota_progress_timer = lv_timer_create(ota_ui_progress_timer_cb, OTA_PROGRESS_PERIOD_MS, NULL);
    LOGI("ota progress started\n");
}

void ota_ui_leave(void)
{
    LOGD("leave\n");

    if (s_ota_progress_timer)
    {
        lv_timer_delete(s_ota_progress_timer);
        s_ota_progress_timer = NULL;
        LOGI("ota progress stopped\n");
    }
}
