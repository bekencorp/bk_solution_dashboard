set(video_player_glue_dir ${CMAKE_CURRENT_SOURCE_DIR}/video_player_glue)
set(beken_generated_dir ${CMAKE_CURRENT_SOURCE_DIR}/ui/beken_generated)

set(incs
    .
    klok_player
    music_player
    ${video_player_glue_dir}/include
    ${beken_generated_dir}
    ${beken_generated_dir}/fonts
    ${beken_generated_dir}/custom
)

set(srcs
    ap_main.c
    klok_mv_render.c
    klok_h264_pp_osd.c
    klok_lvgl_preview.c
    tp/klok_gt911.c
    klok_player/klok_player_adapter.c
    music_player/music_player_ui.c
    ${video_player_glue_dir}/src/video_player_cli.c
    ${video_player_glue_dir}/src/video_play_engine_api.c
    ${video_player_glue_dir}/src/video_play_callbacks.c
    ${video_player_glue_dir}/src/video_play_gpu_postprocess.c
    ${video_player_glue_dir}/src/audio_player_device.c
)

file(GLOB_RECURSE beken_generated_source
    ${beken_generated_dir}/*.c
)

list(REMOVE_ITEM beken_generated_source
    ${beken_generated_dir}/screen_saver_init.c
    ${beken_generated_dir}/image/icon_photo_add_140x140_RGB565A8_NONE.c
    ${beken_generated_dir}/image/klok_home_bg_ARGB8888.c
    ${beken_generated_dir}/image/ss_art_card_240x350_RGB565A8_NONE.c
    ${beken_generated_dir}/image/ss_clock_card_240x350_RGB565A8_NONE.c
    ${beken_generated_dir}/image/ss_scenery_card_240x350_RGB565A8_NONE.c
)

list(APPEND srcs ${beken_generated_source})

set(priv_req
    media_service
    bk_vfs
    multimedia_device_service
    avdk_utils
    bk_peripheral
    bk_gpu
    avdk_driver
    lvgl
    bk_cli
    bk_video_player
    key_app
    sdcard_mtp
    fatfs
    audio_play
    bk_audio_player
    bk_bluetooth
    bluetooth_dual
    bluetooth_secondary_controller
)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

# FL7703 always replaces the SDK GT911 with its address-probing/polling driver.
get_target_property(klok_bk_peripheral_sources __armino_bk_peripheral SOURCES)
list(FILTER klok_bk_peripheral_sources
     EXCLUDE REGEX "(/|^)src/tp/tp_gt911\\.c$")
set_property(TARGET __armino_bk_peripheral
             PROPERTY SOURCES "${klok_bk_peripheral_sources}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # SCH-7259 V1.0: HT6872 CTRL is connected to P13_MUTE.
    AUDIO_PLAYER_PA_CTRL_ENABLE=1
    AUDIO_PLAYER_PA_CTRL_GPIO=13
    AUDIO_PLAYER_PA_ON_LEVEL=1
    # 1: Flexa decoder rotates/compresses and submits directly to DPU.
    # 0: frame decoder with LVGL snapshot/VCDEC OSD fusion.
    KLOK_VIDEO_FLEXA_DIRECT_MODE=1
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)

# PP-OSD requires LVGL Snapshot. The SDK does not expose this switch through
# Kconfig, so point LVGL at the project-local override configuration.
set(KLOK_LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf_custom.h")
target_compile_definitions(__armino_lvgl PUBLIC "LV_CONF_PATH=\"${KLOK_LV_CONF_PATH}\"")
