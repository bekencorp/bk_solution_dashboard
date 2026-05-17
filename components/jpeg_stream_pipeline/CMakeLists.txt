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
    PRIV_REQUIRES bk_common include bk_decoder bk_gpu avdk_utils bk_display media_service
)
