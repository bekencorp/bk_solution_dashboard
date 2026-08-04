#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "gpio_driver.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "bsc_api.h"
#include "bsc_main.h"
#include "bsc_main_internal.h"
#include "bsc_hci.h"

#define LOG_TAG "bsc_main"
#define LOG_LEVEL LOG_LEVEL_INFO

static int32_t (*s_report_cb)(uint8_t *data, uint32_t len);
static bsc_config_t s_bsc_config;

static int32_t bsc_send(uint8_t *data, uint32_t len);

static void bsc_uart_clean_dirty_data(uint8_t uart_id)
{
    uint8_t tmp_read_buffer[128] = {0};

    while (bk_uart_read_bytes(uart_id, tmp_read_buffer, sizeof(tmp_read_buffer), 100) > 0)
    {
        rtos_delay_milliseconds(50);
    }
}

static int32_t bsc_uart_enable(uint8_t uart_id, uint8_t enable, uint32_t band)
{
    bk_err_t ret = 0;
    s_bsc_config.uart_id = uart_id;

    LOGI("uart id %d %s baud %d", uart_id, enable ? "enable" : "disable", band);

    if (enable)
    {
        switch (band)
        {
        case 115200:
            s_bsc_config.uart_config.baud_rate = UART_BAUDRATE_115200;
            break;

        case 921600:
            s_bsc_config.uart_config.baud_rate = UART_BAUDRATE_921600;
            break;

        case 1000000:
            s_bsc_config.uart_config.baud_rate = 1000000;
            break;

        case 3250000:
            s_bsc_config.uart_config.baud_rate = UART_BAUDRATE_3250000;
            break;

        case 2000000:
        default:
            s_bsc_config.uart_config.baud_rate = UART_BAUDRATE_2000000;
            break;
        }

        LOGD("before init uart %d", uart_id);
        ret = bk_uart_init(uart_id, &s_bsc_config.uart_config);
        LOGD("after init uart %d", uart_id);

        if (ret != 0)
        {
            LOGE("bk_uart_init err, ret %d", ret);
            return ret;
        }

        LOGI("ble uart %d enable", uart_id);

        ret = bk_uart_enable_rx_interrupt(s_bsc_config.uart_id);

        if (ret)
        {
            LOGE("enable rx int err %d", ret);
            ret = -1;
            goto end;
        }
    }
    else
    {
        bk_uart_deinit(uart_id);
        LOGI("ble uart %d disable", uart_id);
    }

end:;
    return ret;
}

static void bsc_uart_isr(uart_id_t id, void *param)
{
    int32_t ret = 0;

    if (id != s_bsc_config.uart_id)
    {
        LOGE("uart id not match %d %d", id, s_bsc_config.uart_id);
        return;
    }

    if (s_report_cb)
    {
        uint8_t tmp_buffer[128] = {0};

        while ((ret = bk_uart_read_bytes(s_bsc_config.uart_id, tmp_buffer, sizeof(tmp_buffer), 0)) > 0)
        {
            s_report_cb(tmp_buffer, ret);
        }

        if (ret < 0)
        {
            LOGE("uart read bytes err %d", ret);
        }
    }
}

static uint32_t build_cmd(uint8_t *buffer, uint16_t opcode, uint8_t len)
{
    uint8_t *p = buffer;
    uint32_t i = 0;

    p[i] = 0x1;
    p++;
    os_memcpy(p, &opcode, sizeof(opcode));
    p += sizeof(opcode);
    *p = len;
    p++;

    return p - buffer;
}

