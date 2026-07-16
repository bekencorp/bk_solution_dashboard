#include "dashcam_camera.h"

#include <string.h>
#include "app_camera.h"
#include "app_codec.h"
#include "components/bk_encode/bk_h264_encode_ctlr.h"
#include "components/bk_flexa_bond.h"
#include "components/log.h"
#include "dashcam_config.h"
#include "driver/gpio.h"
#include "os/os.h"

#define TAG "dashcam_cam_own"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* MIPI sensor wiring (mirrors dashcam_video.c defaults). */
#ifndef DASHCAM_CAMERA_PIN_SCL
#define DASHCAM_CAMERA_PIN_SCL    GPIO_69
#endif
#ifndef DASHCAM_CAMERA_PIN_SDA
#define DASHCAM_CAMERA_PIN_SDA    GPIO_70
#endif
#ifndef DASHCAM_CAMERA_I2C_ID
#define DASHCAM_CAMERA_I2C_ID     1
#endif
#ifndef DASHCAM_CAMERA_PIN_RESET
#define DASHCAM_CAMERA_PIN_RESET  GPIO_71
#endif
#ifndef DASHCAM_CAMERA_PIN_PWDN
#define DASHCAM_CAMERA_PIN_PWDN   ((uint8_t)-1)
#endif
#ifndef DASHCAM_CAMERA_PIN_XCLK
#define DASHCAM_CAMERA_PIN_XCLK   GPIO_59
#endif
#define DASHCAM_CAMERA_SENSOR_WIDTH   1920
#define DASHCAM_CAMERA_SENSOR_HEIGHT  1080
#define DASHCAM_CAMERA_SENSOR_FPS     25

static dashcam_camera_mode_t s_mode = DASHCAM_CAMERA_MODE_OFF;
static void *s_isp_encode_bond = NULL;
static bool s_encoder_on = false;

static void dashcam_camera_fill_board(camera_board_config_t *cfg, dashcam_camera_mode_t mode)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->mipi.enable = true;
    cfg->mipi.pin_scl = DASHCAM_CAMERA_PIN_SCL;
    cfg->mipi.pin_sda = DASHCAM_CAMERA_PIN_SDA;
    cfg->mipi.i2c_id = DASHCAM_CAMERA_I2C_ID;
    cfg->mipi.pin_reset = DASHCAM_CAMERA_PIN_RESET;
    cfg->mipi.pin_pwdn = DASHCAM_CAMERA_PIN_PWDN;
    cfg->mipi.pin_xclk = DASHCAM_CAMERA_PIN_XCLK;
    cfg->mipi.sensor_max_width = DASHCAM_CAMERA_SENSOR_WIDTH;
    cfg->mipi.sensor_max_height = DASHCAM_CAMERA_SENSOR_HEIGHT;
    cfg->mipi.sensor_fps = DASHCAM_CAMERA_SENSOR_FPS;

    cfg->isp.mp_enable = true;
    cfg->isp.mp_format = BK_PIXEL_FORMAT_NV12;

    if (mode == DASHCAM_CAMERA_MODE_RECORD || mode == DASHCAM_CAMERA_MODE_RECORD_ONLY)
    {
        /* MP feeds the H264 encoder via flexa. The SP channel (live preview) is
         * only brought up for RECORD; RECORD_ONLY records headless and leaves SP
         * off to save its NV12 buffers (req5 §9.1 OOM fix). */
        cfg->isp.mp_flexa = true;
        cfg->isp.mp_width = DASHCAM_RECORD_WIDTH;
        cfg->isp.mp_height = DASHCAM_RECORD_HEIGHT;
        if (mode == DASHCAM_CAMERA_MODE_RECORD)
        {
            cfg->isp.sp_enable = true;
            cfg->isp.sp_flexa = false;
            cfg->isp.sp_width = DASHCAM_PREVIEW_WIDTH;
            cfg->isp.sp_height = DASHCAM_PREVIEW_CAPTURE_HEIGHT;
            cfg->isp.sp_format = BK_PIXEL_FORMAT_NV12;
        }
        else
        {
            cfg->isp.sp_enable = false;
            cfg->isp.sp_flexa = false;
        }
    }
    else
    {
        /* Preview-only: read NV12 straight off the MP channel, no encoder. */
        cfg->isp.mp_flexa = false;
        cfg->isp.mp_width = DASHCAM_PREVIEW_WIDTH;
        cfg->isp.mp_height = DASHCAM_PREVIEW_CAPTURE_HEIGHT;
        cfg->isp.sp_enable = false;
        cfg->isp.sp_flexa = false;
    }
}

