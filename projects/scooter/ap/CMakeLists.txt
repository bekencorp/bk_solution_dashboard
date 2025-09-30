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

if(CONFIG_BLE)
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
    list(APPEND incs
        hfp_hf
        a2dp_sink
    )

    list(APPEND srcs
        a2dp_sink/a2dp_sink_demo.c
        a2dp_sink/mpeg4_latm_dec.c
        a2dp_sink_demo_cli.c
        hfp_hf/hfp_hf_demo.c
        hfp_hf/ring_buffer_particle.c
        bt_manager.c
    )
endif()

if (CONFIG_BLE OR CONFIG_BT)
    list(APPEND incs
        storage
    )

    list(APPEND srcs
        storage/bluetooth_storage.c
    )
endif()

if (CONFIG_LVGL)
    list(APPEND incs
        lvgl
    )

    list(APPEND srcs
        lvgl/lvgl_app_ui.c
    )

if (CONFIG_LV_USE_DEMO_METER)
    list(APPEND srcs
        lvgl/lv_example_meter.c
    )
endif()

endif()

if (1)
    list(APPEND incs
        media/include
    )
if (1)
    list(APPEND srcs
        media/src/media_comm.c
        media/src/media_transmission.c
        media/src/media_udp_service.c
        media/src/media_tcp_service.c
        media/src/media_devices.c
        media/src/media_data_process.c
        media/src/media_data_process_que.c
        media/src/trans_list.c
        media/src/wifi_co_list.c
        media/src/test_jpeg.c
    )
endif()#CONFIG_MEDIA_APP

endif()#CONFIG_MEDIA_RECEIVE_DEMO

    list(APPEND incs
        decode
    )
    list(APPEND srcs
        decode/jpeg_decode_manager.c
    )

if (CONFIG_USB_CAMERA AND CONFIG_MEDIA_PIPELINE)
    list(APPEND srcs
            camera_manager.c
            h264_encode_manager.c
            frame_buffer_que.c
        )
if (CONFIG_VFS)
    list(APPEND srcs
        h264_encode_storage.c)
endif()#CONFIG_VFS

endif()#CONFIG_USB_CAMERA

if (CONFIG_BK3515_OTA)
    list(APPEND incs
        bk3515_ota/include
    )

    list(APPEND srcs
        bk3515_ota/src/download.c
        bk3515_ota/src/ota_utils.c
    )
endif()


armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_init media_service multimedia bk_cli audio_play audio_record lwip_intf_v2_1 ota wifi_transfer bk_display lvgl bk_mp3_play bk_camera bk_video_pipeline bk_vfs blue_audio_player_service blue_audio_recorder_service sbc_encoder bk_common bk_wifi bk_https easy_flash bk_video_player bluetooth_secondary_controller
)

get_filename_component(config_component_name ${CMAKE_CURRENT_LIST_DIR} NAME)
armino_component_get_target(target "${config_component_name}")
set_local_library_compile_option(${target})
