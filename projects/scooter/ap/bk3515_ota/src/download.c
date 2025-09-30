#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <time/time.h>

#include "ota_utils.h"
#include "download.h"

#include <os/os.h>
#include <os/mem.h> 
#include "driver/gpio.h"
#include "driver/flash.h"

#if CONFIG_OTA_HTTP
#include "utils_httpc.h"
#endif

#include <driver/flash.h>
#include <driver/flash_partition.h>

#if CONFIG_EASY_FLASH
#include "bk_ef.h"
#endif

uint8_t print_log_flag = 1;

unsigned int flash_mid = 0;

int ota_info_addr = 0;
int ota_data_addr = 0;
int ota_data_addr_len = 0;

typedef struct
{
    uint32_t slave_addr;
    uint32_t slave_size;
}ota_slave_bin_info_t;

#define EF_KEY_SLAVE_ADDR "slave_addr"
#define EF_KEY_SLAVE_SIZE "slave_size"

static int set_bt_power(int on);
static int firmware_version_read(uart_id_t id, int baud);
static void bsc_uart_clean_dirty_data(uint8_t uart_id);

#if CONFIG_UART_SW_FLOW_CTRL
static void set_uart_sw_flow_state(uint32_t enable)
{
    if (enable)
    {
        bk_usfc_hw_set_rts(CONFIG_BLUETOOTH_BSC_UART_ID, 1);
        bk_usfc_sw_set_rts(CONFIG_BLUETOOTH_BSC_UART_ID, 1);
    } else
    {
        bk_usfc_sw_set_rts(CONFIG_BLUETOOTH_BSC_UART_ID, 0);
        bk_usfc_hw_set_rts(CONFIG_BLUETOOTH_BSC_UART_ID, 0);
    }
    
}
#endif

static int get_slave_bin_info(uint32_t total_bin_size, ota_slave_bin_info_t *slave_bin_info)
{
    int ret = 0;
    if (slave_bin_info == NULL)
    {
        DBGE("slave_bin_info is NULL");
        return -1;
    }
    
    bk_logic_partition_t *bk_ptr = bk_flash_partition_get_info(BK_PARTITION_OTA);

	if(bk_ptr == NULL)
	{
		BK_LOGW(NULL, "get partition info fail! \r\n");
		return -1;
	}

    uint32_t ota_addr = bk_ptr->partition_start_addr;
    DBGD("ota_addr: 0x%x\r\n", ota_addr);

    uint32_t host_bin_size = 0;
    uint8_t read_buffer[4];
    ret = bk_flash_read_bytes(ota_addr+OTA_INFO_HOST_BIN_SIZE_OFFSET, read_buffer, 4);
    if (ret < 0)
    {
        DBGE("bk_flash_read_bytes fail");
        return ret;
    }

    host_bin_size = ((uint32_t)read_buffer[3]) << 24 | ((uint32_t)read_buffer[2]) << 16 | ((uint32_t)read_buffer[1]) << 8 | (uint32_t)read_buffer[0];
    host_bin_size += OTA_INFO_HOST_HEAD_SIZE;
    DBGD("host_bin_size: 0x%x\r\n", host_bin_size);

    slave_bin_info->slave_size = total_bin_size - host_bin_size;
    slave_bin_info->slave_addr = ota_addr + host_bin_size;

    DBGD("slave_bin_info.slave_addr: 0x%x\r\n", slave_bin_info->slave_addr);
    DBGD("slave_bin_info.slave_size: 0x%x\r\n", slave_bin_info->slave_size);

    return 0;
}

static int write_slave_bin_info_to_ef(ota_slave_bin_info_t *slave_bin_info)
{
    int ret = 0;
    if (slave_bin_info == NULL)
    {
        DBGE("slave_bin_info is NULL\r\n");
        return -1;
    }
#if CONFIG_EASY_FLASH
    ret = bk_set_env_enhance(EF_KEY_SLAVE_ADDR, &slave_bin_info->slave_addr, sizeof(slave_bin_info->slave_addr));
    if (ret < 0)
    {
        DBGE("bk_set_env_enhance EF_KEY_SLAVE_ADDR fail\r\n");
        return ret;
    }
    ret = bk_set_env_enhance(EF_KEY_SLAVE_SIZE, &slave_bin_info->slave_size, sizeof(slave_bin_info->slave_size));
    if (ret < 0)
    {
        DBGE("bk_set_env_enhance EF_KEY_SLAVE_SIZE fail\r\n");
        return ret;
    }
#endif
    return 0;
}

static int read_slave_info_from_ef(ota_slave_bin_info_t *slave_bin_info)
{
    int ret = 0;
    if (slave_bin_info == NULL)
    {
        DBGE("slave_bin_info is NULL\r\n");
        return -1;
    }
#if CONFIG_EASY_FLASH
    ret = bk_get_env_enhance(EF_KEY_SLAVE_ADDR, &(slave_bin_info->slave_addr), sizeof(slave_bin_info->slave_addr));
    if (ret < 0)
    {
        DBGE("bk_get_env_enhance EF_KEY_SLAVE_ADDR fail\r\n");
        return ret;
    }
    ret = bk_get_env_enhance(EF_KEY_SLAVE_SIZE, &(slave_bin_info->slave_size), sizeof(slave_bin_info->slave_size));
    if (ret < 0)
    {
        DBGE("bk_get_env_enhance EF_KEY_SLAVE_SIZE fail\r\n");
        return ret;
    }
#endif

    DBGV("slave_bin_info.slave_addr: 0x%x\r\n", slave_bin_info->slave_addr);
    DBGV("slave_bin_info.slave_size: 0x%x\r\n", slave_bin_info->slave_size);

    return 0;
}

static int save_slave_bin_info_to_ef(uint32_t total_bin_size)
{
    ota_slave_bin_info_t slave_bin_info = {0};
    int ret = 0;
    ret = get_slave_bin_info(total_bin_size, &slave_bin_info);
    if (ret < 0)
    {
        DBGE("get_slave_bin_info fail\r\n");
        return ret;
    }
    ret = write_slave_bin_info_to_ef(&slave_bin_info);
    if (ret < 0)
    {
        DBGE("write_slave_bin_info_to_ef fail\r\n");
        return ret;
    }
    return 0;
}

static int soft_reset(uart_id_t id)
{
    unsigned char cmd[] = {0x01, 0xA0, 0xFC, 0x00};

    return write_command(id, &cmd[0], sizeof(cmd));
}

static int reset_dut(uart_id_t id)
{
    return write_command(id, RESET_DUT_CMD, sizeof(RESET_DUT_CMD) - 1);
}

static int reboot_dut_ex(uart_id_t id)
{
    return write_command(id, REBOOT_DUT_EX_CMD, sizeof(REBOOT_DUT_EX_CMD) - 1);
}


static int handshake(uart_id_t id)
{
    int ret;
    unsigned char buffer[16] = {0};
    unsigned long count = MAX_RETRY;

    DBGD("%s\r\n", __func__);
    ret = reset_dut(id);

    if (ret < 0)
    {
        DBGD("%s reset dut error\r\n", __func__);
        return ret;
    }

    rtos_delay_milliseconds(5);
    do
    {
        count--;

        if (count % 10 == 0)
        {
            set_bt_power(POWER_OFF);
            rtos_delay_milliseconds(200);
            set_bt_power(POWER_ON);
            //NOTICE: delay needs to be decided based on the actual situation
            rtos_delay_milliseconds(120);
        }

#ifdef CMD_LINE_OUTPUT
        DBGD("handshake trying begin: %ld\r\n", count);
        
#endif
        ret = write_command(id, HANDSHAKE_CMD, sizeof(HANDSHAKE_CMD) - 1);

        if (ret < 0)
        {
            DBGD("%s send handshake cmd error\r\n", __func__);
            return -1;
        }

        rtos_delay_milliseconds(5);
        ret = read_command_evt(id, buffer, 8, MSEC_TIMEOUT);

        if (ret > 0 &&
            0 == strncmp((char *)buffer, HANDSHAKE_ACK, sizeof(HANDSHAKE_ACK) - 1))
        {
            DBGD("%s success\r\n", __func__);
            break;
        }

    }
    while (count);
    if (count == 0)
    {
        DBGD("%s reach max retry\r\n", __func__);
        return HANDSHAKE_TIMEOUT_ERROR;
    }

    return 0;
}

