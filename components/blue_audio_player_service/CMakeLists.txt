set(incs include)
set(srcs "")

if (CONFIG_BLUE_AUDIO_PLAYER_SERVICE)
	list(APPEND srcs
		src/blue_audio_player_service.c
	)
endif()

if (CONFIG_BLUE_AUDIO_PLAYER_SERVICE_TEST)
	list(APPEND srcs
		src/cli_blue_audio_player.c
	)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common include
)