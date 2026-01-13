set(incs .
    )

set(srcs
	ap_main.c
    )

if (CONFIG_OVERRIDE_FLASH_PARTITION)
    list(APPEND srcs
        vendor_flash.c
    )
endif()

if (CONFIG_LVGL)
    list(APPEND incs
            lvgl
        )

    list(APPEND srcs
            lvgl/lvgl_app_ui.c
        )

    list(APPEND incs
            lvgl/beken_generated
        )

    file(GLOB_RECURSE beken_generated_source
            lvgl/beken_generated/*.c
        )
    list(APPEND srcs ${beken_generated_source})
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_init bk_common media_service multimedia bk_cli audio_play bk_mp3_play
                  bk_network_transfer bk_display lvgl bk_video_player bluetooth_secondary_controller
                  bk_network_provisioning bk3515_ota bluetooth_dual decode key_app media network_provisioning
)
