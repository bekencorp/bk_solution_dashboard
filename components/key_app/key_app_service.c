#include <common/sys_config.h>
#include <components/log.h>
#include "key_app_service.h"
#include "key_app_config.h"
#if CONFIG_BK_NETWORK_PROVISIONING
#include "bk_network_provisioning.h"
#endif

extern void pet_page_toggle(void);
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
extern void pet_page_enter(void);
extern void pet_page_double(void);
/* Phone-scenario control (GPIO_51), implemented in the scooter V2 app layer. */
extern void phone_key_answer(void);
extern void phone_key_hangup(void);
#endif

#define TAG "key_service"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
#define USER_PET_TOGGLE_THROTTLE_MS 500
#endif


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
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
static uint32_t s_last_pet_toggle_ms = 0;
#endif

static void key_event_handler(uint8_t event)
{
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
    uint32_t now_ms = 0;
#endif

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
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
            now_ms = rtos_get_time();
            if (s_last_pet_toggle_ms != 0 &&
                (now_ms - s_last_pet_toggle_ms) < USER_PET_TOGGLE_THROTTLE_MS)
            {
                LOGI("USER_PET_TOGGLE throttled, delta=%u ms\r\n",
                     (unsigned)(now_ms - s_last_pet_toggle_ms));
                break;
            }

            s_last_pet_toggle_ms = now_ms;
#endif
            LOGI("USER_PET_TOGGLE\r\n");
            pet_page_toggle();
            break;
        case USER_PET_ENTER:
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
            LOGI("USER_PET_ENTER\r\n");
            pet_page_enter();
#endif
            break;
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
        case USER_PET_DOUBLE:
            LOGI("USER_PET_DOUBLE\r\n");
            pet_page_double();
            break;
        case USER_PHONE_ANSWER:
            LOGI("USER_PHONE_ANSWER\r\n");
            phone_key_answer();
            break;
        case USER_PHONE_HANGUP:
            LOGI("USER_PHONE_HANGUP\r\n");
            phone_key_hangup();
            break;
#endif
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