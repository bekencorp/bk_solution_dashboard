#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/usbh_hub_multiple_classes_api.h>
#include <components/bk_camera_ctlr.h>
#include "frame_buffer_que.h"

#define TAG "camera_cli"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

/**
 * @brief UVC test information structure
 * 
 * This structure contains all the necessary information for UVC device testing,
 * including synchronization primitives, port information, and device handles.
 */
typedef struct
{
    beken_semaphore_t uvc_connect_semaphore;    ///< Semaphore for UVC connection synchronization
    beken_mutex_t uvc_mutex;                    ///< Mutex for protecting shared resources
    bk_usb_hub_port_info *port_info;            ///< USB hub port information
    bk_camera_ctlr_handle_t handle;             ///< camera controller handle
} uvc_test_info_t;

static uvc_test_info_t *uvc_test_info = NULL;

static avdk_err_t uvc_test_info_init(void)
{
    if (uvc_test_info == NULL)
    {
        uvc_test_info = os_malloc(sizeof(uvc_test_info_t));
        if (uvc_test_info == NULL)
        {
            LOGE("%s: malloc uvc_test_info failed\n", __func__);
            return AVDK_ERR_NOMEM;
        }
        os_memset(uvc_test_info, 0, sizeof(uvc_test_info_t));

        if (rtos_init_semaphore(&uvc_test_info->uvc_connect_semaphore, 1) != AVDK_ERR_OK)
        {
            LOGE("%s: init semaphore failed\n", __func__);
            os_free(uvc_test_info);
            uvc_test_info = NULL;
            return AVDK_ERR_NOMEM;
        }

        if (rtos_init_mutex(&uvc_test_info->uvc_mutex) != AVDK_ERR_OK)
        {
            LOGE("%s: init mutex failed\n", __func__);
            rtos_deinit_semaphore(&uvc_test_info->uvc_connect_semaphore);
            os_free(uvc_test_info);
            uvc_test_info = NULL;
            return AVDK_ERR_NOMEM;
        }
    }

    return AVDK_ERR_OK;
}

static avdk_err_t uvc_test_info_deinit(void)
{
    if (uvc_test_info)
    {
        if (uvc_test_info->port_info)
        {
            LOGE("%s, %d, port_info not NULL\n", __func__, __LINE__);
            return AVDK_ERR_INVAL;
        }

        rtos_deinit_mutex(&uvc_test_info->uvc_mutex);
        rtos_deinit_semaphore(&uvc_test_info->uvc_connect_semaphore);
        os_free(uvc_test_info);
        uvc_test_info = NULL;
    }

    return AVDK_ERR_OK;
}

static frame_buffer_t *uvc_frame_malloc(image_format_t format, uint32_t size)
{
    return frame_queue_malloc(format, size);
}

static void uvc_frame_complete(uint8_t port, image_format_t format, frame_buffer_t *frame, int result)
{
    if (frame->sequence % 30 == 0)
    {
        LOGD("%s: port:%d, seq:%d, length:%d, format:%s, h264_type:%x, result:%d\n", __func__, port, frame->sequence,
            frame->length, (format == IMAGE_MJPEG) ? "mjpeg" : "h264", frame->h264_type, result);
    }

    frame_queue_complete(format, frame);
}

static void uvc_connect_successful_callback(bk_usb_hub_port_info *port_info, void *arg)
{
    LOGI("%s, %d\n", __func__, __LINE__);
    uvc_test_info_t *info = (uvc_test_info_t *)arg;
    if (info == NULL)
        return;
    rtos_lock_mutex(&info->uvc_mutex);
    info->port_info = port_info;
    rtos_set_semaphore(&info->uvc_connect_semaphore);
    rtos_unlock_mutex(&info->uvc_mutex);
}

static void uvc_disconnect_callback(bk_usb_hub_port_info *port_info, void *arg)
{
    LOGI("%s, %d\n", __func__, __LINE__);
    uvc_test_info_t *info = (uvc_test_info_t *)arg;
    if (info == NULL)
        return;
    rtos_lock_mutex(&info->uvc_mutex);
    info->port_info = NULL;
    rtos_unlock_mutex(&info->uvc_mutex);
}

