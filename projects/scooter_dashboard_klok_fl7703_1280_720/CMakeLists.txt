# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)



include($ENV{ARMINO_TOOLS_PATH}/build_tools/cmake/project.cmake)

set(KLOK_COMPONENTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components)

# Only register components this project actually uses. Pointing EXTRA at the
# whole ../../components tree also pulls in dashcam/media/etc. and breaks the
# build when project-local headers (e.g. dashcam_config.h) are absent.
set(EXTRA_COMPONENTS_DIRS
    $ENV{ARMINO_TOOLS_PATH}/../projects/common_components/multimedia_device_service
    ${KLOK_COMPONENTS_DIR}/key_app
    ${KLOK_COMPONENTS_DIR}/sdcard_mtp
    ${KLOK_COMPONENTS_DIR}/bluetooth_dual
    ${KLOK_COMPONENTS_DIR}/bluetooth_secondary_controller
    ${KLOK_COMPONENTS_DIR}/blue_audio_player_service
    ${KLOK_COMPONENTS_DIR}/blue_audio_recorder_service
    ${KLOK_COMPONENTS_DIR}/sw_sbc_decoder
)

project(app)