static unsigned int read_flash_mid(uart_id_t id)
{
    int ret;
    // unsigned char buffer[1024] = {0};
    unsigned char *buffer = NULL;
    unsigned long count = MID_MAX_RETRY;
    unsigned int mid = 0;
    buffer = os_malloc(1024);
    if (buffer == NULL)
    {
        DBGE("%s malloc fail\r\n", __func__);
        return -1;
    }
    memset(buffer, 0, 1024);

    do
    {
        count--;

        DBGV("%s start: %ld\r\n", __func__,  count);

        ret = write_command(id, FLASH_MID_CMD, sizeof(FLASH_MID_CMD) - 1);

        if (ret < 0)
        {
            DBGD("%s write mid error\r\n", __func__);
            break;
        }

        ret = read_command_evt(id, buffer, 15, MID_MSEC_TIMEOUT);

        DBGD("got cmd ack: %d\r\n", ret);

        hex_dump("flash id\r\n", buffer, ret);

        if (ret > 0)
        {
            DBGV("%s success, %d\r\n", __func__, ret);

            if (ret == 15)
            {
                mid = buffer[ret - 1] << 16 | buffer[ret - 2] << 8 | buffer[ret - 3];
                break;
            }

            //continue;
        }
        rtos_delay_milliseconds(5);

    }
    while (count);
    if (count == 0)
    {
        DBGD("%s reach max retry\r\n", __func__);
        if (buffer != NULL)
        {
            os_free(buffer);
        }
        return -1;
    }

    if (buffer != NULL)
    {
        os_free(buffer);
    }
    return mid;
}

static int set_uart_baud(uart_id_t id, unsigned int baud)
{ 
    int ret;
    ret = bk_uart_set_baud_rate(id, baud);
    set_local_baud(id, baud);
    return ret;
}

static int change_baud(uart_id_t id, unsigned int baud)
{
    int ret;
    unsigned char buffer[128] = {0};
    unsigned char retry = MAX_TRY_TIMES;

    unsigned char baud_cmd[] = {0x01, 0xE0, 0xFC, 0x06, 0x0F,
                                (unsigned char)(baud & 0xFF),
                                (unsigned char)((baud >> 8) & 0xFF),
                                (unsigned char)((baud >> 16) & 0xFF),
                                (unsigned char)((baud >> 24) & 0xFF),
                                0x0F
                               };

    rtos_delay_milliseconds(5);

    ret = write_command(id, baud_cmd, sizeof(baud_cmd));

    if (ret < 0)
    {
        DBGD("%s write baud_cmd error\r\n", __func__);
        return -1;
    }

    rtos_delay_milliseconds(5);

    ret = set_uart_baud(id, baud);
    if (ret < 0)
    {
        DBGD("%s set uart baud error\r\n", __func__);
        return -1;
    }

    do
    {

        ret = read_command_evt(id, buffer, 12, 1000);

        if (ret > 0)
        {
            DBGV("%s baud_cmd success, %d\r\n", __func__, ret);

            if (!strncmp((char *)buffer, BAUD_ACK, sizeof(BAUD_ACK) - 1))
            {
                DBGD("%s BAUD_ACK success\r\n", __func__);
                break;
            }

        }

        retry --;
        if(retry == 0)
        {
#ifdef CMD_LINE_OUTPUT
            if(print_log_flag)
            {
                DBGD("%s baud_cmd error retry failed\n", __func__);
                
            }
#endif
            DBGV("%s retry change_baud failed %d \n", __func__, ret);
            return -1;
        }
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("%s baud_cmd error retry %d\n", __func__, retry);
            
        }
#endif

        rtos_delay_milliseconds(16);
    }
    while (retry);

    return ret;
}

static int write_flash_reg1(uart_id_t id, unsigned char bt, int retry)
{
    int ret;
    unsigned char buffer[20] = {0};
    unsigned long count = REG1_MAX_RETRY;

    unsigned char flash_reg1_cmd1[] = {0x01, 0xE0,
                                       0xFC, 0xFF, 0xF4, 0x03, 0x00, 0x0D, 0x01, 0x00
                                      };

    flash_reg1_cmd1[9] = bt;

    do
    {
        count--;

        DBGV("%s start: %ld\r\n", __func__, count);

        ret = write_command(id, flash_reg1_cmd1, sizeof(flash_reg1_cmd1));

        if (ret < 0)
        {
            DBGD("%s error1\r\n", __func__);
            return -1;
        }

        ret = read_command_evt(id, buffer, 13, REG1_MSEC_TIMEOUT);

        if (ret != 13 || buffer[12] != bt)
        {
            continue;
        }

        rtos_delay_milliseconds(500);

#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("FLASH_REG1_CMD2 %lu\n", count);
        }
#endif

        ret = write_command(id, FLASH_REG1_CMD2, sizeof(FLASH_REG1_CMD2) - 1);

        if (ret < 0)
        {
            DBGD("%s error2\r\n", __func__);
            return -1;
        }

        ret = read_command_evt(id, buffer, 13, REG1_MSEC_TIMEOUT);

        if (ret != 13)
        {
            continue;
        }

        if (buffer[12] == bt)
        {
            break;
        }

    }
    while (count);
    if (count == 0)
    {
        DBGD("%s reach max retry\r\n", __func__);
        return -1;
    }

    return 0;
}

static int write_flash_reg1_plus(uart_id_t id, unsigned char bt1, unsigned char bt2, int retry)
{
    int ret;
    unsigned char buffer[20] = {0};
    unsigned long count = REG1_MAX_RETRY;
    unsigned char flash_reg1_plus_cmd1[] = {0x01, 0xE0, 0xFC, 0xFF, 0xF4, 0x04, 0x00, 0x0D, 0x01, 0x00, 0x00};

    flash_reg1_plus_cmd1[9] = bt1;
    flash_reg1_plus_cmd1[10] = bt2;

    do
    {
        count--;

        DBGD("%s start: %ld", __func__, count);

        ret = write_command(id, flash_reg1_plus_cmd1, sizeof(flash_reg1_plus_cmd1));

        if (ret < 0)
        {
            DBGD("%s error1", __func__);
            return -1;
        }

        ret = read_command_evt(id, buffer, 14, 1000);

        DBGV("%s read cmd1 ack: %d", __func__, ret);

        if (ret != 14 || buffer[12] != bt1 || buffer[13] != bt2)
        {
            DBGV("%s read cmd1 ack error", __func__);
            continue;
        }

        rtos_delay_milliseconds(500);
        ret = write_command(id, FLASH_REG1_PLUS_CMD2, sizeof(FLASH_REG1_CMD2) - 1);

        if (ret < 0)
        {
            DBGD("%s error2", __func__);
            return -1;
        }

        ret = read_command_evt(id, buffer, 13, 1000);

        DBGV("%s read cmd2 ack: %d", __func__, ret);

        if (ret != 13)
        {
            DBGV("%s read cmd2 ack error", __func__);
            continue;
        }

        if (buffer[12] == bt1)
        {
            ret = write_command(id, FLASH_REG1_PLUS_CMD3, sizeof(FLASH_REG1_PLUS_CMD3) - 1);

            if (ret < 0)
            {
                DBGD("%s read cmd3 ack error 1", __func__);
                return -1;
            }

            ret = read_command_evt(id, buffer, 13, 1000);

            DBGV("%s read cmd3 ack: %d", __func__, ret);

            if (ret != 13)
            {
                DBGV("%s read cmd3 ack error 2", __func__);
                continue;
            }
            if ((buffer[12] & (1 << 6)) == 0)
            {
                DBGV("%s read cmd3 ack error 3", __func__);
                break;
            }
        }
        else
        {
            DBGD("%s read cmd3 ack error 0, : %02X: %02X", __func__, buffer[12], bt1);
        }

    }
    while (count);
    if (count == 0)
    {
        DBGD("%s reach max retry", __func__);
        return -1;
    }

    return 0;
}