static int32_t read_compl_evt_parse(uint8_t *buffer, uint16_t max_size, uint16_t expect_op)
{
    int32_t ret = 0;
    int32_t tmp_read_ret = 0;
    int32_t read_len = 0;
    uint32_t need_size = 7;
    uint8_t len = 0;
    uint16_t op = 0;

    //while(1) rtos_delay_milliseconds(100);
    while (read_len < need_size)
    {
        LOGI("%d need read %d %d", __LINE__, need_size - read_len, tmp_read_ret);
        tmp_read_ret = bk_uart_read_bytes(s_bsc_config.uart_id, buffer + read_len, need_size - read_len, 50);//BEKEN_WAIT_FOREVER);

        if (tmp_read_ret == BK_ERR_UART_RX_TIMEOUT)
        {
            continue;
        }

        if (tmp_read_ret < 0)
        {
            LOGE("uart read err %d", tmp_read_ret);
        }

        read_len += tmp_read_ret;
    }

    if (buffer[0] != 0x4)
    {
        LOGE("invalid hci evt 0x%x", buffer[0]);
        ret = -1;
        goto end;
    }

    if (buffer[1] != 0xe)
    {
        LOGE("invalid evt type 0x%x", buffer[1]);
        ret = -1;
        goto end;
    }

    len = buffer[2];

    if (len < 4)
    {
        LOGE("invalid evt len %d", len);
        ret = -1;
        goto end;
    }

    op = ((((uint16_t)buffer[5]) << 8) | buffer[4]);

    if (op != expect_op)
    {
        LOGE("unmatch op 0x%x 0x%x", op, expect_op);
        ret = -1;
        //goto end;
    }

    need_size = len - 4;

    uint8_t tmp_read_len = 0;

    while (tmp_read_len < need_size)
    {
        LOGI("%d need read %d %d", __LINE__, need_size - tmp_read_len, tmp_read_ret);
        tmp_read_ret = bk_uart_read_bytes(s_bsc_config.uart_id, buffer + read_len + tmp_read_len, need_size - tmp_read_len, BEKEN_WAIT_FOREVER);

        if (tmp_read_ret == BK_ERR_UART_RX_TIMEOUT)
        {
            continue;
        }

        if (tmp_read_ret < 0)
        {
            LOGE("uart read err %d", tmp_read_ret);
        }

        tmp_read_len += tmp_read_ret;
    }

end:;

    return ret ? ret : buffer[6];
}

static uint8_t bsc_hci_baud_2_platform_baud(uint8_t input, uint32_t *out)
{
    uint8_t ret = 0;

    switch (input)
    {
    case VSC_BEKEN_USERIAL_BAUD_230400:
        *out = 230400;
        break;

    case VSC_BEKEN_USERIAL_BAUD_460800:
        *out = 460800;
        break;

    case VSC_BEKEN_USERIAL_BAUD_921600:
        *out = 921600;
        break;

    case VSC_BEKEN_USERIAL_BAUD_1M:
        *out = 1000000;
        break;

    case VSC_BEKEN_USERIAL_BAUD_2M:
        *out = 2000000;
        break;

    default:
    case VSC_BEKEN_USERIAL_BAUD_115200:
        *out = 115200;
        break;
    }

    return ret;
}

static uint8_t bsc_platform_baud_2_hci_baud(uint32_t input, uint8_t *out)
{
    uint8_t ret = 0;

    switch (input)
    {
    case 230400:
        *out = VSC_BEKEN_USERIAL_BAUD_230400;
        break;

    case 460800:
        *out = VSC_BEKEN_USERIAL_BAUD_460800;
        break;

    case 921600:
        *out = VSC_BEKEN_USERIAL_BAUD_921600;
        break;

    case 1000000:
        *out = VSC_BEKEN_USERIAL_BAUD_1M;
        break;

    case 2000000:
        *out = VSC_BEKEN_USERIAL_BAUD_2M;
        break;

    default:
    case 115200:
        *out = VSC_BEKEN_USERIAL_BAUD_115200;
        break;
    }

    return ret;
}

