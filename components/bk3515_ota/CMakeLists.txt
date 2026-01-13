set(incs include)
set(srcs "")

if (CONFIG_BK3515_OTA)
    list(APPEND srcs
        src/download.c
        src/ota_utils.c
    )
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include
)

