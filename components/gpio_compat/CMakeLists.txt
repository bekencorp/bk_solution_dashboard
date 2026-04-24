set(srcs "gpio_compat.c")

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver include
)
