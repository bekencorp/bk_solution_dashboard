set(incs
    .
)

set(srcs
    cp_main.c
    vnd_cal.c
)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}")
