set(incs .)
set(srcs "")

if (CONFIG_BUTTON)
    list(APPEND srcs
        key_app_service.c
    )
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include key bk_network_provisioning bk_wifi media
)

