set(incs include
    ${ARMINO_PATH}/components/multimedia/include
)
set(srcs "")

list(APPEND srcs
    src/media_msg.c
)

if (CONFIG_LCD OR CONFIG_BK_DISPLAY)
    list(APPEND srcs
        src/media_devices.c
    )
endif()

if (CONFIG_MEDIA_PIPELINE)
    list(APPEND srcs
        src/media_frame_psram_alloc.c
        src/media_data_process_que.c
        src/media_network_transfer.c
        src/media_navigation_transfer.c
    )
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include multimedia bk_network_transfer bk_vfs lvgl cast_pipeline bk_peripheral bk_network_provisioning display_ui
)

