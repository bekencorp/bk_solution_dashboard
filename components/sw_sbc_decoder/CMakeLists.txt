set(incs include)
set(srcs "")

if (CONFIG_SW_SBC_DECODER)
	list(APPEND srcs
		src/sw_sbc_decoder.c
		src/sbc.c
		src/bits.c
		src/sw_pcm_tap.c
		src/sw_null_sink.c
	)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common include driver
)