static bk_err_t dashcam_camera_start_encoder_bond(void)
{
    void *isp_handle = app_isp_handle_get();
    bk_h264_encode_ctlr_handle_t enc_handle;

    if (isp_handle == NULL)
    {
        LOGE("app_isp_handle_get failed\n");
        return BK_FAIL;
    }

    if (app_h264e_turn_on() != BK_OK)
    {
        LOGE("app_h264e_turn_on failed\n");
        return BK_FAIL;
    }
    s_encoder_on = true;

    enc_handle = (bk_h264_encode_ctlr_handle_t)app_h264_encode_handle_get();
    if (enc_handle == NULL)
    {
        LOGE("app_h264_encode_handle_get failed\n");
        return BK_FAIL;
    }

    if (bk_flexa_isp_h264e_bond_start(&s_isp_encode_bond, isp_handle, enc_handle) != AVDK_ERR_OK)
    {
        LOGE("bk_flexa_isp_h264e_bond_start failed\n");
        s_isp_encode_bond = NULL;
        return BK_FAIL;
    }

    return BK_OK;
}

static void dashcam_camera_stop_encoder_bond(void)
{
    if (s_isp_encode_bond != NULL)
    {
        bk_flexa_isp_h264e_bond_stop(s_isp_encode_bond);
        s_isp_encode_bond = NULL;
    }

    if (s_encoder_on)
    {
        (void)app_h264e_turn_off();
        s_encoder_on = false;
    }
}

bk_err_t dashcam_camera_open(dashcam_camera_mode_t mode)
{
    camera_board_config_t cfg;

    LOGD("open req mode=%d (cur=%d)\n", (int)mode, (int)s_mode);

    if (mode == DASHCAM_CAMERA_MODE_OFF)
    {
        return BK_ERR_PARAM;
    }

    if (s_mode != DASHCAM_CAMERA_MODE_OFF)
    {
        if (s_mode == mode)
        {
            return BK_OK;
        }
        dashcam_camera_close();
    }

    dashcam_camera_fill_board(&cfg, mode);
    if (app_camera_board_config_set(&cfg) != BK_OK)
    {
        LOGE("app_camera_board_config_set failed\n");
        return BK_FAIL;
    }

    if (app_isp_mipi_camera_turn_on(app_camera_board_config_get()) != BK_OK)
    {
        LOGE("app_isp_mipi_camera_turn_on failed\n");
        return BK_FAIL;
    }

    if (mode == DASHCAM_CAMERA_MODE_RECORD || mode == DASHCAM_CAMERA_MODE_RECORD_ONLY)
    {
        /* Only RECORD brings up the SP (live preview) channel. */
        if (mode == DASHCAM_CAMERA_MODE_RECORD)
        {
            if (app_isp_camera_sp_channel_turn_on(app_camera_board_config_get()) != BK_OK)
            {
                LOGE("app_isp_camera_sp_channel_turn_on failed\n");
                (void)app_isp_camera_turn_off();
                return BK_FAIL;
            }
        }

        if (dashcam_camera_start_encoder_bond() != BK_OK)
        {
            dashcam_camera_stop_encoder_bond();
            (void)app_isp_camera_turn_off();
            return BK_FAIL;
        }
    }

    s_mode = mode;
    LOGI("camera open mode=%d record=%ux%u preview=%ux%u sp=%d\n",
         (int)mode,
         (unsigned)DASHCAM_RECORD_WIDTH,
         (unsigned)DASHCAM_RECORD_HEIGHT,
         (unsigned)DASHCAM_PREVIEW_WIDTH,
         (unsigned)DASHCAM_PREVIEW_HEIGHT,
         (mode == DASHCAM_CAMERA_MODE_RECORD) ? 1 : 0);
    return BK_OK;
}

void dashcam_camera_close(void)
{
    LOGD("close (mode=%d)\n", (int)s_mode);

    if (s_mode == DASHCAM_CAMERA_MODE_OFF)
    {
        return;
    }

    dashcam_camera_stop_encoder_bond();
    (void)app_isp_camera_turn_off();
    s_mode = DASHCAM_CAMERA_MODE_OFF;
    LOGI("camera closed\n");
}

dashcam_camera_mode_t dashcam_camera_get_mode(void)
{
    return s_mode;
}

int dashcam_camera_read_preview(uint8_t *frame, uint32_t size, uint32_t timeout_ms)
{
    uint8_t channel;

    if (s_mode == DASHCAM_CAMERA_MODE_OFF || frame == NULL)
    {
        return BK_FAIL;
    }

    /* RECORD_ONLY has no readable preview channel (MP is flexa-bonded to the
     * encoder, SP is off). Callers must not read preview in that mode. */
    if (s_mode == DASHCAM_CAMERA_MODE_RECORD_ONLY)
    {
        return BK_FAIL;
    }

    channel = (s_mode == DASHCAM_CAMERA_MODE_RECORD) ? APP_ISP_SP_CHN_ID : APP_ISP_MP_CHN_ID;
    return app_isp_camera_channel_read(channel, frame, size, timeout_ms);
}

uint32_t dashcam_camera_preview_width(void)
{
    return DASHCAM_PREVIEW_WIDTH;
}

uint32_t dashcam_camera_preview_height(void)
{
    return DASHCAM_PREVIEW_HEIGHT;
}
