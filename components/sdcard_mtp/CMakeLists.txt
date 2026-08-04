set(srcs "sdcard_mtp.c")

# bk_usb  -> usb_mtp_init/deinit + bk_usb_mtp.h (MTP engine lives there, under CONFIG_USBD_MTP)
# bk_cli  -> cli_register_commands / struct cli_command
# bk_common -> logging + os_str/os_mem helpers
# bk_vfs  -> bk_posix.h opendir/readdir/mount + /sd0 partition defs (for `mtp ls`)
armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bk_usb bk_cli bk_common bk_vfs
)