static int write_status_register_enable(uart_id_t id)
{
    int ret;
    unsigned char *buffer = NULL;

    buffer = os_malloc(1024);
    if (buffer == NULL)
    {
        DBGE("%s malloc fail\r\n", __func__);
        return -1;
    }
    memset(buffer, 0, 1024);

    do
    {
        DBGD("%s", __func__);

        ret = write_command(id, WRITE_STATUS_REGISTER_ENABLE, sizeof(WRITE_STATUS_REGISTER_ENABLE) - 1);

        if (ret < 0)
        {
            DBGD("%s error", __func__);
            if (buffer != NULL)
            {
                os_free(buffer);
            }
            return -1;
        }

        ret = read_command_evt(id, buffer, 1024, 800);

        if (ret == 12)
        {
            break;
        }

    }
    while (0);

    if (buffer != NULL)
    {
        os_free(buffer);
    }
    return 0;
}

static int write_flash_reg2(uart_id_t id, unsigned char bt, int retry)
{
    int ret;
    // unsigned char buffer[1024] = {0};
    unsigned char *buffer = NULL;
    unsigned long count = REG1_MAX_RETRY;

    unsigned char flash_reg2_cmd1[] = {0x01, 0xE0,
                                       0xFC, 0xFF, 0xF4, 0x03, 0x00, 0x0D, 0x31, 0x00
                                      };

    flash_reg2_cmd1[9] = bt;

    buffer = os_malloc(1024);
    if (buffer == NULL)
    {
        DBGE("%s malloc fail\r\n", __func__);
        return -1;
    }
    memset(buffer, 0, 1024);

    do
    {
        count--;

        DBGV("%s start: %ld", __func__, count);

        ret = write_command(id, flash_reg2_cmd1, sizeof(flash_reg2_cmd1));

        if (ret < 0)
        {
            DBGD("%s error1", __func__);
            if (buffer != NULL)
            {
                os_free(buffer);
            }
            return -1;
        }

        ret = read_command_evt(id, buffer, 1024, 800);

        if (ret != 13 || buffer[12] != bt)
        {
            continue;
        }

        rtos_delay_milliseconds(500);

        ret = write_command(id, FLASH_REG2_CMD2, sizeof(FLASH_REG2_CMD2) - 1);

        if (ret < 0)
        {
            DBGD("%s error2", __func__);
            if (buffer != NULL)
            {
                os_free(buffer);
            }
            return -1;
        }

        ret = read_command_evt(id, buffer, 1024, 800);

        if (ret != 13)
        {
            continue;
        }

        if (buffer[12] == bt)
        {
            break;
        }

    }
    while (count);
    if (count == 0)
    {
        DBGD("%s reach max retry", __func__);
        if (buffer != NULL)
        {
            os_free(buffer);
        }
        return -1;
    }

    if (buffer != NULL)
    {
        os_free(buffer);
    }

    return 0;
}

static int flash_protect_opration(uart_id_t id, int flash_mid, unsigned char protect)
{
    if (protect)
    {
        if (write_flash_reg1(id, 0x1C, 10))
        {
            DBGD("%s reg1 0x1c failed", __func__);
            return FLASH_PROTECT_ON_ERROR ;
        }
    }
    else
    {
        switch (flash_mid)
        {
            case FLASH_ID_Puya_25Q16H:
            case FLASH_ID_Puya_25Q32H:
            case FLASH_ID_Puya_25Q16SH:
            case FLASH_ID_GD_25Q41B:
                if (write_status_register_enable(id))
                {
                    DBGD("%s write_status_register_enable failed", __func__);
                    return FLASH_PROTECT_OFF_ERROR ;
                }

                if (write_flash_reg1(id, 0x00, 10))
                {
                    DBGD("%s reg1 0x00 failed", __func__);
                    return FLASH_PROTECT_OFF_ERROR ;
                }

                if (write_flash_reg2(id, 0x00, 10))
                {
                    DBGD("%s reg2 0x00 failed", __func__);
                    return FLASH_PROTECT_OFF_ERROR ;
                }
                break;
            case FLASH_ID_Puya_25Q80:
            case FLASH_ID_GD_25Q80C:
            case FLASH_ID_GD_25Q16B:
            case FLASH_ID_Puya_25Q40:
            case FLASH_ID_UC_25QH40:
                if (write_flash_reg1_plus(id, 0x00, 0x00, 10))
                {
                    DBGD("%s reg1 plus 0x00 failed", __func__);
                    return FLASH_PROTECT_OFF_ERROR ;
                }
                break;
            case FLASH_ID_MXIC_25V80066:
            default:
                if (write_flash_reg1(id, 0x00, 10))
                {
                    DBGD("%s reg1 0x00 failed2", __func__);
                    return FLASH_PROTECT_OFF_ERROR ;
                }
                break;
        }
    }

    return 0;
}

static int get_flash_type(int id)
{
    int type = FLASH_ID_UNKNOWN;
    switch (id)
    {
        case FLASH_ID_XTX_25F04B:
        case FLASH_ID_GD_25D40:
        case FLASH_ID_Puya_25Q40:
        case FLASH_ID_MXIC_25V4035F:
        case FLASH_ID_GD_25Q41B:
        case FLASH_ID_BY_PN25Q40A:
        case FLASH_ID_UC_25QH40:
            type = _4M_BITS;
            break;
        case FLASH_ID_XTX_25F08B:
        case FLASH_ID_MXIC_25V8035F:
        case FLASH_ID_GD_25D80:
        case FLASH_ID_GD_25Q80C:
        case FLASH_ID_Puya_25Q80:
        case FLASH_ID_BY_PN25Q80A:
            type = _8M_BITS;
            break;
        case FLASH_ID_XTX_25F16B_1:
        case FLASH_ID_XTX_25F16B:
        case FLASH_ID_MXIC_25V1635F:
        case FLASH_ID_ESMT_25QH16B:
        case FLASH_ID_GD_25WQ16E:
        case FLASH_ID_Puya_25Q16H:
        case FLASH_ID_Puya_25Q16SH:
        case FLASH_ID_GD_25Q16:
            type = _16M_BITS;
            break;
        case FLASH_ID_GD_25WQ32E:
        case FLASH_ID_Puya_25Q32H:
            type = _32M_BITS;
            break;
        case FLASH_ID_GD_25Q64:
        case FLASH_ID_GD_25WQ64E:
        case FLASH_ID_Puya_25Q64H:
            type = _64M_BITS;
            break;
        default:
            type = _4M_BITS;
            break;
    }
    return type;
}

static int get_flash_size(int type)
{
    int flash_size;

    switch (type)
    {
        case 0:  //4M
            flash_size = 0x80000;
            break;
        case 1:  //8M
            flash_size = 0x100000;
            break;
        case 2:
            flash_size = 0x200000;
            break;
        case 3:  //32M
            flash_size = 0x400000;
            break;

        default: //16M
            flash_size = 0x200000;
            break;
    }
    return flash_size;
}

