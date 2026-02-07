#pragma once

#include "wifi_boarding_demo_service.h"

#include <stdint.h>

#if 1//!defined(CONFIG_BK_BLE_PROVISIONING)
#define WIFI_BOARDING_DEMO_ENABLE 1
#else
#define WIFI_BOARDING_DEMO_ENABLE 0
#endif

enum
{
    BOARDING_DEBUG_LEVEL_ERROR,
    BOARDING_DEBUG_LEVEL_WARNING,
    BOARDING_DEBUG_LEVEL_INFO,
    BOARDING_DEBUG_LEVEL_DEBUG,
    BOARDING_DEBUG_LEVEL_VERBOSE,
};

#define BOARDING_DEBUG_LEVEL BOARDING_DEBUG_LEVEL_INFO

#define wboard_loge(format, ...) do{if(wifi_boarding_demo_get_log_level() >= BOARDING_DEBUG_LEVEL_ERROR)   BK_LOGE("app_board", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define wboard_logw(format, ...) do{if(wifi_boarding_demo_get_log_level() >= BOARDING_DEBUG_LEVEL_WARNING) BK_LOGW("app_board", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define wboard_logi(format, ...) do{if(wifi_boarding_demo_get_log_level() >= BOARDING_DEBUG_LEVEL_INFO)    BK_LOGI("app_board", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define wboard_logd(format, ...) do{if(wifi_boarding_demo_get_log_level() >= BOARDING_DEBUG_LEVEL_DEBUG)   BK_LOGD("app_board", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define wboard_logv(format, ...) do{if(wifi_boarding_demo_get_log_level() >= BOARDING_DEBUG_LEVEL_VERBOSE) BK_LOGV("app_board", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)

#define STREAM_TO_UINT16(u16, p) {u16 = ((uint16_t)(*(p)) + (((uint16_t)(*((p) + 1))) << 8)); (p) += 2;}

int32_t wifi_boarding_demo_main(ble_boarding_info_t * info);
int32_t wifi_boarding_demo_deinit(uint8_t deinit_bluetooth_future);
int32_t wifi_boarding_demo_deinit_because_bluetooth_deinit_future(void);

uint8_t wifi_boarding_demo_get_log_level(void);
void wifi_boarding_demo_set_log_level(uint8_t level);
int wifi_boarding_notify(uint8_t *data, uint16_t length);
