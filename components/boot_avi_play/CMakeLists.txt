set(incs
    include
    ${ARMINO_PATH}/components/multimedia/include
)

set(srcs
    src/boot_avi_play.c
    src/boot_sd_mount.c
    src/boot_bg_preload.c
)

# bk_vfs always calls armino_component_register (empty when CONFIG_VFS is off), so
# requiring it is safe for projects that leave VFS off (scooter_1280_720): it adds
# nothing there, and exposes bk_partition.h / bk_vfs.h for the CONFIG_VFS mount
# path (scooter_1280_720_v2). lvgl is needed by boot_bg_preload (TJPGD software
# decode into an lv_image_dsc_t).
set(reqs
    bk_common
    include
    bk_display
    fatfs
    bk_vfs
    media_service
    avdk_utils
    bk_gpu
    lvgl
    jpeg_stream_pipeline
    display_ui
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES ${reqs}
)