static void get_ota_addr(int id)
{
    switch ((id & 0xff0000)>>16)
    {
        case 0x13:  //4M
            ota_info_addr = FLASH_ENVOTA_DEF_ADDR_4M_ABS;
            ota_data_addr = OTA_DATA_ADDR_4M_START;
            ota_data_addr_len = OTA_DATA_ADDR_4M_END - OTA_DATA_ADDR_4M_START;
            break;
        case 0x14:
            ota_info_addr = FLASH_ENVOTA_DEF_ADDR_8M_ABS;
            ota_data_addr = OTA_DATA_ADDR_8M_START;
            ota_data_addr_len = OTA_DATA_ADDR_8M_END - OTA_DATA_ADDR_8M_START;
            break;
//        case 15:
//            ota_info_addr = FLASH_ENVOTA_DEF_ADDR_16M_ABS;
//            ota_data_addr = ;
//            break;
//        case 16:  //32M
//            ota_info_addr = FLASH_ENVOTA_DEF_ADDR_32M_ABS;
//            ota_data_addr = ;
//            break;

        default: //16M
            ota_info_addr = 0xFE000;
            ota_data_addr = OTA_DATA_ADDR_8M_START;
            ota_data_addr_len = OTA_DATA_ADDR_8M_END - OTA_DATA_ADDR_8M_START;
            break;
    }
}

static uint32_t get_slave_bin_size(void)
{
    ota_slave_bin_info_t slave_bin_info = {0};

    read_slave_info_from_ef(&slave_bin_info);
    DBGD("%s slave_addr: 0x%x, slave_size: 0x%x\r\n", __func__, slave_bin_info.slave_addr, slave_bin_info.slave_size);
    uint32_t slave_size = slave_bin_info.slave_size;
    return slave_size;
}

static uint32_t get_slave_bin_addr(void)
{
    ota_slave_bin_info_t slave_bin_info = {0};

    read_slave_info_from_ef(&slave_bin_info);
    DBGD("%s slave_addr: 0x%x, slave_size: 0x%x\r\n", __func__, slave_bin_info.slave_addr, slave_bin_info.slave_size);
    uint32_t slave_addr = slave_bin_info.slave_addr;
    return slave_addr;
}

