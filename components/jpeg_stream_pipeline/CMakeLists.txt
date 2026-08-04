set(incs
    include
    ${ARMINO_PATH}/components/multimedia/include
)

set(srcs
    src/jpeg_stream_pipeline.c
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    # lvgl + avdk_driver are only used by the CONFIG_PROJECT_SCOOTER_V2 方案 B path
    # (lv_vendor_gpu_* serialization + shared vg_lite / bk_gpu driver re-borrow via
    # gpu_core.h). They are harmless link deps for projects that leave the macro off.
    PRIV_REQUIRES bk_common include bk_decoder bk_gpu avdk_utils bk_display media_service lvgl avdk_driver
)
