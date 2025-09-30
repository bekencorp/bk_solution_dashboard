#ifndef __BEKEN_DOWNLOAD_H_
#define __BEKEN_DOWNLOAD_H_

#include "stdint.h"


#define BK3515N_BOOTUP_PIN  CONFIG_BLUETOOTH_BSC_RESET_PIN //RESET PIN

enum {
    POWER_OFF = 0,
    POWER_ON = 1,
};

enum{
    NO_ERROR                = 0,
    OPEN_ERROR              = -1,
    SEEK_ERROR              = -2,
    WRITE_DATA_ERROR        = -3,
    READ_DATA_ERROR         = -4,
    READ_ERROR_DATA_ERROR   = -5,
    HANDSHAKE_TIMEOUT_ERROR = -6,
    ERASE_FLASH_ERROR       = -7,//if erase flash, must ensure success
    DOWNLOAD_FLASH_ERROR    = -8,
    CHECK_CRC_ERROR         = -9,
    FLASH_PROTECT_ON_ERROR  = -10,
    FLASH_PROTECT_OFF_ERROR = -11,
    WRITE_FLASH_WR_QE_ERROR = -12,
    REBOOT_DUT_EX_ERROR     = -13,
    OPEN_TTY_FILE_LOCK_ERR  = -14,
    BIN_ERR                 = -15,
};

enum image_flag
{
    IMAGE_MCU        =   0x01,
    IMAGE_DSP        =   0x02,
    IMAGE_ENV        =   0x03,
    IMAGE_ENV_ADDR   =   0x04,
    IMAGE_MCU_ENV    =   0x05,
};

typedef struct
{
    uint16_t mark;          //2
    uint16_t flag;          //2
    uint16_t crc;
    uint32_t len;           //4
    uint32_t addr1;         //4
    uint32_t addr2;         //4
    uint8_t reserved[12];
    uint16_t info_crc;
}__attribute__((packed)) driver_ota_info_s;



#define OTA_INFO_BT_ADDR_ADDR_OFFSET    (32)
#define OTA_INFO_BT_NAME_ADDR_OFFSET    (OTA_INFO_BT_ADDR_ADDR_OFFSET + 6)
#define OTA_INFO_LEN             30

#define OTA_INFO_HOST_BIN_SIZE_OFFSET     0x58
#define OTA_INFO_HOST_HEAD_SIZE           0x60

/* ota location mark */
#define OTA_MARK_INIT            0xFFFF
#define OTA_MARK_ONGO            0xFF55
#define OTA_MARK_FAIL            0x0000
#define OTA_MARK_SUCC            0x5555
#define OTA_MARK_USED            0x0055

#define READ_CMD1 "\x01\xE0\xFC\xFF\xF4\x05\x00\x09"
#define READ_CMD1_SIZE 8
#define READ_ADDR_SIZE 4
#define READ_PACK_HEAD (READ_CMD1_SIZE + READ_ADDR_SIZE)

#define DOWNLOAD_CMD1 "\x01\xE0\xFC\xFF\xF4\x05\x10\x07"
#define DOWNLOAD_CMD1_SIZE 8
#define DOWNLOAD_ADDR_SIZE 4
#define DOWNLOAD_PACK_HEAD (DOWNLOAD_CMD1_SIZE + DOWNLOAD_ADDR_SIZE)

#define DOWNLOAD_CMD2 "\x01\xE0\xFC\x02\x01\x00"

#define FIXABLE_LENGTH_ERASE_CMD "\x01\xE0\xFC\xFF\xF4\x06\x00\x0F"
#define FIXABLE_LENGTH_ERASE_CMD_SIZE 8
#define FIXABLE_LENGTH_ERASE_ADDR_SIZE 4

#define FIXABLE_LENGTH_WRITE_CMD "\x01\xE0\xFC\xFF\xF4\x00\x00\x06"
#define FIXABLE_LENGTH_WRITE_CMD_SIZE 8
#define FIXABLE_LENGTH_WRITE_ADDR_SIZE 4

