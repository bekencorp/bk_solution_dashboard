armino_build_get_property(klok_project_dir PROJECT_DIR)
set(klok_app_include_dir "${klok_project_dir}/ap")

set(klok_project_headers_available TRUE)
foreach(required_header
        klok_h264_pp_osd.h
        klok_lvgl_preview.h)
    if(NOT EXISTS "${klok_app_include_dir}/${required_header}")
        set(klok_project_headers_available FALSE)
        break()
    endif()
endforeach()

# ARMINO evaluates every component while collecting requirements, including
# components that the current project does not use. Keep this KLOK-specific
# component inert for other projects instead of failing their configuration.
if(NOT klok_project_headers_available)
    armino_component_register()
    return()
endif()

set(srcs
    src/video_player_cli.c
    src/video_play_engine_api.c
    src/video_play_callbacks.c
    src/video_play_gpu_postprocess.c
    src/audio_player_device.c
)

set(priv_req
    bk_common
    include
    driver
    media_service
    bk_frame_buffer
    bk_vfs
    multimedia_device_service
    avdk_utils
    bk_peripheral
    bk_gpu
    avdk_driver
    lvgl
    bk_cli
    bk_video_player
    fatfs
    audio_play
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS "${klok_app_include_dir}"
    PRIV_REQUIRES "${priv_req}"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AUDIO_PLAYER_PA_CTRL_ENABLE=1
    AUDIO_PLAYER_PA_CTRL_GPIO=13
    AUDIO_PLAYER_PA_ON_LEVEL=1
    KLOK_VIDEO_FLEXA_DIRECT_MODE=1
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
