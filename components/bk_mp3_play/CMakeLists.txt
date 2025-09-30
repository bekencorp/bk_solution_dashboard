set(incs include)
set(srcs "")

if (CONFIG_MP3_PLAY)
	list(APPEND srcs
		src/bk_mp3_play.c
	)
endif()

if (CONFIG_MP3_PLAY_TEST)
	list(APPEND srcs
		src/cli_mp3_play.c
	)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common include
)

