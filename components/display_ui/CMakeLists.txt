set(incs
    .
    include
)

set(srcs
    display_ui.c
    display_ui_cast_hooks.c
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common multimedia bk_display bk_peripheral bk_frame_buffer bk_gpu lvgl avdk_utils cast_pipeline
)
