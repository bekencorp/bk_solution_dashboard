include(${CMAKE_CURRENT_LIST_DIR}/beken_generated/beken_generated.cmake)

# display_ui now always drives the panel through the app_display layer
# (app_mipi_lcd_turn_on), so this project must compile app_display.c and expose
# its header, mirroring scooter_1280_720_v2.
set(MULTIMEDIA_DEVICE_SERVICE_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../../../components/multimedia_device_service
)

set(incs .
    ${BEKEN_GENERATOR_INCLUDE_DIRS}
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/include
)

set(srcs
    ap_main.c
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/src/app_display.c
    ${BEKEN_GENERATOR_SOURCES}
    )

set(priv_req  bk_init bk_common media_service bk_cli bk_display bk_peripheral avdk_utils lvgl
              bk_network_provisioning easy_flash
              bluetooth_dual bluetooth_secondary_controller
              bk_network_transfer media network_provisioning key_app bk_gpu display_ui
              jpeg_stream_pipeline fatfs bk_bluetooth boot_avi_play)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -O3)

# Workaround: SDK Kconfig missing LV_USE_GIF/LV_USE_FS_FATFS entries.
# Use LV_CONF_PATH to override lv_conf.h defaults. Remove when SDK is fixed.
set(LV_CONF_OVERRIDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf_override.h")
target_compile_definitions(__armino_lvgl PUBLIC "LV_CONF_PATH=\"${LV_CONF_OVERRIDE_PATH}\"")
