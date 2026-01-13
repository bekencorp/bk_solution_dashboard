set(incs .)
set(srcs "")

list(APPEND srcs
    jpeg_decode_manager.c
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include multimedia media network_provisioning bk_network_provisioning
)