static avdk_err_t uvc_camera_power_on_handle(uvc_test_info_t *info, uint32_t timeout)
{
    // Power on the camera device and check have connected already
    uint8_t port = 1;
    bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, uvc_connect_successful_callback, info);
    bk_usbh_hub_port_register_disconnect_callback(port, USB_UVC_DEVICE, uvc_disconnect_callback, info);
    bk_usbh_hub_multiple_devices_power_on(USB_HOST_MODE, port, USB_UVC_DEVICE);

    // After power-on is completed, check if connection is successful
    avdk_err_t ret = bk_usbh_hub_port_check_device(port, USB_UVC_DEVICE, &info->port_info);

    // If not checked successfully, wait for the connection success callback
    if (ret != AVDK_ERR_OK)
    {
        ret = rtos_get_semaphore(&info->uvc_connect_semaphore, timeout);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d, timeout:%d\n", __func__, __LINE__, timeout);
        }
    }

    return ret;
}

static avdk_err_t uvc_camera_power_off_handle(uvc_test_info_t *info)
{
    // Before power-off, check if all ports are closed
    uint8_t port = 1;
    if (info == NULL)
    {
        LOGE("%s, %d: info is NULL\n", __func__, __LINE__);
        return AVDK_ERR_INVAL;
    }

    if (info->handle != NULL)
    {
        LOGW("%s, %d: handle not closed\n", __func__, __LINE__);
        return AVDK_ERR_BUSY;
    }

    rtos_get_semaphore(&info->uvc_connect_semaphore, BEKEN_NO_WAIT);
    bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, NULL, NULL);
    bk_usbh_hub_port_register_disconnect_callback(port, USB_UVC_DEVICE, NULL, NULL);
    bk_usbh_hub_multiple_devices_power_down(USB_HOST_MODE, port, USB_UVC_DEVICE);
    info->port_info = NULL;

    return AVDK_ERR_OK;
}

static void uvc_event_callback(bk_usb_hub_port_info *port_info,void *arg, uvc_error_code_t code)
{
    LOGD("uvc_event_callback: %d", code);
}

static const bk_uvc_callback_t uvc_cbs = {
    .malloc = uvc_frame_malloc,
    .complete = uvc_frame_complete,
    .uvc_event_callback = uvc_event_callback,
};


avdk_err_t uvc_checkout_port_info_mjpeg(uvc_test_info_t *info, bk_cam_uvc_config_t *user_config)
{
    uint8_t frame_num = 0;
    uint8_t index = 0;
    uint8_t resolution_flag = false;
    uint8_t fps_flag = false;
    uint8_t format_flag = false;

    // Need to lock protection when accessing port_info
    rtos_lock_mutex(&info->uvc_mutex);
    bk_usb_hub_port_info *uvc_port_info = info->port_info;
    if (uvc_port_info == NULL)
    {
        rtos_unlock_mutex(&info->uvc_mutex);
        LOGE("%s: uvc_port_info is NULL\n", __func__);
        return AVDK_ERR_INVAL;
    }

    bk_uvc_device_brief_info_t *uvc_device_param = (bk_uvc_device_brief_info_t *)uvc_port_info->usb_device_param;

    // Add PID/VID and other log information
    LOGD("PORT:0x%x\r\n", user_config->port);
    LOGD("VID:0x%x\r\n", uvc_device_param->vendor_id);
    LOGD("PID:0x%x\r\n", uvc_device_param->product_id);
    LOGD("BCD:0x%x\r\n", uvc_device_param->device_bcd);

    frame_num = uvc_device_param->all_frame.mjpeg_frame_num;
    for (index = 0; index < frame_num; index++)
    {
        format_flag = true;
        LOGD("MJPEG width:%d heigth:%d index:%d\r\n",
             uvc_device_param->all_frame.mjpeg_frame[index].width,
             uvc_device_param->all_frame.mjpeg_frame[index].height,
             uvc_device_param->all_frame.mjpeg_frame[index].index);

        if (uvc_device_param->all_frame.mjpeg_frame[index].width == user_config->width
            && uvc_device_param->all_frame.mjpeg_frame[index].height == user_config->height)
        {
            resolution_flag = true;
        }

        // iterate all support fps of current resolution
        for (int i = 0; i < uvc_device_param->all_frame.mjpeg_frame[index].fps_num; i++)
        {
            LOGD("MJPEG fps:%d\r\n", uvc_device_param->all_frame.mjpeg_frame[index].fps[i]);

            if (resolution_flag
                && uvc_device_param->all_frame.mjpeg_frame[index].fps[i] == user_config->fps)
            {
                fps_flag = true;
            }
        }

        if (resolution_flag)
        {
            // have adapt this resolution
            if (fps_flag == false)
            {
                user_config->fps = uvc_device_param->all_frame.mjpeg_frame[index].fps[0];
                fps_flag = true;
            }
            break;
        }
    }

    // Unlock at the end of the function to ensure all access to shared resources is safe
    rtos_unlock_mutex(&info->uvc_mutex);

    if (format_flag == false)
    {
        LOGE("%s, not support this format:%d\r\n", __func__, user_config->img_format);
        return AVDK_ERR_UNSUPPORTED;
    }

    if (resolution_flag == false)
    {
        LOGE("%s, not support this resolution:%dX%d\r\n", __func__, user_config->width, user_config->height);
        return AVDK_ERR_UNSUPPORTED;
    }

    return AVDK_ERR_OK;
}