static int erase_flash_ota(uart_id_t id)
{
    int ret;
    // unsigned char buffer[1024] = {0};
    // unsigned char package[128] = {0};
    unsigned char *buffer = NULL;
    unsigned char *package = NULL;
    unsigned int file_size = 0;
    unsigned int sectors, i;
    unsigned int address = ota_data_addr;
    unsigned int retry = MAX_TRY_TIMES;
    unsigned int ota_size = ota_data_addr_len;

    {
        file_size = get_slave_bin_size();

        DBGD("%s get file size : %d\r\n", __func__, file_size);

        if (file_size <= 0)
        {
            DBGD("%s get file size faild: %d", __func__, file_size);
            return -1;
        }

        if(file_size > ota_size)
        {
            DBGD("%s file size %d is overflow ota size %d", __func__, file_size, ota_size);
            return -1;
        }

        buffer = (unsigned char *)os_malloc(1024);
        if (buffer == NULL)
        {
            DBGD("%s buffer malloc failed", __func__);
            return -1;
        }
        
        memset(buffer, 0, 1024);

        package = (unsigned char *)os_malloc(128);
        if (package == NULL)
        {
            os_free(buffer);
            DBGD("%s package malloc failed", __func__);
            return -1;
        }

        sectors = (file_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

        // TODO delete 
        DBGD("%s sectors: %d\n", __func__, sectors);

        for (i = 0; i < sectors; i++)
        {
            memset(package, 0xFF, 128);
            memcpy(package, ERASE_4K_FLASH_CMD, ERASE_4K_FLASH_CMD_SIZE);
            memcpy(package + ERASE_4K_FLASH_CMD_SIZE, &address, ERASE_FLASH_ADDR_SIZE);
            DBGD("%s addr: 0x%08X\n", __func__, address);
            ret = write_command(id, package, ERASE_4K_FLASH_CMD_SIZE + ERASE_FLASH_ADDR_SIZE);
            if (ret < 0)
            {
                DBGD("%s write address: %08X failed\r\n", __func__, address);
                retry --;
                if(retry == 0)
                {
                    DBGD("%s retry write address: %08X failed\n", __func__, address);
                    if (buffer)
                    {
                        free(buffer);
                    }
                    if (package)
                    {
                        free(package);
                    }
                    return ERASE_FLASH_ERROR;
                }
                i--;
                continue;
            }
            
            ret = read_command_evt(id, buffer, 1024, 800);

            if (ret <= 0 || buffer[9] != 0x0B || *((unsigned int *)(&buffer[11])) != address)
            {
                DBGD("%s check package response error", __func__);
                retry --;
                if(retry == 0)
                {
#ifdef CMD_LINE_OUTPUT
                    if(print_log_flag)
                    {
                        DBGD("\b\r%scheck package response error retry failed\n", __func__);
                    }
#endif
                    if (buffer)
                    {
                        free(buffer);
                    }
                    if (package)
                    {
                        free(package);
                    }
                    return ERASE_FLASH_ERROR;
                }
#ifdef CMD_LINE_OUTPUT
                if(print_log_flag)
                {
                    DBGD("\b\r%s check package response error retry %d\n", __func__, retry);
                    
                }
#endif
                DBGD("%s check package response error retry %d", __func__, retry);
                i --;
                continue;
            }
            else
            {
                retry = MAX_TRY_TIMES;
            }

            address += FLASH_SECTOR_SIZE;
        }
    }
    if (buffer)
    {
        os_free(buffer);
    }
    if (package)
    {
        os_free(package);
    }

    return 0;
}


static int download_flash_ota(uart_id_t id, unsigned int flash_mid)
{

    unsigned int file_size = 0;
    // unsigned char package[FLASH_SECTOR_SIZE + DOWNLOAD_PACK_HEAD];
    // unsigned char buffer[1024] = {0};
    unsigned char *package = NULL;
    unsigned char *buffer = NULL;
    unsigned int sectors, i;
    unsigned int address = ota_data_addr;
    unsigned int retry_times = MAX_TRY_TIMES;

    unsigned int bk3515_ota_address = get_slave_bin_addr();
    int ret = 0;
    int prog = 0;

    file_size = get_slave_bin_size();

    if (file_size <= 0)
    {
        DBGD("%s get file size faild: %d\r\n", __func__, file_size);
        return -1;
    }

    package = (unsigned char *)os_malloc(FLASH_SECTOR_SIZE + DOWNLOAD_PACK_HEAD);
    if (package == NULL)
    {
        DBGD("%s package malloc failed", __func__);
        return -1;
    }

    buffer = (unsigned char *)os_malloc(1024);
    if (buffer == NULL)
    {
        os_free(package);
        DBGD("%s buffer malloc failed", __func__);
        return -1;
    }
    memset(buffer, 0, 1024);

    sectors = (file_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

    for (i = 0; i < sectors; i++)
    {
        retry_times = MAX_TRY_TIMES;
        memset(package, 0xFF, FLASH_SECTOR_SIZE + DOWNLOAD_PACK_HEAD);
        memcpy(package, DOWNLOAD_CMD1, DOWNLOAD_CMD1_SIZE);
        memcpy(package + DOWNLOAD_CMD1_SIZE, &address, DOWNLOAD_ADDR_SIZE);

        //从地址读4k到Buffer
        uint32_t read_addr = bk3515_ota_address + (i * FLASH_SECTOR_SIZE);
        uint32_t read_size = (i == sectors - 1) ? (file_size % FLASH_SECTOR_SIZE) : FLASH_SECTOR_SIZE;

        DBGD("ota read size :%d \n", read_size);

        if (bk_flash_read_bytes(read_addr, package + DOWNLOAD_PACK_HEAD, read_size) != BK_OK)
        {
            DBGD("%s flash read failed at 0x%08X\r\n", __func__, read_addr);
            goto error;
        }
        
        ret = write_command(id, package, FLASH_SECTOR_SIZE + DOWNLOAD_PACK_HEAD);

        if (ret < 0)
        {
            DBGD("%s write package at address: %08X failed", __func__, address);
            goto error;
        }

        ret = read_command_evt(id, buffer, 1024, 3000);

        if (ret <= 0)
        {
            DBGD("%s read package response failed\r\n", __func__);
            goto error;
        }

        DBGD("download address: %08X\n", address);
retry1:
        if (*((unsigned int *)(&buffer[11])) != address)
        {
            unsigned char download_cmd3[] = {0x01, 0xE0,
                                             0xFC, 0xFF, 0xF4, 0x06, 0x00, 0x0F, 0x20,
                                             (unsigned char)(address & 0xFF),
                                             (unsigned char)(address >> 8 & 0xFF),
                                             (unsigned char)(address >> 16 & 0xFF),
                                             (unsigned char)(address >> 24 & 0xFF)
                                            };
            int try_times = MAX_TRY_TIMES;

            retry_times --;
            DBGD("%s read package response error", __func__);

            ret = write_command(id, DOWNLOAD_CMD2, sizeof(DOWNLOAD_CMD2) - 1);

            if (ret < 0)
            {
                DBGD("%s write cmd2 failed", __func__);
                goto error;
            }

            do
            {
                ret = write_command(id, download_cmd3, sizeof(download_cmd3));

                if (ret < 0)
                {
                    DBGD("%s write download cmd3 error", __func__);
                    goto error;
                }

                ret = read_command_evt(id, buffer, 1024, 3000);

                if (ret <= 0)
                {
                    DBGD("%s read cmd3 response failed", __func__);
                    goto error;
                }

                try_times--;
                if (*((unsigned int *)(&buffer[12])) == address)
                {
#ifdef CMD_LINE_OUTPUT
                    if(print_log_flag)
                    {
                        DBGD("%s write download cmd3 successed\n", __func__);
                        
                    }
#endif
                    break;
                }
                else
                {
#ifdef CMD_LINE_OUTPUT
                    if(print_log_flag)
                    {
                        DBGD("%s write download cmd3 failed retry %d\n", __func__, try_times);
                        
                    }
#endif
                }
            }
            while (try_times);

            if (*((unsigned int *)(&buffer[12])) != address)
            {
#ifdef CMD_LINE_OUTPUT
                if(print_log_flag)
                {
                    DBGD("%s write download cmd3 error retry failed\n", __func__);
                    
                }
#endif
                goto error;
            }

            ret = write_command(id, package, FLASH_SECTOR_SIZE + DOWNLOAD_PACK_HEAD);

            if (ret < 0)
            {
                DBGD("%s write package at address: %08X failed 2", __func__, address);
                goto error;
            }

            ret = read_command_evt(id, buffer, 1024, 3000);

            if (ret <= 0)
            {
                DBGD("%s read package response failed 2\r\n", __func__);
                goto error;
            }

            if (*((unsigned int *)(&buffer[11])) != address)
            {
                DBGD("%s check package response error 2\r\n", __func__);
                if(retry_times > 0)
                {
#ifdef CMD_LINE_OUTPUT
                    if(print_log_flag)
                    {
                        DBGD("%s check package response error retry %d\n", __func__, retry_times);
                        
                    }
#endif
                    goto retry1;
                }
                else
                {
#ifdef CMD_LINE_OUTPUT
                    if(print_log_flag)
                    {
                        DBGD("\b\r%s check package response error retry failed\n", __func__);
                        
                    }
#endif
                goto error;
                }
            }
        }

        prog += 1;
        address += FLASH_SECTOR_SIZE;

#ifdef CMD_LINE_OUTPUT
        DBGD("\b\rdownload inprocessing ==>: %d%%\n\b\r", i * 100 / sectors);
        
#endif
    }

#ifdef CMD_LINE_OUTPUT
    DBGD("\b\rdownload inprocessing ==>: 100%%\n\b\r");
    
#endif

    if (package)
    {
        os_free(package);
    }
    if (buffer)
    {
        os_free(buffer);
    }

    return 0;
    
error:
#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("download failed\n");
    }
#endif

    if (package)
    {
        free(package);
    }
    if (buffer)
    {
        free(buffer);
    }

    return DOWNLOAD_FLASH_ERROR;
}

static unsigned int crc_table(unsigned int index)
{
    unsigned int c = index;
    unsigned int poly = 0xedb88320L;
    int k;

    for (k = 0; k < 8; k++)
    {
        c = c & 1 ? poly ^ (c >> 1) : c >> 1;
    }

    return c;
}

static unsigned int crc32_ver2(unsigned int crc, unsigned char *buf, int len)
{
    if (buf == NULL)
    {
        return 0L;
    }

    while (len >= 8)
    {
        DO8(buf)
        len -= 8;
    }

    if (len)
    {
        do
        {
            DO1(buf)
        }
        while (--len);
    }

    return crc;
}

static unsigned int get_firmware_crc(uint16_t offset)
{
    int file_size = get_slave_bin_size();
    unsigned char *buffer = psram_malloc(file_size);
    unsigned char *p = buffer;
    unsigned int crc = 0xffffffff;
    unsigned int bk3515_ota_address = get_slave_bin_addr();
    int ret = -1;
    DBGV("%s file size: %d, 0x%x", __func__, file_size, offset);


    ret = bk_flash_read_bytes(bk3515_ota_address, buffer, file_size);

    if (ret < 0)
    {
        DBGD("%s read faild\r\n", __func__);
        psram_free(buffer);
        return 0;
    }

    p += offset;
    file_size -= offset;

    while (file_size >= 8)
    {
        DO8(p)
        file_size -= 8;
    }

    if (file_size) do
        {
            DO1(p)
        }
        while (--file_size);

    psram_free(buffer);

    return crc;
}


static int check_crc_error_ota(uart_id_t id)
{
    int ret;
    unsigned char buffer[16] = {0};
    int crc_address = ota_data_addr + get_slave_bin_size() - 1;
    unsigned char cmd[] = {0x01, 0xE0, 0xFC, 0x09, 0x10,
                           (unsigned char)(ota_data_addr & 0xFF),
                           (unsigned char)(ota_data_addr >> 8 & 0xFF),
                           (unsigned char)(ota_data_addr >> 16 & 0xFF),
                           (unsigned char)(ota_data_addr >> 24 & 0xFF),
                           (unsigned char)(crc_address & 0xFF),
                           (unsigned char)(crc_address >> 8 & 0xFF),
                           (unsigned char)(crc_address >> 16 & 0xFF),
                           (unsigned char)(crc_address >> 24 & 0xFF)
                          };
    unsigned char check[] = {0x04, 0x0e, 0x05, 0x01, 0xe0, 0xfc, 0x10};
    unsigned int read_crc = 0, file_crc = 0;
    unsigned int retry = MAX_TRY_TIMES;

    file_crc = get_firmware_crc(0);

    DBGV("%s file crc: %X\r\n", __func__, file_crc);

    do
    {
        DBGV("%s", __func__);
        retry --;

        ret = write_command(id, cmd, sizeof(cmd));

        if (ret < 0)
        {
            DBGD("%s write error\r\n", __func__);
            return WRITE_DATA_ERROR;
        }

        ret = read_command_evt(id, buffer, 11, 12000);

        if (ret == 11)
        {
            break;
        }
        else
        {
            DBGD("%s read crc error: %d\r\n", __func__, ret);
#ifdef CMD_LINE_OUTPUT
            if(print_log_flag)
            {
                DBGD("\b\r%s read cmd ack error %d\n", __func__, retry);
                
            }
#endif
            DBGD("%s read cmd ack error %d\r\n", __func__, retry);
            if(retry == 0)
            {
#ifdef CMD_LINE_OUTPUT
                if(print_log_flag)
                {
                    DBGD("\b\r%s read cmd ack error failed\n", __func__);
                    
                }
#endif
                DBGD("%s read crc error failed\r\n", __func__);
                return CHECK_CRC_ERROR;
            }
        }

    }
    while (retry);

    check[2] = 3 + 1 + 4;

    if (memcmp(check, buffer, 7) != 0)
    {
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("%s response error\r\n", __func__);
            
        }
#endif
        return CHECK_CRC_ERROR;
    }

    read_crc = buffer[10];
    read_crc = (read_crc << 8) + buffer[9];
    read_crc = (read_crc << 8) + buffer[8];
    read_crc = (read_crc << 8) + buffer[7];

#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("%s CRC: %08X %08X\r\n", __func__, read_crc, file_crc);
        
    }
