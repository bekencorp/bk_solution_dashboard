set(incs .)
set(srcs "")

if (CONFIG_BLUETOOTH_MULTI_CONTROLLER)
	list(APPEND incs
		include
	)

	list(APPEND srcs
		bsc_main.c
	)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common include
)