static int32_t bsc_power_ctrl(uint8_t enable)
{
    int32_t ret = 0;
    gpio_config_t gpio_cfg =
    {
        .io_mode = GPIO_OUTPUT_ENABLE,
        .pull_mode = GPIO_PULL_UP_EN,
        .func_mode = GPIO_SECOND_FUNC_DISABLE,
    };

    ret = gpio_dev_unmap(s_bsc_config.reset_pin);

    // if (ret)
    // {
    //     LOGE("gpio unmap err %d", ret);
    //     ret = -1;
    //     goto end;
    // }

    ret = bk_gpio_set_config(s_bsc_config.reset_pin, &gpio_cfg);

    // if (ret)
    // {
    //     LOGE("gpio set config err %d", ret);
    //     ret = -1;
    //     goto end;
    // }

    if (!enable)
    {
        ret = bk_gpio_set_output_low(s_bsc_config.reset_pin);

        if (ret)
        {
            LOGE("gpio set low err %d", ret);
            ret = -1;
            goto end;
        }
    }
    else
    {
        rtos_delay_milliseconds(100);

        ret = bk_gpio_set_output_high(s_bsc_config.reset_pin);

        if (ret)
        {
            LOGE("gpio set high err %d", ret);
            ret = -1;
            goto end;
        }
    }

end:;
    return ret;
}