#endif

    if (read_crc != file_crc)
    {
        return CHECK_CRC_ERROR;
    }

    return 0;
}

static uint16_t gen_crc16(uint16_t crc, uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
    {
         crc = ((crc >> 8) | (crc << 8)) & 0xFFFF;
         crc ^= (data[i] & 0xFF);// byte to int, trunc sign
         crc ^= ((crc & 0xFF) >> 4);
         crc ^= (crc << 12) & 0xFFFF;
         crc ^= ((crc & 0xFF) << 5) & 0xFFFF;
    }
    return (crc & 0xFFFF);
}

static uint16_t get_crc_all(void)
{
    int ret = NO_ERROR;
    unsigned char buffer[128];
    uint16_t crc = 0;
    unsigned int bk3515_ota_address = get_slave_bin_addr();

    ret = bk_flash_read_bytes(bk3515_ota_address+ ALL_CRC_ADDR, buffer, 2);

    if (ret < 0)
    {
        DBGD("%s read faild\r\n", __func__);
        ret = READ_DATA_ERROR;
        return ret;
    }

    DBGD("%s %x %x\r\n", __func__, buffer[0], buffer[1]);
    crc = ((buffer[1] & 0xff) << 8) + ((buffer[0] & 0xff) << 0);

    DBGD("%s %x\r\n", __func__, crc);

    return crc;

}


static uint16_t calc_crc(void)
{
    uint16_t crc = 0xFFFF;
    uint16_t len = 0;

    int file_size = get_slave_bin_size();
    int file_offset = 0;
    unsigned char *buffer = psram_malloc(file_size);
    unsigned char *p = buffer;
    unsigned int bk3515_ota_address = get_slave_bin_addr();
    int ret = -1;

    ret = bk_flash_read_bytes(bk3515_ota_address, buffer, file_size);
    if (ret < 0)
    {
        DBGD("%s read faild\r\n", __func__);
        psram_free(buffer);
        return 0;
    }

    while (file_size > file_offset)
    {
        len = ((file_offset + 500) <= file_size) ? 500 : (file_size - file_offset);
        crc = gen_crc16(crc, p, len);
        file_offset += len;
        p += len;
    }

    psram_free(buffer);

    return crc;
}

static int set_ota_info(uart_id_t id, unsigned int status, unsigned int ota_addr)
{
    unsigned char package[128] = {0};
    // unsigned char buffer[1024];
    unsigned char *buffer = NULL;
    int ret = 0;
    driver_ota_info_s ota_info = {0};
    uint16_t crc_all = 0;
    buffer = os_malloc(1024);
    if (buffer == NULL)
    {
        DBGD("%s malloc buffer failed\r\n", __func__);
        return -1;
    }
    memset(buffer, 0, 1024);

    memset(package, 0xFF, sizeof(package));
    memcpy(package, FIXABLE_LENGTH_ERASE_CMD, FIXABLE_LENGTH_ERASE_CMD_SIZE);
    package[FIXABLE_LENGTH_ERASE_CMD_SIZE] = sizeof(driver_ota_info_s);
    memcpy(package + FIXABLE_LENGTH_ERASE_CMD_SIZE + 1, &ota_addr, FIXABLE_LENGTH_ERASE_ADDR_SIZE);

    ret = write_command(id, package, (FIXABLE_LENGTH_ERASE_CMD_SIZE + 1 + FIXABLE_LENGTH_ERASE_ADDR_SIZE ));
    if (ret < 0)
    {
        DBGD("%s clear ota_info error\r\n", __func__);
        return -1;
    }

    ret = read_command_evt(id, buffer, 1024, 3000);

    if (ret <= 0)
    {
        DBGD("%s clear ota_info failed\r\n", __func__);
        if (buffer != NULL)
        {
            os_free(buffer);
        }
        return -1;
    }

    if (status == OTA_MARK_SUCC)
    {
        memset(package, 0xFF, sizeof(package));
        memcpy(package, FIXABLE_LENGTH_WRITE_CMD, FIXABLE_LENGTH_WRITE_CMD_SIZE);
        ota_info.crc = calc_crc();
        ota_info.flag = IMAGE_ENV;
        ota_info.mark = OTA_MARK_SUCC;
        ota_info.addr1 = ota_data_addr;
        ota_info.addr2 = BOOTLOADER_SIZE;
        crc_all = get_crc_all();
        ota_info.reserved[0] = (crc_all & 0xff00) >> 8;
        ota_info.reserved[1] = crc_all & 0xff;
        ota_info.len = get_slave_bin_size();
        ota_info.info_crc = gen_crc16(0xffff, (uint8_t *)&ota_info, OTA_INFO_LEN);
        package[5] = (sizeof(driver_ota_info_s) + 1 + FIXABLE_LENGTH_WRITE_ADDR_SIZE) & 0xFF;
        package[6] = ((sizeof(driver_ota_info_s) + 1 + FIXABLE_LENGTH_WRITE_ADDR_SIZE) >> 8) & 0xFF;
        DBGD("%s crc_all %x\r\n", __func__, get_crc_all);

        memcpy(package + FIXABLE_LENGTH_WRITE_CMD_SIZE, &ota_addr, FIXABLE_LENGTH_WRITE_ADDR_SIZE);
        memcpy(package + FIXABLE_LENGTH_WRITE_CMD_SIZE + FIXABLE_LENGTH_WRITE_ADDR_SIZE, &ota_info, sizeof(driver_ota_info_s));

        ret = write_command(id, package, FIXABLE_LENGTH_WRITE_CMD_SIZE + FIXABLE_LENGTH_WRITE_ADDR_SIZE + sizeof(driver_ota_info_s));
        if (ret < 0)
        {
            DBGD("%s write ota_info failed\r\n", __func__);
        }
        ret = read_command_evt(id, buffer, 1024, 3000);
        if (ret <= 0)
        {
            DBGD("%s write ota_info failed\r\n", __func__);
            if (buffer != NULL)
            {
                os_free(buffer);
            }
            return -1;
        }

        memset(package, 0xFF, sizeof(package));
        memcpy(package, FIXABLE_LENGTH_READ_CMD, FIXABLE_LENGTH_READ_CMD_SIZE);
        package[5] = (2 + FIXABLE_LENGTH_READ_ADDR_SIZE) & 0xFF;
        package[6] = ((2 + FIXABLE_LENGTH_READ_ADDR_SIZE) >> 8) & 0xFF;
        memcpy(package + FIXABLE_LENGTH_READ_CMD_SIZE, &ota_addr, FIXABLE_LENGTH_READ_ADDR_SIZE);
        package[FIXABLE_LENGTH_READ_CMD_SIZE + FIXABLE_LENGTH_WRITE_ADDR_SIZE] = sizeof(driver_ota_info_s);

        ret = write_command(id, package, (FIXABLE_LENGTH_READ_CMD_SIZE + FIXABLE_LENGTH_READ_ADDR_SIZE + 1));
        if (ret < 0)
        {
            DBGD("%s read ota_info failed\r\n", __func__);
            return -1;
        }
        ret = read_command_evt(id, buffer, 1024, 3000);
        if (ret <= 0)
        {
            DBGD("%s read ota_info failed\r\n", __func__);
            if (buffer != NULL)
            {
                os_free(buffer);
            }
            return -1;
        }

    }
    else
    {
        memset(package, 0xFF, sizeof(package));
        memcpy(package, FIXABLE_LENGTH_WRITE_CMD, FIXABLE_LENGTH_WRITE_CMD_SIZE);
        ota_info.mark = OTA_MARK_FAIL;
        ota_info.info_crc = gen_crc16(0xffff, (uint8_t *)&ota_info, OTA_INFO_LEN);

        memcpy(package + FIXABLE_LENGTH_WRITE_ADDR_SIZE, &ota_addr, FIXABLE_LENGTH_WRITE_ADDR_SIZE);
        memcpy(package + FIXABLE_LENGTH_WRITE_CMD_SIZE + FIXABLE_LENGTH_WRITE_ADDR_SIZE, &ota_info, sizeof(driver_ota_info_s));

        ret = write_command(id, package, FIXABLE_LENGTH_WRITE_CMD_SIZE + FIXABLE_LENGTH_WRITE_ADDR_SIZE + sizeof(driver_ota_info_s));
        if (ret < 0)
        {
            DBGD("%s write ota_info error\r\n", __func__);
        }
		ret = read_command_evt(id, buffer, 1024, 3000);
        if (ret <= 0)
        {
            DBGD("%s write ota_info failed\r\n", __func__);
        }
        if (buffer != NULL)
        {
            os_free(buffer);
        }
        return ret;

    }
#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("%s write ota_info success\n", __func__);
        
    }
