set(incs .)
set(srcs "")

list(APPEND srcs
    network_provisioning.c
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include bk_network_provisioning json bluetooth_dual media bk_network_transfer
                  bk_bluetooth
)
