#pragma once

#include <common/bk_err.h>
#include <stdint.h>

bk_err_t wifi_boarding_adv_init(void);
void wifi_boarding_adv_set_device_info(uint8_t device_type,
                                        uint8_t fw_major,
                                        uint8_t fw_minor,
                                        uint8_t fw_patch);
bk_err_t wifi_boarding_adv_start(void);
bk_err_t wifi_boarding_adv_stop(void);
