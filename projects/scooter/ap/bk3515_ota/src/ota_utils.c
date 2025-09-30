#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ota_utils.h"
// #include "driver/uart.h"

extern uint8_t print_log_flag;

typedef struct
{
    uint32_t baud_rate;
} bk3515_uart_config_t;

static bk3515_uart_config_t uart_configs[SOC_UART_ID_NUM_PER_UNIT] = {{115200},{115200},{115200}};

void hex_dump(char *tag, unsigned char *src, unsigned int length)
{
#define MAX_PRINT_SIZE 10

    char buffer [MAX_PRINT_SIZE * 3];
    char hex2str[] = "0123456789ABCDEF";
    unsigned int i = 0, j = 0, sector = length / MAX_PRINT_SIZE;
    char *p;

    if (length == 0)
    {
        return;
    }

    for (i = 0; i < sector; i++)
    {
        p = &buffer[0];

        for (j = 0; j < MAX_PRINT_SIZE; j++)
        {
            *p++ = hex2str[(src[i * MAX_PRINT_SIZE + j] >> 4) & 0x0F];
            *p++ = hex2str[src[i * MAX_PRINT_SIZE + j] & 0x0F];
            *p++ = ' ';
        }

        buffer[MAX_PRINT_SIZE * 3 - 1] = 0;

        DBGD("%s: %s", tag, buffer);
    }

    if (length % MAX_PRINT_SIZE)
    {
        p = &buffer[0];

        for (i = i * MAX_PRINT_SIZE; i < length; i++)
        {
            *p++ = hex2str[(src[i] >> 4) & 0x0F];
            *p++ = hex2str[src[i] & 0x0F];
            *p++ = ' ';
        }

        *p = 0;

        DBGD("%s: %s", tag, buffer);
    }
}


int read_command_evt_internal(uart_id_t id, unsigned char *buffer, unsigned int length, unsigned int msec)
{
    int ret, len=0, header = 0, event = 0, cmd = 0;
    int data_len = 0;
    unsigned int count = 0;
    unsigned char *p = buffer;
    unsigned int status = COMMAND_IDLE;
    unsigned int type = TYPE_UNKNOWN;

    while (1)
    {
        ret = bk_uart_read_bytes(id, p, 1, msec);
#ifdef CMD_LINE_OUTPUT
        if (print_log_flag)
        {
            DBGD("p: 0x%02x\r\n", *p);
        }
#endif
        header = *p;
        if (ret == 1 && header == 0x04)
        {
            ret = bk_uart_read_bytes(id, p + 1, 2, msec);
#ifdef CMD_LINE_OUTPUT
            if (print_log_flag)
            {
                DBGD("*(p+1) *(p+2) :0x%02x 0x%02x\r\n", *(p+1), *(p+2));
            }
#endif
            if (ret != 2)
            {
                return -1;
            }
            event = *(p+1);
            data_len = *(p+2);
            // when event = 0x10 indicates an error,read error data from buffer
            if (event == 0x10)
            {
                len += 3;
                if ((unsigned int)data_len > length - len)
                {
                    return -1;
                }
                ret = bk_uart_read_bytes(id, p + 3, data_len, msec);
                if (ret != data_len)
                {
                    return -1;
                }
                len += data_len;
                return ERROR_RESPONSE_RECEIVED;
            }
            
            status = COMMAND_START;
            len += 3;
            break;
        }
        else
        {
#ifdef CMD_LINE_OUTPUT
            if (print_log_flag)
            {
                DBGD("header: 0x%02x\r\n", header);
            }
#endif
            memset(buffer, 0, length);
            if(count >= 50)
            {
                return ret;
            }
            count ++;
            continue;
        }
    }
    if (status == COMMAND_START)
    {
        if(data_len == 0xff)
        {
            type = TYPE_FLASH;
        }
        else
        {
            type = TYPE_CMD;
        }
    }
    else
    {

        return -1;
    }
    if(type == TYPE_CMD)
    {
        if ((unsigned int)data_len > length - len)
        {

            return -1;
        }
        ret = bk_uart_read_bytes(id, p+3, data_len, msec);
#ifdef CMD_LINE_OUTPUT
        if (print_log_flag)
        {
            for (int i = 0; i < data_len; i++)
            {
                DBG_RAW_D("0x%02x ", *(p+len+i));
                if (i % 16 == 15)
                {
                    DBG_RAW_D("\n");
                }
                
            }  
        }
#endif
        if (ret != data_len)
        {

            return -1;
        }
        len += data_len;
    }
    else if(type == TYPE_FLASH)
    {
        ret = bk_uart_read_bytes(id, p+3, 6, msec);
        if (ret != 6)
        {

            return -1;
        }
        // opcode = (uint16_t)*(uint8_t *)(p+4) + (((uint16_t)*((uint8_t *)(p+4) + 1)) << 8);
        cmd = *(p+6);
        data_len = (uint16_t)*(uint8_t *)(p+7) + (((uint16_t)*((uint8_t *)(p+7) + 1)) << 8);
#ifdef CMD_LINE_OUTPUT
        if (print_log_flag)
        {
            for (int i = 0; i < 6; i++)
            {
                DBG_RAW_D("0x%02x ", *(p+len+i));
            }
            DBG_RAW_D("\n");
        }
#endif
        if(cmd != 0xf4)
        {

            return -1;
        }
        len += 6;
        if ((unsigned int)data_len > length - len)
        {

            return -1;
        }
        ret = bk_uart_read_bytes(id, p+9, data_len, msec);
#ifdef CMD_LINE_OUTPUT
        if (print_log_flag)
        {
            for (int i = 0; i < data_len; i++)
            {
                DBG_RAW_D("0x%02x ", *(p+9+i));
                if (i % 16 == 15)
                {
                    DBG_RAW_D("\n");
                }

            }
        }
#endif
        if (ret != data_len)
        {

            return -1;
        }
        len += data_len;
    }
    else
    {
        return -1;
    }

    return len;
}

