set(incs .)
set(srcs "")

list(APPEND srcs
    network_provisioning.c
)

set(priv_reqs bk_common include bk_network_provisioning json bluetooth_dual media bk_network_transfer
              bk_bluetooth)

if (CONFIG_BK_MODEM)
    list(APPEND priv_reqs bk_modem driver bk_cli)
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES ${priv_reqs}
)
