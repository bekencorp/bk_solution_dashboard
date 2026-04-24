#pragma once

#include <common/bk_err.h>
#include "lwip/sockets.h"
#include "net.h"

bk_err_t boarding_wifi_sta_connect(char *ssid, char *key);
bk_err_t boarding_wifi_soft_ap_start(char *ssid, char *key, uint16_t channel);


