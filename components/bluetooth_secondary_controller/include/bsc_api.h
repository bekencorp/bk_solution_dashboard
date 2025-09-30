#pragma once

#include <stdint.h>
#include "driver/uart.h"

typedef struct
{
    uart_id_t uart_id;
    uart_config_t uart_config;
    uint32_t init_baud;
    uint32_t nego_baud;
    gpio_id_t reset_pin;
} bsc_config_t;

int32_t bk_cpn_bsc_init(bsc_config_t *config);
int32_t bk_cpn_bsc_deinit(void);
