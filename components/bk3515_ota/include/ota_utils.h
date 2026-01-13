#ifndef __OTA_UTILS_H__
#define __OTA_UTILS_H__

#include <components/log.h>
#include "driver/uart.h"

#define LOG_TAG "bk3515_ota"

#define DBGV(...) BK_LOGV(LOG_TAG, ##__VA_ARGS__)
#define DBGD(...) BK_LOGD(LOG_TAG, ##__VA_ARGS__)
#define DBGI(...) BK_LOGI(LOG_TAG, ##__VA_ARGS__)
#define DBGW(...) BK_LOGW(LOG_TAG, ##__VA_ARGS__)
#define DBGE(...) BK_LOGE(LOG_TAG, ##__VA_ARGS__)

#define DBG_RAWV(...) BK_RAW_LOGV(LOG_TAG, ##__VA_ARGS__)
#define DBG_RAW_D(...) BK_RAW_LOGD(LOG_TAG, ##__VA_ARGS__)
#define DBG_RAW_I(...) BK_RAW_LOGI(LOG_TAG, ##__VA_ARGS__)
#define DBG_RAW_W(...) BK_RAW_LOGW(LOG_TAG, ##__VA_ARGS__)
#define DBG_RAW_E(...) BK_RAW_LOGE(LOG_TAG, ##__VA_ARGS__)


#define ERROR_RESPONSE_RECEIVED  -100
//TODO delete
#define CMD_LINE_OUTPUT

#define BAUD_HCI_MODE (115200)
#define BAUD_FLASH_MODE (2000000)
#define BAUD_BOOTROM_MODE (115200)

#define FLASH_SECTOR_SIZE (4096)
#define MAX_TRY_TIMES 2

#define OTA_DATA_ADDR_8M_START      0x7D000
#define OTA_DATA_ADDR_8M_END        0xE3000
#define OTA_DATA_ADDR_4M_START      0x4F000
#define OTA_DATA_ADDR_4M_END        0x7C000

#define FLASH_ENVOTA_DEF_ADDR_4M_ABS 	 0x7E000
#define FLASH_ENVOTA_DEF_ADDR_8M_ABS 	 0xFE000
#define FLASH_ENVOTA_DEF_ADDR_16M_ABS 	 0x1FE000
#define FLASH_ENVOTA_DEF_ADDR_32M_ABS 	 0x3FE000


#define STREAM_TO_UINT8(u8, p) \
  {                            \
    (u8) = (uint8_t)(*(p));    \
    (p) += 1;                  \
  }
#define STREAM_TO_UINT16(u16, p) {u16 = ((uint16_t)(*(p)) + (((uint16_t)(*((p) + 1))) << 8)); (p) += 2;}
#define STREAM_TO_UINT32(u32, p) do{u32 = (uint32_t)*(uint8_t *)(p) + ((   (uint32_t)*((uint8_t *)(p) + 1)    ) << 8) + ((   (uint32_t)*((uint8_t *)(p) + 2)    ) << 16) + ((   (uint32_t)*((uint8_t *)(p) + 3)    ) << 24); (p) += 4;}while(0)

#define UNUSED_ATTR __attribute__((unused))

#define CRC_VALUE_SIZE 0x02
#define CRC_PACKET_SIZE 0x20
#define ENC_PACKET_SIZE 0x04

#define DO1(buf) crc = crc_table(((int)crc ^ (*buf++)) & 0xff) ^ (crc >> 8);
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);

#define ACK_TIMEOUT (2 * 1000 * 1000)

enum
{
    FLASH_ID_XTX_25F08B = 0x14405e,
    FLASH_ID_MXIC_25V8035F = 0x1423c2,
    FLASH_ID_XTX_25F04B = 0x13311c,
    FLASH_ID_GD_25D40 = 0x134051,
    FLASH_ID_GD_25D80 = 0x144051,
    FLASH_ID_GD_25Q80C = 0x1440C8,
    FLASH_ID_Puya_25Q80 = 0x146085,
    FLASH_ID_Puya_25Q40 = 0x136085,
    FLASH_ID_Puya_25Q32H = 0x166085,
    FLASH_ID_GD_25Q16 = 0x001540c8,
    FLASH_ID_GD_25Q16B = 0x001540c8,
    FLASH_ID_XTX_25F16B = 0x15400b,
    FLASH_ID_XTX_25F16B_1 = 0x15404b,
    FLASH_ID_XTX_25F32B = 0x0016400b,

    FLASH_ID_MXIC_25V4035F = 0x1323c2,
    FLASH_ID_MXIC_25V1635F = 0x1523c2,
    FLASH_ID_GD_25Q41B = 0x1340c8,
    FLASH_ID_BY_PN25Q80A = 0x1440e0,
    FLASH_ID_BY_PN25Q40A = 0x1340e0,

    FLASH_ID_XTX_25Q64B = 0x0017600b,
    FLASH_ID_XTX_25F64B = 0x0017400b,
    FLASH_ID_Puya_25Q64H = 0x00176085,
    FLASH_ID_GD_25Q64 = 0x001740c8,
    FLASH_ID_WB_25Q128JV = 0x001840ef,
    FLASH_ID_ESMT_25QH16B = 0x0015701c,
    FLASH_ID_GD_25WQ64E = 0x001765c8,
    FLASH_ID_GD_25WQ32E = 0x001665c8,
    FLASH_ID_GD_25WQ16E = 0x001565c8,
    FLASH_ID_Puya_25Q16H = 0x154285,
    FLASH_ID_Puya_25Q16SH = 0x156085,
    FLASH_ID_MXIC_25V80066 = 0x1420c2,
    FLASH_ID_UC_25QH40 = 0x1360B3,
    FLASH_ID_UNKNOWN = -1
};

enum FLASH_TYPE
{
    _4M_BITS,
    _8M_BITS,
    _16M_BITS,
    _32M_BITS,
    _64M_BITS,
};

typedef enum{
    COMMAND_IDLE,
    COMMAND_START,
    COMMAND_CONTINUE,
}command_status_t;
typedef enum{
    TYPE_UNKNOWN,
    TYPE_CMD,
    TYPE_FLASH,
}command_type_t;

void hex_dump(char *tag, unsigned char *src, unsigned int length);
int read_command_evt(uart_id_t id, unsigned char *buffer, unsigned int length, unsigned int msec);
int write_command(uart_id_t id, void *buffer, unsigned int length);
unsigned int str2int(unsigned char *buffer, unsigned char offset, unsigned char length);
unsigned int str2month(unsigned char *buffer);

int get_local_baud(uart_id_t id);
void set_local_baud(uart_id_t id, int baud);



#endif /* __OTA_UTILS_H__ */