static int32_t bsc_init(int32_t (*report)(uint8_t *data, uint32_t len))
{
    int32_t ret = 0;
    uint32_t write_buffer_index = 0;
    LOGW("");

    s_report_cb = report;

    // ret = gpio_dev_unmap(0);

    // if (ret)
    // {
    //     LOGE("gpio unmap err %d", ret);
    //     ret = -1;
    //     goto end;
    // }

    // ret = gpio_dev_unmap(1);

    // if (ret)
    // {
    //     LOGE("gpio unmap err %d", ret);
    //     ret = -1;
    //     goto end;
    // }

    ret = bsc_uart_enable(s_bsc_config.uart_id, 0, s_bsc_config.init_baud);

    if (ret)
    {
        LOGE("uart disable err %d", ret);
        ret = -1;
        goto end;
    }

    if (
        s_bsc_config.uart_config.flow_ctrl == UART_FLOWCTRL_DISABLE &&
#if CONFIG_UART_SW_FLOW_CTRL
        !s_bsc_config.uart_config.enable_sw_flow_ctrl
#else
        1
#endif
    )
    {
        //for 3515 enable flow ctrl
        gpio_config_t cfg = {.io_mode = GPIO_OUTPUT_ENABLE, .pull_mode = GPIO_PULL_DOWN_EN, .func_mode = GPIO_SECOND_FUNC_DISABLE};

        LOGI("set rts gpio %d to low", s_bsc_config.uart_config.rts_gpio);

        ret = gpio_dev_unmap(s_bsc_config.uart_config.rts_gpio);

        if (ret)
        {
            LOGE("gpio %d rts unmap err %d", s_bsc_config.uart_config.rts_gpio, ret);
            ret = -1;
            goto end;
        }

        ret = bk_gpio_set_config(s_bsc_config.uart_config.rts_gpio, &cfg);

        if (ret)
        {
            LOGE("gpio %d rts set config err %d", s_bsc_config.uart_config.rts_gpio, ret);
            ret = -1;
            goto end;
        }
        
        ret = bk_gpio_set_output_low(s_bsc_config.uart_config.rts_gpio);
        if (ret)
        {
            LOGE("gpio %d rts set value err %d", s_bsc_config.uart_config.rts_gpio, ret);
            ret = -1;
            goto end;
        }

    }

    ret = bsc_power_ctrl(0);

    if (ret)
    {
        LOGE("gpio set low err %d", ret);
        ret = -1;
        goto end;
    }

    rtos_delay_milliseconds(100);

    ret = bsc_power_ctrl(1);

    if (ret)
    {
        LOGE("gpio set high err %d", ret);
        ret = -1;
        goto end;
    }

    ret = bsc_uart_enable(s_bsc_config.uart_id, 1, s_bsc_config.init_baud);

    if (ret)
    {
        LOGE("uart enable err %d", ret);
        ret = -1;
        goto end;
    }

    rtos_delay_milliseconds(300);

    bsc_uart_clean_dirty_data(s_bsc_config.uart_id);
    uint8_t tmp_write_buffer[128] = {0};
    uint8_t tmp_read_buffer[128] = {0};

    //1
    write_buffer_index = build_cmd(tmp_write_buffer, HCI_SOFTWARE_RESET, 0);
    bsc_send(tmp_write_buffer, write_buffer_index);
    LOGI("wait HCI_SOFTWARE_RESET");
    ret = read_compl_evt_parse(tmp_read_buffer, sizeof(tmp_read_buffer), HCI_SOFTWARE_RESET);

    if (ret)
    {
        LOGE("HCI_SOFTWARE_RESET rsp err %d", ret);
        ret = -1;
        goto end;
    }

    // rtos_delay_milliseconds(100);
    // bk_uart_read_bytes(s_bsc_config.uart_id, tmp_read_buffer, sizeof(tmp_read_buffer), 0);

    if (s_bsc_config.init_baud != s_bsc_config.nego_baud)
    {
        //2
        LOGI("baud %d -> %d", s_bsc_config.init_baud, s_bsc_config.nego_baud);
        write_buffer_index = build_cmd(tmp_write_buffer, TCI_BEKEN_SET_CONTROLLER_BAUD, 1);
        bsc_platform_baud_2_hci_baud(s_bsc_config.nego_baud, &tmp_write_buffer[write_buffer_index]);
        write_buffer_index++;
        bsc_send(tmp_write_buffer, write_buffer_index);
        LOGI("wait TCI_BEKEN_SET_CONTROLLER_BAUD");
        ret = read_compl_evt_parse(tmp_read_buffer, sizeof(tmp_read_buffer), TCI_BEKEN_SET_CONTROLLER_BAUD);

        if (ret)
        {
            LOGE("TCI_BEKEN_SET_CONTROLLER_BAUD rsp err %d", ret);
            ret = -1;
            goto end;
        }

#if 0
        bsc_uart_enable(s_bsc_config.uart_id, 0, s_bsc_config.init_baud);
        rtos_delay_milliseconds(40);
        ret = bsc_uart_enable(s_bsc_config.uart_id, 1, s_bsc_config.nego_baud);
#else
        ret = bk_uart_set_baud_rate(s_bsc_config.uart_id, s_bsc_config.nego_baud);
        rtos_delay_milliseconds(100);
#endif

        if (ret)
        {
            LOGE("uart enable err %d", ret);
            ret = -1;
            goto end;
        }
    }

    //3
    write_buffer_index = build_cmd(tmp_write_buffer, TCI_READ_VENDOR_VERSION, 0);
    bsc_send(tmp_write_buffer, write_buffer_index);
    LOGI("wait TCI_READ_VENDOR_VERSION");
    ret = read_compl_evt_parse(tmp_read_buffer, sizeof(tmp_read_buffer), TCI_READ_VENDOR_VERSION);

    if (ret)
    {
        LOGE("TCI_READ_VENDOR_VERSION rsp err %d", ret);
        ret = -1;
        goto end;
    }

    uint8_t *p = tmp_read_buffer + 7;
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;

    LE_STREAM_TO_UINT16(year, p);
    LE_STREAM_TO_UINT8(month, p);
    LE_STREAM_TO_UINT8(day, p);
    LE_STREAM_TO_UINT8(hour, p);
    LE_STREAM_TO_UINT8(minute, p);
    LE_STREAM_TO_UINT8(second, p);

    LOGI("Firmware version: %d/%02d/%02d %02d:%02d:%02d", year, month, day, hour, minute, second);

    //4
    write_buffer_index = build_cmd(tmp_write_buffer, HCI_VSC_WRITE_BD_ADDR, 1);
    tmp_write_buffer[write_buffer_index - 1] = 6;
    uint8_t current_mac[6] = {0};
    bk_get_mac(current_mac, MAC_TYPE_BLUETOOTH);

    for (int32_t i = 0; i < sizeof(current_mac) / 2; i++)
    {
        uint8_t tmp = current_mac[i];
        current_mac[i] = current_mac[sizeof(current_mac) - 1 - i];
        current_mac[sizeof(current_mac) - 1 - i] = tmp;
    }

    os_memcpy(tmp_write_buffer + write_buffer_index, current_mac, sizeof(current_mac));

    write_buffer_index += 6;
    bsc_send(tmp_write_buffer, write_buffer_index);
    LOGI("wait HCI_VSC_WRITE_BD_ADDR");
    ret = read_compl_evt_parse(tmp_read_buffer, sizeof(tmp_read_buffer), HCI_VSC_WRITE_BD_ADDR);

    if (ret)
    {
        LOGE("HCI_VSC_WRITE_BD_ADDR rsp err %d", ret);
        ret = -1;
        goto end;
    }

    //5
    write_buffer_index = build_cmd(tmp_write_buffer, TCI_READ_FRQ_OFFSET_FROM_FLASH, 0);
    bsc_send(tmp_write_buffer, write_buffer_index);
    LOGI("wait TCI_READ_FRQ_OFFSET_FROM_FLASH");
    ret = read_compl_evt_parse(tmp_read_buffer, sizeof(tmp_read_buffer), TCI_READ_FRQ_OFFSET_FROM_FLASH);

    if (ret)
    {
        LOGE("TCI_READ_FRQ_OFFSET_FROM_FLASH rsp err %d", ret);
        ret = -1;
        goto end;
    }

    p = tmp_read_buffer + 7;

    LOGI("TCI_READ_FRQ_OFFSET_FROM_FLASH 0x%x", *p);

    ret = bk_uart_disable_rx_interrupt(s_bsc_config.uart_id);

    if (ret)
    {
        LOGE("disable rx int err %d", ret);
        ret = -1;
        goto end;
    }

    ret = bk_uart_take_rx_isr(s_bsc_config.uart_id, bsc_uart_isr, NULL);

    if (ret)
    {
        LOGE("take rx isr err %d", ret);
        ret = -1;
        goto end;
    }

    ret = bk_uart_enable_rx_interrupt(s_bsc_config.uart_id);

    if (ret)
    {
        LOGE("enable rx int err %d", ret);
        ret = -1;
        goto end;
    }

end:;

    if (ret)
    {
        bk_uart_disable_rx_interrupt(s_bsc_config.uart_id);
        bsc_uart_enable(s_bsc_config.uart_id, 0, s_bsc_config.init_baud);
    }

    LOGW("ret %d", ret);

    return ret;
}

