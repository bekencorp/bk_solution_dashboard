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

if (CONFIG_LCD_PANEL_USE_480X272)
    list(APPEND incs
            lvgl/lv_ui_480x272
        )

    list(APPEND srcs
            lvgl/lv_ui_480x272/basic_callback.c
            lvgl/lv_ui_480x272/beken_ui.c
            lvgl/lv_ui_480x272/page_1_init.c
            lvgl/lv_ui_480x272/page_2_init.c

            lvgl/lv_ui_480x272/images/battery_bak_28x20_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/battery_bak_38x23_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/battery_ind_28x21_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/battery_ind_34x22_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/beken_logo_blue_76x42_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/bluetooth_23x23_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/bluetooth_28x28_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/kmbg_113x110_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/light_11_29x22_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/light_11_34x26_RGB565A8_NONE.c

            lvgl/lv_ui_480x272/images/light_green_30x30_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/light_green_45x45_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/light_grey_30x30_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/light_grey_46x46_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/moto_ARGB8888.c
            lvgl/lv_ui_480x272/images/wifi_1_23x23_RGB565A8_NONE.c
            lvgl/lv_ui_480x272/images/wifi_1_26x26_RGB565A8_NONE.c
        )
elseif(CONFIG_LCD_PANEL_USE_800X480)
    list(APPEND incs
            lvgl/lv_ui_800x480
        )

    list(APPEND srcs
            lvgl/lv_ui_800x480/basic_callback.c
            lvgl/lv_ui_800x480/beken_ui.c
            lvgl/lv_ui_800x480/page_1_init.c

            lvgl/lv_ui_800x480/images/battery_bak_57x35_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/battery_ind_47x35_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/beken_logo_blue_118x65_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/bluetooth_40x42_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/kmbg_207x212_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/light_11_52x37_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/light_11_70x56_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/light_green_70x74_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/light_grey_70x74_RGB565A8_NONE.c
            lvgl/lv_ui_800x480/images/moto1_800x480_ARGB8888.c
            lvgl/lv_ui_800x480/images/wifi_1_40x42_RGB565A8_NONE.c
        )
else()
    list(APPEND incs
            lvgl/lv_ui_1024x600
        )

    list(APPEND srcs
            lvgl/lv_ui_1024x600/basic_callback.c
            lvgl/lv_ui_1024x600/beken_ui.c
            lvgl/lv_ui_1024x600/page_1_init.c

            lvgl/lv_ui_1024x600/images/battery_bak_81x51_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/battery_ind_73x49_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/beken_logo_blue_162x93_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/bluetooth_49x51_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/kmbg_241x243_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/light_11_85x58_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/light_green_96x99_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/light_grey_98x101_RGB565A8_NONE.c
            lvgl/lv_ui_1024x600/images/moto_ARGB8888.c
            lvgl/lv_ui_1024x600/images/wifi_1_49x51_RGB565A8_NONE.c
        )
endif()
endif()

if (1)
    list(APPEND incs
        media/include
        network_provisioning
    )
if (1)
    list(APPEND srcs
        media/src/media_devices.c
        media/src/media_data_process_que.c
        media/src/test_jpeg.c
        media/src/media_msg.c
        media/src/media_network_transfer.c
        media/src/media_navigation_transfer.c
    )
endif()

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

if (CONFIG_BK_BLE_PROVISIONING)
    list(APPEND srcs
        network_provisioning/network_provisioning.c
    )
endif()

if (CONFIG_BUTTON)
    list(APPEND incs
        key_app
    )
    list(APPEND srcs
        key_app/key_app_service.c
    )
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_init media_service multimedia bk_cli audio_play audio_record lwip_intf_v2_1 ota bk_network_transfer bk_display lvgl bk_mp3_play bk_camera bk_video_pipeline bk_vfs blue_audio_player_service blue_audio_recorder_service sbc_encoder bk_common bk_wifi bk_https easy_flash bk_video_player bluetooth_secondary_controller bk_network_provisioning json key
)

get_filename_component(config_component_name ${CMAKE_CURRENT_LIST_DIR} NAME)
armino_component_get_target(target "${config_component_name}")
set_local_library_compile_option(${target})
