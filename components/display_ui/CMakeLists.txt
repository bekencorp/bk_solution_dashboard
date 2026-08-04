# Only the CONFIG_PROJECT_SCOOTER_V2 path pulls in app_display.h (app_mipi_lcd_turn_on
# + display_board_config_t) from multimedia_device_service. Adding the search path
# unconditionally is harmless for projects that leave the macro off.
set(MULTIMEDIA_DEVICE_SERVICE_DIR
    $ENV{ARMINO_TOOLS_PATH}/../projects/common_components/multimedia_device_service
)

set(incs
    .
    include
    ${MULTIMEDIA_DEVICE_SERVICE_DIR}/include
)

set(srcs
    display_ui.c
    display_ui_cast_hooks.c
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common  bk_display bk_peripheral bk_frame_buffer bk_gpu lvgl avdk_utils cast_pipeline
)
