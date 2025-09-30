set(incs include)
set(srcs "")

if (CONFIG_BLUE_AUDIO_RECORDER_SERVICE)
	list(APPEND srcs
		src/blue_audio_recorder_service.c
	)
endif()

if (CONFIG_BLUE_AUDIO_RECORDER_SERVICE_TEST)
	list(APPEND srcs
		src/cli_blue_audio_recorder.c
	)
endif()

armino_component_register(
    SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_audio
)