static int32_t bsc_deinit(void)
{
    int32_t ret = 0;
    s_report_cb = NULL;

    bk_uart_disable_rx_interrupt(s_bsc_config.uart_id);
    bk_uart_take_rx_isr(s_bsc_config.uart_id, NULL, NULL);
    ret = bsc_uart_enable(s_bsc_config.uart_id, 0, s_bsc_config.init_baud);
    bsc_power_ctrl(0);
    //os_memset(&s_bsc_config, 0, sizeof(s_bsc_config));
    rtos_delay_milliseconds(200);

    return ret;
}

static int32_t bsc_send(uint8_t *data, uint32_t len)
{
    int32_t ret = 0;

    ret = bk_uart_write_bytes(s_bsc_config.uart_id, data, len);
    //LOGV("uart write %d ret %d", len, ret);
    return ret;
}

static const bk_bluetooth_secondary_callback_t bsc_cb =
{
    .send = bsc_send,
    .init = bsc_init,
    .deinit = bsc_deinit,

    .acl_handle_threshold_min = 0x30,//for 3515ns
    .acl_handle_threshold_max = 0xe10,
};

bk_err_t bk_cpn_bsc_init(bsc_config_t *config)
{
    LOGI("");
    os_memcpy(&s_bsc_config, config, sizeof(*config));
    return bk_bluetooth_reg_secondary_controller((bk_bluetooth_secondary_callback_t *)&bsc_cb);
}

bk_err_t bk_cpn_bsc_deinit(void)
{
    LOGI("");
    return BK_OK;
}
