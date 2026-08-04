include(${CMAKE_CURRENT_LIST_DIR}/beken_generated/beken_generated.cmake)

set(MULTIMEDIA_DEVICE_SERVICE_DIR
    $ENV{ARMINO_TOOLS_PATH}/../projects/common_components/multimedia_device_service
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
    # Video surface: pick exactly ONE.
    #   dashcam_video.c    - legacy live-preview compositor (CPU page-composite blend)
    #   dashcam_video_v2.c - legacy live-preview via lv_camera_blend_process (GPU)
    #   dashcam_video_v3.c - playback-only (camera preview removed; the camera->display
    #                        path now lives in dashcam_assitview.c). Active.
    #   dashcam/dashcam_video.c
    #   dashcam/dashcam_video_v2.c
    dashcam/dashcam_video_v3.c
    dashcam/dashcam_player.c
    dashcam/dashcam_recorder.c
    dashcam/dashcam_storage.c
    dashcam/dashcam_camera.c
    dashcam/dashcam_app.c
    dashcam/dashcam_ui.c
    dashcam/dashcam_assitview.c
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
              bk_video_recorder bk_video_player bk_encoder app boot_avi_play sdcard_mtp bk_audio_player)

# Audio-metadata (ID3 / Vorbis comment) tag parsers used by the music_player page
# to read title/artist/duration.
#   - CONFIG_AUDIO_PLAYER ON  : the whole player (incl. these parsers) is built by
#     the bk_audio_player component, so we ONLY depend on it (added to priv_req
#     above). Compiling the sources here too would cause duplicate symbols.
#   - CONFIG_AUDIO_PLAYER OFF : the component builds nothing, so we compile just
#     the parser + common + mem sources here (lean: no decoders / player core).
if(NOT CONFIG_AUDIO_PLAYER)
    list(APPEND srcs
        ${BK_AUDIO_PLAYER_DIR}/core/player_mem.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_metadata_parser_common.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_mp3_metadata_parser.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_wav_metadata_parser.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_flac_metadata_parser.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_m4a_metadata_parser.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_aac_metadata_parser.c
        ${BK_AUDIO_PLAYER_DIR}/plugins/metadata_parsers/bk_audio_player_ogg_metadata_parser.c)
endif()
armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -O3)

# Workaround: SDK Kconfig missing LV_USE_GIF/LV_USE_FS_FATFS entries.
# Use LV_CONF_PATH to override lv_conf.h defaults. Remove when SDK is fixed.
set(LV_CONF_OVERRIDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf_override.h")
target_compile_definitions(__armino_lvgl PUBLIC "LV_CONF_PATH=\"${LV_CONF_OVERRIDE_PATH}\"")