#endif
    if (buffer != NULL)
    {
        os_free(buffer);
    }
    return ret;
}

static int check_flash_status(uart_id_t id, int retry_times)
{
    int ret, count = retry_times;
    // unsigned char buffer[1024] = {0};
    unsigned char *buffer = NULL;
    buffer = os_malloc(1024);
    if (buffer == NULL)
    {
        DBGD("%s malloc failed\r\n", __func__);
        return -1;
    }
    memset(buffer, 0, 1024);
    do
    {
        DBGV("%s\r\n", __func__);
        count --;

        ret = write_command(id, CHECK_FLASH_STATUS_CMD, sizeof(CHECK_FLASH_STATUS_CMD) - 1);

        if (ret < 0)
        {
            DBGD("%s error\r\n", __func__);
            if (buffer != NULL)
            {
                os_free(buffer);
            }
            return -1;
        }

        ret = read_command_evt(id, buffer, 1024, 800);

        if (ret != 13)
        {
            continue;
        }

        if ((buffer[12] & 0x3) == 0)
        {
            break;
        }

    }
    while (count);
    if (count == 0)
    {
        DBGD("%s, check_flash_status failed\r\n", __func__);
        if (buffer != NULL)
        {
            os_free(buffer);
        }
        return -1;
    }

    if (buffer != NULL)
    {
        os_free(buffer);
    }
    return 0;
}


static int write_flash_wr_qe(uart_id_t id)
{
    int ret = 0;

    ret = check_flash_status(id, 10);

    if (ret < 0)
    {
        DBGD("%s, check_flash_status failed\r\n", __func__);
        return WRITE_FLASH_WR_QE_ERROR;
    }

    ret = write_flash_reg2(id, 0x42, 10);

    if (ret < 0)
    {
        DBGD("%s, write_flash_reg2 failed\r\n", __func__);
        return WRITE_FLASH_WR_QE_ERROR;
    }

    return ret;
}

static void bsc_uart_clean_dirty_data(uint8_t uart_id)
{
    uint8_t tmp_read_buffer[128] = {0};

    while (bk_uart_read_bytes(uart_id, tmp_read_buffer, sizeof(tmp_read_buffer), 100) > 0)
    {
        rtos_delay_milliseconds(50);
    }
}

static int open_serial(uart_id_t id, int baud)
{
    int ret = 0;
	uart_config_t uart_config = {0};
    uart_config.baud_rate = baud;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_NONE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_FLOWCTRL_DISABLE;
    uart_config.src_clk = UART_SCLK_XTAL_26M;
    uart_config.enable_sw_flow_ctrl = 1,
    uart_config.rts_gpio = CONFIG_BLUETOOTH_BSC_RTS_PIN,
    uart_config.cts_gpio = CONFIG_BLUETOOTH_BSC_CTS_PIN,

   	ret = bk_uart_init(id, &uart_config);
    if (ret < 0)
    {
        DBGW("bk_uart_init failed");
    }
	bk_uart_enable_rx_interrupt(id);
    bsc_uart_clean_dirty_data(id);

    return ret;
}

static int transfer_firmware_to_device(uart_id_t id)
{
    int ret;
    unsigned char buffer[32] = {0};

    open_serial(id,BAUD_HCI_MODE);
    DBGD("open serial %d\r\n", id);

    set_uart_baud(id, BAUD_HCI_MODE);
    if (BAUD_HCI_MODE == get_local_baud(id))
    {
        set_bt_power(POWER_OFF);
        rtos_delay_milliseconds(200);
        set_bt_power(POWER_ON);
        //NOTICE: delay needs to be decided based on the actual situation
        rtos_delay_milliseconds(120);
    } else
    {
        return -1;
    }
    
    
    DBGD("set uart baud %d\r\n", BAUD_HCI_MODE);
    if (BAUD_HCI_MODE != get_local_baud(id))
    {
        return -1;
    }

    ret = handshake(id);
    bsc_uart_clean_dirty_data(id);
    rtos_delay_milliseconds(400);

    flash_mid = read_flash_mid(id);

    if (!flash_mid)
    {
        DBGD("%s read_flash_mid failed\r\n", __func__);
        return ret;
    }

    DBGD("get flash id: %08X\r\n", flash_mid);

    get_ota_addr(flash_mid);

    DBGD("change baud from %d to %d for download\r\n",get_local_baud(id), BAUD_FLASH_MODE);
    ret = change_baud(id, BAUD_FLASH_MODE);
    if (ret < 0)
    {
        DBGD("%s change_baud failed\r\n", __func__);
        return ret;
    }

#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("%s disable flash protect\n", __func__);
        
    }
#endif

    ret = flash_protect_opration(id, flash_mid, 0);

    if (ret < 0)
    {
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("\b\r%s flash_protect_opration false failed\n", __func__);
            
        }
#endif
        return ret;
    }

    rtos_delay_milliseconds(500);

    ret = erase_flash_ota(id);

    if (ret < 0)
    {
#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("%s erase_flash failed\n", __func__);
        
    }
#endif
        return ret;
    }

#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("\b\r%s download flash\n", __func__);
        
    }
#endif
    rtos_delay_milliseconds(500);

    ret = download_flash_ota(id, flash_mid);


    if (ret < 0)
    {
        DBGD("%s download_flash failed\r\n", __func__);
        return ret;
    }

    rtos_delay_milliseconds(500);

#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("%s check_crc_error start\n", __func__);
        
    }
#endif

    ret = check_crc_error_ota(id);

    if (ret < 0)
    {
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("%s check_crc_error failed\n", __func__);
            
        }
