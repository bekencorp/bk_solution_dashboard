function(set_local_library_compile_option input_target)
    set(local_compile_option )

    set(TMP_WARNING_LIST)
    list(APPEND TMP_WARNING_LIST
                unused-but-set-variable
                unused-variable
                unused-function
                unused-but-set-variable
                unused-label
                #uninitialized
    )

    foreach(tmp_loopvar IN LISTS TMP_WARNING_LIST)
        list(APPEND local_compile_option -Wno-error=${tmp_loopvar} -Wno-${tmp_loopvar})
    endforeach()

    target_compile_options(${input_target} PRIVATE ${local_compile_option})
endfunction()

set(incs .)
set(srcs "")

if (CONFIG_BLE)
    list(APPEND incs
        gatt
        wifi_boarding
        ble_ota
    )

    list(APPEND srcs
        gatt/dm_gatt.c
        gatt/dm_gattc.c
        gatt/dm_gatts.c
        gatt/dm_gatt_cli.c
        gatt/dm_gatt_connection.c
        gatt/dm_gap_utils.c
        hogpd/hogpd_demo.c
        hogpd/hogpd_demo_cli.c
        wifi_boarding/wifi_boarding_demo.c
        wifi_boarding/wifi_boarding_demo_service.c
        wifi_boarding/wifi_boarding_demo_cli.c
        wifi_boarding/wifi_boarding_network.c
    )
endif()

if (CONFIG_BT)
    list(APPEND srcs
    )

    if (CONFIG_A2DP_SINK_DEMO)
        list(APPEND incs a2dp_sink)
        list(APPEND srcs
            a2dp_sink/a2dp_sink_demo.c
            a2dp_sink/a2dp_sink_audio.c
        )
        if (CONFIG_HFP_HF_DEMO)
            list(APPEND srcs
                a2dp_sink_demo_cli.c
            )
        endif()
    endif()

    if (CONFIG_HFP_HF_DEMO)
        list(APPEND incs hfp_hf)
        list(APPEND srcs
            hfp_hf/hfp_hf_demo.c
        )
    endif()
endif()

if (CONFIG_BLE OR CONFIG_BT)
    list(APPEND incs
    )

    list(APPEND srcs
    )
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common include blue_audio_player_service blue_audio_recorder_service sbc_encoder 
                  bluetooth_secondary_controller lwip_intf_v2_1 ota sw_sbc_decoder bk_bluetooth audio_play
)

get_filename_component(config_component_name ${CMAKE_CURRENT_LIST_DIR} NAME)
armino_component_get_target(target "${config_component_name}")
set_local_library_compile_option(${target})