#define FIXABLE_LENGTH_READ_CMD "\x01\xE0\xFC\xFF\xF4\x00\x00\x08"
#define FIXABLE_LENGTH_READ_CMD_SIZE 8
#define FIXABLE_LENGTH_READ_ADDR_SIZE 4

#define VERSION_CMD "\x01\xA1\xFC\x00"
#define CHIP_CMD "\x01\xB4\xFC\x00"
#define CHIP_FLASH_CMD "\x01\xE0\xFC\xFF\xF4\x06\x00\x08\x10\x01\x00\x00\x08"


#define BUILD_VERSION_SIZE sizeof("000 00 0000 00:00:00")

#define USE_FILE_LOCK 1
#define BEKEN_UART_FILE_LOCK "/data/misc/bluetooth/.beken_bt_uart_pid"

#define FORCE_DOWNLOAD 1
#define BACKUP_DOWNLOAD 0


#define ERASE_FLASH_CMD "\x01\xE0\xFC\xFF\xF4\x02\x00\x0A\xFF"
#define ERASE_4K_FLASH_CMD "\x01\xE0\xFC\xFF\xF4\x05\x00\x0B"
#define ERASE_4K_FLASH_CMD_SIZE 8
#define ERASE_FLASH_ADDR_SIZE 4

#define MSEC_TIMEOUT 10
#define HANDSHAKE_CMD "\x01\xE0\xFC\x02\xAA\x55"
#define HANDSHAKE_ACK "\x04\x0E\x05\x01\xE0\xFC\xAA\x55"
#define MAX_RETRY ((30 * 1000) / MSEC_TIMEOUT)

#define REG1_MSEC_TIMEOUT 100
#define REG1_MAX_RETRY (1000 / REG1_MSEC_TIMEOUT)
#define FLASH_REG1_CMD2 "\x01\xE0\xFC\xFF\xF4\x02\x00\x0C\x05"
#define FLASH_REG2_CMD2 "\x01\xE0\xFC\xFF\xF4\x02\x00\x0C\x35"

#define RESET_DUT_CMD "\x01\xE0\xFC\x05\xFE\x95\x27\x95\x27"

#define REBOOT_DUT_EX_CMD "\x01\xE0\xFC\x02\x0E\xA5"

#define MID_MSEC_TIMEOUT 1000
#define FLASH_MID_CMD "\x01\xE0\xFC\xFF\xF4\x05\x00\x0E\x9F\x00\x00\x00"
#define MID_MAX_RETRY (100000 / MID_MSEC_TIMEOUT)

#define HCI_SOFTRESET_RSP "\x04\x0E\x04\x01\xA0\xFC\x00"

#define BAUD_ACK "\x04\x0e\x09\x01\xe0\xfc\x0f"

#define FLASH_REG1_PLUS_CMD2 "\x01\xE0\xFC\xFF\xF4\x02\x00\x0C\x05"
#define FLASH_REG1_PLUS_CMD3 "\x01\xE0\xFC\xFF\xF4\x02\x00\x0C\x35"

#define WRITE_STATUS_REGISTER_ENABLE "\x01\xE0\xFC\xFF\xF4\x02\x00\x0E\x50"

#define CHECK_FLASH_STATUS_CMD "\x01\xE0\xFC\xFF\xF4\x02\x00\x0C\x05"

#ifndef VENDOR_BTPOWER_PROC_NODE
#define VENDOR_BTPOWER_PROC_NODE "/proc/bluetooth/power/ctrl"
#endif

#define BOOTLOADER_SIZE (7 * FLASH_SECTOR_SIZE)

#define FILE_VERSION_ADDR 0x34
#define CHIP_VERSION_ADDR 0x0C
#define ALL_CRC_ADDR 0x32

#define GZIP_HEAD_LENGTH 0x60

/**
 * @brief     Update BK3515 firmware,baud is 115200
 *
 * This API update BK3515 firmware.
 *
 * @param id UART ID
 *
 * @return
 *    - BK_OK: succeed
 *    - others: other errors.
 */
int update_bk3515_firmware(uart_id_t id);

int bk3515_ota_init(void);

#endif /* __BEKEN_DOWNLOAD_H_ */