#endif

        set_ota_info(id, OTA_MARK_FAIL, ota_info_addr);
    }
    else
    {
        set_ota_info(id, OTA_MARK_SUCC, ota_info_addr);
    }
    rtos_delay_milliseconds(500);

#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("\b\r%s enable flash protect\n", __func__);
        
    }
#endif

    ret = flash_protect_opration(id, flash_mid, 1);

    if (ret < 0)
    {
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("%s flash_protect_opration true failed\n", __func__);
            
        }
#endif
        return ret;
    }

    if (flash_mid == FLASH_ID_Puya_25Q16H
        || flash_mid == FLASH_ID_Puya_25Q32H
        || flash_mid == FLASH_ID_Puya_25Q16SH)
    {
        DBGV("%s set register QE as 1\r\n", __func__);

        ret = write_flash_wr_qe(id);

        if (ret < 0)
        {
            DBGD("%s write_flash_wr_qe failed\r\n", __func__);
            return ret;
        }
    }

#ifdef CMD_LINE_OUTPUT
    if(print_log_flag)
    {
        DBGD("%s restart device\n", __func__);
        
    }
#endif

    ret = reboot_dut_ex(id);
    rtos_delay_milliseconds(10);

    if (ret < 0)
    {
        DBGD("%s reboot_dut_ex failed\r\n", __func__);
        return REBOOT_DUT_EX_ERROR;
    }

    set_uart_baud(id, BAUD_HCI_MODE);

    ret = read_command_evt(id, buffer, sizeof(buffer), 10000);

    if (ret <= 0)
    {
        DBGD("%s write ota_info failed\r\n", __func__);
    }
    else
    {
        DBGD("ota update successed\r\n");
    }

    if(memcmp(HCI_SOFTRESET_RSP, buffer, 7) == 0)
    {
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("%s complete\n", __func__);
            
        }
#endif
    }
    else
    {
#ifdef CMD_LINE_OUTPUT
        if(print_log_flag)
        {
            DBGD("%s failed \n", __func__);
            
        }
#endif
    }

    return 0;
}


int userial_close(uart_id_t id)
{
    int ret = 0;
    ret = bk_uart_deinit(id);

    if(ret != 0)
    {
        DBGW("close serial %d failed\r\n", id);
    }
    DBGD("close serial %d success\r\n", id);
    return ret;
}

static int set_bt_power(int on)
{
    int ret = 0;

    switch(on)
    {
        case 0:
            bk_gpio_set_value(BK3515N_BOOTUP_PIN, 0);
            break;

        case 1:
			bk_gpio_set_value(BK3515N_BOOTUP_PIN, 2);
            break;
    }

    return ret;
}

static int read_version_by_file_compare(
    uint16_t year_t, uint8_t month_t, uint8_t day_t,
    uint16_t hour_t, uint8_t minute_t, uint8_t second_t)
{
    int ret = NO_ERROR;
    unsigned char buffer[128];
    unsigned int year, month, day;
    unsigned int hour, minute, second;
    int timep_l,timep_r;
    struct tm info;
    unsigned int bk3515_ota_address = get_slave_bin_addr();

    ret = bk_flash_read_bytes(bk3515_ota_address+ FILE_VERSION_ADDR, buffer, BUILD_VERSION_SIZE);

    if (ret < 0)
    {
        DBGD("%s read %s faild\r\n", __func__);
        ret = READ_DATA_ERROR;
        goto error;
    }

    month = str2month(buffer);
    day = str2int(buffer, 4, 2);
    year = str2int(buffer, 7, 4);
    hour = str2int(buffer, 12, 2);
    minute = str2int(buffer, 15, 2);
    second = str2int(buffer, 18, 2);


    info.tm_year = year - 1900;
    info.tm_mon = month - 1;
    info.tm_mday = day;
    info.tm_hour = hour;
    info.tm_min = minute;
    info.tm_sec = second;
    info.tm_isdst = -1;

    //下载的Bin文件时间
    timep_l = mktime(&info); 

    info.tm_year = year_t - 1900;
    info.tm_mon = month_t - 1;
    info.tm_mday = day_t;
    info.tm_hour = hour_t;
    info.tm_min = minute_t;
    info.tm_sec = second_t;
    info.tm_isdst = -1;

    //3515时间
    timep_r = mktime(&info); 

    DBGD("Local file, %d/%d/%d %d:%d:%d, timep_l:%d, timep_r:%d\r\n", year, month, day, hour, minute, second, timep_l, timep_r);

    if (timep_l > timep_r)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

error:

    return ret;
}

static int firmware_version_read(uart_id_t id, int baud)
{
    int ret = NO_ERROR, i;
    unsigned char buffer[14] = {0}, *p;
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    uint8_t read_buffer[3];
    uint32_t slave_bin_addr = get_slave_bin_addr();
    bk_flash_read_bytes(slave_bin_addr, read_buffer, 3);

    if (!(read_buffer[0] == 0x52 && read_buffer[1] == 0x42 && read_buffer[2] == 0x4C))
    {
        DBGE("slave bin head is error\r\n");
        return ret;
    } else
    {
        set_bt_power(POWER_OFF);
        rtos_delay_milliseconds(200);
        set_bt_power(POWER_ON);

        open_serial(id, baud);
        set_local_baud(id, baud);
        //Notice: need delay about 460ms to wait 3515 join app
        rtos_delay_milliseconds(300);
        ret = write_command(id, VERSION_CMD, sizeof(VERSION_CMD) - 1);

        if (ret < 0)
        {
            DBGD("%s send VERSION cmd error\r\n", __func__);
            ret = WRITE_DATA_ERROR;
            goto error;
        }

        ret = read_command_evt(id, buffer, sizeof(buffer), 4000);

        if (ret < 0)
        {
            DBGD("%s read VERSION cmd error1, %d\r\n", __func__, ret);
            ret = READ_DATA_ERROR;
            goto error;
        }

        if (buffer[4] != 0xA1 || buffer[5] != 0xFC || ret != 14)
        {
            DBGD("%s read VERSION cmd error2, %d\r\n", __func__, ret);
            for (i = 0; i < ret; i++)
            {
                DBG_RAW_D("0x%02x ", buffer[i]);
                if (i % 16 == 15)
                {
                    DBG_RAW_D("\n");
                }
            }
            ret = READ_ERROR_DATA_ERROR;
            goto error;
        }

        p = buffer + 7;

        STREAM_TO_UINT16(year, p);
        STREAM_TO_UINT8(month, p);
        STREAM_TO_UINT8(day, p);

        STREAM_TO_UINT8(hour, p);
        STREAM_TO_UINT8(minute, p);
        STREAM_TO_UINT8(second, p);


        DBGD("3515 Firmware time is, %d/%d/%d %d:%d:%d\r\n", year, month, day, hour, minute, second);

        ret = read_version_by_file_compare(year, month, day,
            hour, minute, second);

    error:
        userial_close(id);
        return ret;
    }
    
}

int update_bk3515_firmware(uart_id_t id)
{
    int ret;

    ret = firmware_version_read(id, BAUD_HCI_MODE);

    if (ret != 0)
    {
       DBGD("start transfer firmware to update\r\n");
       ret = transfer_firmware_to_device(id);
       if(ret < 0)
       { 
           DBGD("transfer firmware to device failed");
           return ret;
       }

    } else 
    {
        DBGD("don't need update firmware\r\n");
    }
   
    return ret;

}

int bk3515_ota_init(void)
{
#if CONFIG_UART_SW_FLOW_CTRL
    mb_flash_register_op_uart_notify(set_uart_sw_flow_state);
#endif
    bk_http_register_save_slave_bin_info_callback(save_slave_bin_info_to_ef);
    return 0;
}



