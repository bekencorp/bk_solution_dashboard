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

#define TAG "d_camera"
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
/*
 * SCH-7259_V1.0 (dashboard v1) board: the GC2053 RSTN line is hardware
 * pulled high, so the sensor is released from reset without firmware help.
 * GPIO_71 on this board is BK3515NS BT_EN, so driving it as camera reset
 * would clash with Bluetooth bring-up. Use the 0xFF "no pin" sentinel so
 * csi_gc2053.c skips the reset toggle entirely (it guards on pin_reset != 0xFF).
 */
#ifndef DASHCAM_CAMERA_PIN_RESET
#define DASHCAM_CAMERA_PIN_RESET  ((uint8_t)-1)
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

static bool s_open = false;
static void *s_isp_encode_bond = NULL;
static bool s_encoder_on = false;

static void dashcam_camera_fill_board(camera_board_config_t *cfg)
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
    cfg->mipi.hmirror = DASHCAM_CAMERA_HMIRROR;
    cfg->mipi.vflip = DASHCAM_CAMERA_VFLIP;

    cfg->isp.mp_enable = true;
    cfg->isp.mp_format = BK_PIXEL_FORMAT_NV12;

    /* MP streams via flexa, feeding both the H264 encoder (recorder) and, when
     * the assist view is up, a second MP->GPU bond. The SP (live preview)
     * channel is never brought up so its NV12 buffers stay free (req5 §9.1 OOM
     * fix). */
    cfg->isp.mp_flexa = true;
    cfg->isp.mp_width = DASHCAM_RECORD_WIDTH;
    cfg->isp.mp_height = DASHCAM_RECORD_HEIGHT;
    cfg->isp.sp_enable = false;
    cfg->isp.sp_flexa = false;
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

bk_err_t dashcam_camera_open(void)
{
    camera_board_config_t cfg;

    LOGD("open req (open=%d)\n", (int)s_open);

    if (s_open)
    {
        return BK_OK;
    }

    dashcam_camera_fill_board(&cfg);
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

    if (dashcam_camera_start_encoder_bond() != BK_OK)
    {
        dashcam_camera_stop_encoder_bond();
        (void)app_isp_camera_turn_off();
        return BK_FAIL;
    }

    s_open = true;
    LOGI("camera open record=%ux%u\n",
         (unsigned)DASHCAM_RECORD_WIDTH,
         (unsigned)DASHCAM_RECORD_HEIGHT);
    return BK_OK;
}

void dashcam_camera_close(void)
{
    LOGD("close (open=%d)\n", (int)s_open);

    if (!s_open)
    {
        return;
    }

    dashcam_camera_stop_encoder_bond();
    (void)app_isp_camera_turn_off();
    s_open = false;
    LOGI("camera closed\n");
}

bool dashcam_camera_is_open(void)
{
    return s_open;
}
