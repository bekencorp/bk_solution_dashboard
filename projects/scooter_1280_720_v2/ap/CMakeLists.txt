include(${CMAKE_CURRENT_LIST_DIR}/beken_generated/beken_generated.cmake)

set(MULTIMEDIA_DEVICE_SERVICE_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../../../components/multimedia_device_service
)

# SDK audio-player component root (sits beside the Solution tree; see project
# Makefile: SDK_DIR ?= ../../../SDK).
set(BK_AUDIO_PLAYER_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../../../../SDK/ap/components/bk_audio_player
)

set(incs .
    dashcam
    ota
    home
    phone_book
    music_player
    ${BEKEN_GENERATOR_INCLUDE_DIRS}
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/include
    ${BK_AUDIO_PLAYER_DIR}
    ${BK_AUDIO_PLAYER_DIR}/include
)

set(srcs
    ap_main.c
    dashcam/dashcam_ui.c
    ota/ota_ui.c
    home/home_ui.c
    phone_book/phone_book_ui.c
    music_player/music_player_ui.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/app_camera.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/app_codec.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/app_gpu.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/app_display.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/app_h264_encode.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/doorbell_img_manager.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/encode_frame_que.c
    ${BEKEN_GENERATOR_SOURCES}
    )

set(priv_req  bk_init bk_common media_service bk_cli bk_display bk_peripheral avdk_utils lvgl
              bk_network_provisioning easy_flash bk_vfs
              bluetooth_dual bluetooth_secondary_controller
              bk_network_transfer media network_provisioning key_app bk_gpu display_ui
              jpeg_stream_pipeline fatfs bk_bluetooth avdk_driver bk_camera
              bk_video_recorder bk_video_player bk_encoder app boot_avi_play
              bk_audio_player dashcam)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -O3)

# Workaround: SDK Kconfig missing LV_USE_GIF/LV_USE_FS_FATFS entries.
# Use LV_CONF_PATH to override lv_conf.h defaults. Remove when SDK is fixed.
set(LV_CONF_OVERRIDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf_override.h")
target_compile_definitions(__armino_lvgl PUBLIC "LV_CONF_PATH=\"${LV_CONF_OVERRIDE_PATH}\"")