avdk_err_t uvc_camera_open(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint8_t all_closed = true;
    avdk_err_t ret = uvc_test_info_init();
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: uvc_test_info_init failed\n", __func__);
        return ret;
    }

    ret = frame_queue_init_all();
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: frame_queue_init_all failed\n", __func__);
        return ret;
    }

    if (uvc_test_info->handle)
    {
        LOGE("%s, %d, already opened\n", __func__, __LINE__);
        ret = AVDK_ERR_OK;
        return ret;
    }

    // Power on the camera device and check have connected
    ret = uvc_camera_power_on_handle(uvc_test_info, 4000);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: uvc_camera_power_on_handle failed\n", __func__);
        goto exit;
    }

    bk_uvc_ctlr_config_t uvc_ctlr_config = {
        .config = BK_UVC_864X480_30FPS_MJPEG_CONFIG(),
        .cbs = &uvc_cbs,
    };

    if (argc >= 5)
    {
        uvc_ctlr_config.config.width = os_strtoul(argv[3], NULL, 10);
        uvc_ctlr_config.config.height = os_strtoul(argv[4], NULL, 10);
    }

    uvc_ctlr_config.config.user_data = uvc_test_info;

    // 检查是否支持用户输入的分辨率和帧率，当前只检查MJPEG格式，因为默认输出MJPEG格式
    ret = uvc_checkout_port_info_mjpeg(uvc_test_info, &uvc_ctlr_config.config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: uvc_checkout_port_info_mjpeg failed\n", __func__, __LINE__);
        goto exit;
    }

    ret = bk_camera_uvc_ctlr_new(&uvc_test_info->handle, &uvc_ctlr_config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: bk_camera_uvc_ctlr_new failed\n", __func__, __LINE__);
        goto exit;
    }

    ret = bk_camera_open(uvc_test_info->handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: bk_camera_open failed\n", __func__, __LINE__);
        bk_camera_delete(uvc_test_info->handle);
        uvc_test_info->handle = NULL;
        goto exit;
    }

    LOGD("%s open successful\n", __func__);

    return ret;

exit:

    uvc_camera_power_off_handle(uvc_test_info);
    return ret;
}

avdk_err_t uvc_camera_close(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (uvc_test_info == NULL)
    {
        LOGE("%s, %d: uvc_test_info is NULL\n", __func__, __LINE__);
        return AVDK_ERR_INVAL;
    }

    if (uvc_test_info->handle == NULL)
    {
        LOGE("%s: camera handle is NULL\n", __func__);
        return AVDK_ERR_INVAL;
    }

    avdk_err_t ret =  bk_camera_close(uvc_test_info->handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: camera close fail\n", __func__);
    }
    else
    {
        bk_camera_delete(uvc_test_info->handle);
        uvc_test_info->handle = NULL;
    }

    ret = uvc_camera_power_off_handle(uvc_test_info);

    return ret;
}