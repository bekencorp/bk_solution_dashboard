set(incs include)
set(srcs "")

list(APPEND srcs
    src/media_devices.c
    src/media_data_process_que.c
    src/test_jpeg.c
    src/media_msg.c
    src/media_network_transfer.c
    src/media_navigation_transfer.c
)

if (CONFIG_USB_CAMERA AND CONFIG_MEDIA_PIPELINE)
    list(APPEND srcs
        src/camera_manager.c
        src/h264_encode_manager.c
        src/frame_buffer_que.c
    )

if (CONFIG_VFS)
    list(APPEND srcs
        src/h264_encode_storage.c
    )
endif()
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include multimedia bk_network_transfer bk_vfs lvgl decode bk_peripheral network_provisioning bk_network_provisioning
)

