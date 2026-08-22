set(incs .
    ${ARMINO_PATH}/components/multimedia/include
)

set(srcs
    cast_jpeg_pipeline.c
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include jpeg_stream_pipeline bk_gpu avdk_driver avdk_utils bk_display bk_frame_buffer
)
