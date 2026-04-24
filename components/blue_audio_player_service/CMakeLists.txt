set(incs include)
set(srcs "")
set(priv_reqs bk_common include)

if (CONFIG_BLUE_AUDIO_PLAYER_SERVICE)
	list(APPEND srcs
		src/blue_audio_player_service.c
	)
endif()

if (CONFIG_SW_SBC_DECODER)
	list(APPEND incs
		../sw_sbc_decoder/include
	)
	list(APPEND priv_reqs sw_sbc_decoder)
endif()

if (CONFIG_BLUE_AUDIO_PLAYER_SERVICE_TEST)
	list(APPEND srcs
		src/cli_blue_audio_player.c
	)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES ${priv_reqs}
)