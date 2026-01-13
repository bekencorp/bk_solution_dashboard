set(incs .)
set(srcs "")

if (CONFIG_BK_BLE_PROVISIONING)
    list(APPEND srcs
        network_provisioning.c
    )
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include bk_network_provisioning json bluetooth_dual media
)

