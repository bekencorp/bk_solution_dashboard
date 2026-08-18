# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)



include($ENV{ARMINO_TOOLS_PATH}/build_tools/cmake/project.cmake)

set(EXTRA_COMPONENTS_DIRS
    $ENV{ARMINO_TOOLS_PATH}/../projects/common_components/multimedia_device_service
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/sdcard_mtp
)

project(app)