int read_command_evt_with_retry(uart_id_t id, unsigned char *buffer, unsigned int length, unsigned int msec)
{
    int retry_count = 0;
    int max_retries = 3;
    int ret = 0;
    while (retry_count < max_retries)
    {
        ret = read_command_evt_internal(id, buffer, length, msec);
        if(ret == ERROR_RESPONSE_RECEIVED)
        {
            retry_count++;
            continue;
        } 
        else
        {
            return ret;
        }

    }
    return ERROR_RESPONSE_RECEIVED;
    
}

int read_command_evt(uart_id_t id, unsigned char *buffer, unsigned int length, unsigned int msec)
{
    int ret;
    ret = read_command_evt_with_retry(id, buffer, length, msec);
    return ret;
}

int write_command(uart_id_t id, void *buffer, unsigned int length)
{
    int ret;
    ret = bk_uart_write_bytes(id, buffer, length);
    return ret;
}

unsigned int str2int(unsigned char *buffer, unsigned char offset, unsigned char length)
{
    char data[10] = {0};
    unsigned int ret = 0, i, count = length > sizeof(data) ? sizeof(data) : length;

    memcpy(data, buffer + offset, count);

    for (i = 0; i < count; i++)
    {
        if (data[i] > '9' || data[i] < '0')
        {
            if(data[i] == 0x20)
            {
                continue;
            }
            break;
        }

        ret = ret * 10 + data[i] - '0';
    }

    return ret;
}

unsigned int str2month(unsigned char *buffer)
{
    unsigned int ret = 0;
    unsigned char month[] = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";
    unsigned int i;

    for (i = 0; i < sizeof(month) - 3; i++)
    {
        if (month[i] == buffer[0]
            && month[i + 1] == buffer[1]
            && month[i + 2] == buffer[2])
        {
            ret = i / 4 + 1;
        }
    }

    return ret;
}

int get_local_baud(uart_id_t id)
{
    if (id < 0 || id >= SOC_UART_ID_NUM_PER_UNIT)
    {
        return -1;
    }

    return uart_configs[id].baud_rate;
}

void set_local_baud(uart_id_t id, int baud)
{
    if (id < 0 || id >= SOC_UART_ID_NUM_PER_UNIT)
    {
       return;
    }
    
    uart_configs[id].baud_rate = baud;
}


