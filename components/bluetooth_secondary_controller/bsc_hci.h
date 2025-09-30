#pragma once

#define HCI_GRP_LINK_CONTROL_CMDS       (0x01 << 10)            /* 0x0400 */
#define HCI_GRP_LINK_POLICY_CMDS        (0x02 << 10)            /* 0x0800 */
#define HCI_GRP_HOST_CONT_BASEBAND_CMDS (0x03 << 10)            /* 0x0C00 */
#define HCI_GRP_INFORMATIONAL_PARAMS    (0x04 << 10)            /* 0x1000 */
#define HCI_GRP_STATUS_PARAMS           (0x05 << 10)            /* 0x1400 */
#define HCI_GRP_TESTING_CMDS            (0x06 << 10)            /* 0x1800 */

#define HCI_GRP_VENDOR_SPECIFIC         (0x3F << 10)            /* 0xFC00 */

//beken vendor cmd
#define TCI_SOFTWARE_RESET                          (0xA0 | HCI_GRP_VENDOR_SPECIFIC)
#define HCI_SOFTWARE_RESET                          TCI_SOFTWARE_RESET
#define HCI_VSC_WRITE_BD_ADDR                   0xFC1A

#define TCI_READ_VENDOR_VERSION                     (0xA1 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WOBLE_ADD_DEVICE_TO_WAKEUP_LIST         (0xA2 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WOBLE_CLEAN_WAKEUP_LIST                 (0xA3 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WOBLE_ADD_WAKEUP_RULE                   (0xA4 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WOBLE_CLEAN_WAKEUP_RULE                 (0xA5 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WOBLE_WRITE_WOBLE_ENABLE                (0xA6 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_BEKEN_H5_INIT                           (0xA7 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_BEKEN_SOFT_RESET_NO_RESP                (0xA8 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_BEKEN_SET_CONTROLLER_BAUD               (0xAA | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WRITE_FRQ_OFFSET_TO_FLASH               (0xAB | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_READ_FRQ_OFFSET_FROM_FLASH              (0xAC | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_BEKEN_HEART_BEAT_RSP                    (0xAD | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_BEKEN_ENABLE_HEART_BEAT                 (0xAE | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_WRITE_POWER_TO_FLASH                    (0xAF | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_READ_POWER_FROM_FLASH                   (0xB0 | HCI_GRP_VENDOR_SPECIFIC)

#define TCI_WRITE_USER_DATA_TO_FLASH                (0xB1 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_READ_USER_DATA_FROM_FLASH               (0xB2 | HCI_GRP_VENDOR_SPECIFIC)
#define TCI_READ_USER_DATA_LEN_FROM_FLASH           (0xB3 | HCI_GRP_VENDOR_SPECIFIC)

#define TCI_READ_CHIP_ID                            (0xB4 | HCI_GRP_VENDOR_SPECIFIC)

#define HCI_VSC_UPPER_2_FAKE_LAYER                  (0x100 | HCI_GRP_VENDOR_SPECIFIC)// 0xFD01 fake cmd !!!!
enum
{
    HCI_VSC_UPPER_2_FAKE_LAYER_SUB_OPCODE_H5_INIT = 1,
    HCI_VSC_UPPER_2_FAKE_LAYER_SUB_OPCODE_SET_HOST_BAUD,
};//HCI_VSC_UPPER_2_FAKE_LAYER_SUB_OPCODE;

enum
{
    VSC_BEKEN_USERIAL_BAUD_300 = 0,
    VSC_BEKEN_USERIAL_BAUD_600,
    VSC_BEKEN_USERIAL_BAUD_1200,
    VSC_BEKEN_USERIAL_BAUD_2400,
    VSC_BEKEN_USERIAL_BAUD_9600,
    VSC_BEKEN_USERIAL_BAUD_19200,
    VSC_BEKEN_USERIAL_BAUD_57600,
    VSC_BEKEN_USERIAL_BAUD_115200,
    VSC_BEKEN_USERIAL_BAUD_230400,
    VSC_BEKEN_USERIAL_BAUD_460800,
    VSC_BEKEN_USERIAL_BAUD_921600,
    VSC_BEKEN_USERIAL_BAUD_1M,
    VSC_BEKEN_USERIAL_BAUD_1_5M,
    VSC_BEKEN_USERIAL_BAUD_2M,
    VSC_BEKEN_USERIAL_BAUD_3M,
    VSC_BEKEN_USERIAL_BAUD_4M,
    //VSC_BEKEN_USERIAL_BAUD_AUTO,
};

#define LE_STREAM_TO_UINT8(u8, p) do{u8 = (uint8_t)(*(uint8_t *)(p)); (p) += 1;}while(0)
#define LE_STREAM_TO_UINT16(u16, p) do{u16 = (uint16_t)*(uint8_t *)(p) + ((   (uint16_t)*((uint8_t *)(p) + 1)    ) << 8); (p) += 2;}while(0)
#define LE_STREAM_TO_UINT32(u32, p) do{u32 = (uint32_t)*(uint8_t *)(p) + ((   (uint32_t)*((uint8_t *)(p) + 1)    ) << 8) + ((   (uint32_t)*((uint8_t *)(p) + 2)    ) << 16) + ((   (uint32_t)*((uint8_t *)(p) + 3)    ) << 24); (p) += 4;}while(0)
#define LE_STREAM_TO_ARRAY(a, p, size) do{memcpy(a, p, size); p += (size);}while(0)
