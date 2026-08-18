if(NOT DEFINED DASHCAM_CONFIG_DIR OR DASHCAM_CONFIG_DIR STREQUAL "")
    armino_build_get_property(project_dir PROJECT_DIR)
    set(DASHCAM_CONFIG_DIR ${project_dir}/ap/dashcam)
endif()

set(MULTIMEDIA_DEVICE_SERVICE_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../multimedia_device_service
)

set(srcs
    src/dashcam_app.c
    src/dashcam_camera.c
    src/dashcam_h264_flexa_decoder.c
    src/dashcam_player.c
    src/dashcam_recorder.c
    src/dashcam_storage.c
    src/dashcam_video_v4.c
    src/dashcam_assitview.c
)

set(incs
    include
    ${DASHCAM_CONFIG_DIR}
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/include
)

set(priv_req
    bk_common
    media_service
    bk_display
    bk_peripheral
    avdk_utils
    lvgl
    bk_vfs
    media
    bk_gpu
    display_ui
    fatfs
    avdk_driver
    bk_camera
    bk_video_recorder
    bk_video_player
    bk_encoder
    app
    boot_avi_play
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES "${priv_req}"
)
