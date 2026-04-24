#include <common/sys_config.h>
#include <components/log.h>
#include "key_app_service.h"
#include "key_app_config.h"
#if CONFIG_BK_NETWORK_PROVISIONING
#include "bk_network_provisioning.h"
#endif

extern void pet_page_toggle(void);

#define TAG "key_service"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)


static void config_network(void)
{
    LOGI("USER_CONFIG_NETWORK\r\n");
#if CONFIG_BK_NETWORK_PROVISIONING
    erase_network_auto_reconnect_info();
    bk_reboot();
#endif
}

static void release_info(void)
{
    LOGI("USER_REASE_INFO\r\n");
#if CONFIG_BK_NETWORK_PROVISIONING
    erase_network_auto_reconnect_info();
#endif
}

static KeyConfig_t key_configs[] = KEY_DEFAULT_CONFIG_TABLE;

static void key_event_handler(uint8_t event)
{
    if (IS_INVALID_EVENT(event))
    {
        LOGI("Invalid event: %d\r\n", event);
        return;
    }
    
    switch(event) {
        case USER_CONFIG_NETWORK:
            config_network();
            break;
        case USER_ERASE_INFO:
            release_info();
            break;
        case USER_PET_TOGGLE:
            LOGI("USER_PET_TOGGLE\r\n");
            pet_page_toggle();
            break;
        default:
            break;
    }
}


void bk_key_service_init(void)
{

    bk_key_driver_init(key_configs, sizeof(key_configs)/sizeof(KeyConfig_t));
    rtos_delay_milliseconds(200);
    bk_key_register_event_handler(key_event_handler);
}