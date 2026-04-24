#pragma once

#include <common/bk_err.h>

typedef enum
{
    BOARDING_OP_SYNC_PHONE_OS = 30,
    BOARDING_OP_CONFIG_WIFI_AP = 31,
    BOARDING_OP_GET_SCAN_RESULTS = 32,
    BOARDING_OP_CONFIG_WIFI_STA = 33,
    BOARDING_OP_CONFIG_WIFI_P2P = 34,
    BOARDING_OP_TRANSFER_FILE_CONTROL = 50,
    BOARDING_OP_TRANSFER_FILE_DATA = 51,
    BOARDING_OP_NAVIGATION_CONTROL = 52,
    BOARDING_OP_NAVIGATION_TYPE_CONTROL = 53,
    BOARDING_OP_MAX
} boarding_opcode_cmd_t;

#define EVT_STATUS_OK               (0)
#define EVT_STATUS_ERROR            (1)

/* Navigation transfer protocol definitions for BLE-based navigation */
typedef enum
{
    FILE_TRANSFER_PROTOCOL_VERSION_1 = 1,
} file_transfer_protocol_version_t;

#define FILE_TRANSFER_PROTOCOL_VERSION_MIN      FILE_TRANSFER_PROTOCOL_VERSION_1
#define FILE_TRANSFER_PROTOCOL_VERSION_MAX      FILE_TRANSFER_PROTOCOL_VERSION_1

typedef enum
{
    FILE_OPERATION_DISPLAY = 0,
    FILE_OPERATION_SAVE = 1,
} file_operation_t;

typedef enum
{
    FILE_TYPE_JPEG = 0,
    FILE_TYPE_PNG = 1,
} file_type_t;

typedef enum
{
    FILE_CONTROL_START = 0,
    FILE_CONTROL_STOP = 1,
    FILE_CONTROL_COMPLETE = 2,
} file_control_cmd_t;

typedef struct
{
    uint8_t version;
    uint8_t controller;
} __attribute__((packed)) file_transfer_control_t;

typedef struct
{
    uint8_t version;
    uint8_t controller;
    uint32_t all_data_length;
    uint16_t crc;
    uint16_t packet_all_count;
    uint8_t file_operation;
    uint8_t file_type;
    char file_name[64];
} __attribute__((packed)) file_transfer_control_v1_t;

typedef struct
{
    uint16_t packet_num;
    uint8_t data[];
} __attribute__((packed)) file_transfer_data_t;

typedef enum
{
    NAVIGATION_CONTROL_START = 0,
    NAVIGATION_CONTROL_STOP = 1,
} navigation_control_cmd_t;

typedef struct
{
    uint8_t controller;
} __attribute__((packed)) navigation_control_t;

typedef enum
{
    NAVIGATION_TYPE_BT = 0,
    NAVIGATION_TYPE_WIFI = 1,
} navigation_type_t;

typedef struct
{
    uint8_t type;
} __attribute__((packed)) navigation_type_control_t;

#if !CONFIG_BK_BLE_PROVISIONING
typedef struct
{
    int32_t event;
    uint32_t param;
    uint16_t length;
} ble_prov_msg_t;
#endif

bk_err_t bk_sl_np_init(uint8_t reg_method);
navigation_type_t get_navigation_type(void);
