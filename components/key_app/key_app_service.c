#include <common/sys_config.h>
#include <components/log.h>
#include <stdbool.h>
#include "key_app_service.h"
#include "key_app_config.h"

#define TAG "key_service"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/*
 * Anti-bounce throttle. This only guards against the exact same key + action
 * being reported twice back-to-back; it is deliberately per (pin, action) so it
 * never blocks a different key, nor a double press that follows a single press.
 * The real single/double/long discrimination is done in the driver state
 * machine (see multi_button SHORT_TICKS), so this window can stay small.
 */
#define KEY_EVENT_THROTTLE_MS 100

static KeyConfig_t key_configs[] = KEY_DEFAULT_CONFIG_TABLE;

static const key_action_cfg_t *s_actions = NULL;
static uint16_t s_action_count = 0;
static uint32_t s_last_event_ms = 0;
static uint8_t s_last_pin_id = 0;
static key_action_t s_last_action = SHORT_PRESS;
static bool s_has_last_event = false;

static void key_event_handler(uint8_t event)
{
    uint32_t now_ms = 0;
    uint8_t pin_id = 0;
    key_action_t action = SHORT_PRESS;
    bool event_found = false;

    if (IS_INVALID_EVENT(event))
    {
        LOGI("Invalid event: %d\r\n", event);
        return;
    }

    for (uint16_t i = 0; i < sizeof(key_configs) / sizeof(key_configs[0]); i++)
    {
        if (key_configs[i].short_event == event)
        {
            pin_id = key_configs[i].gpio_id;
            action = SHORT_PRESS;
            event_found = true;
            break;
        }
        if (key_configs[i].double_event == event)
        {
            pin_id = key_configs[i].gpio_id;
            action = DOUBLE_PRESS;
            event_found = true;
            break;
        }
        if (key_configs[i].long_event == event)
        {
            pin_id = key_configs[i].gpio_id;
            action = LONG_PRESS;
            event_found = true;
            break;
        }
        if (key_configs[i].hold_event == event)
        {
            pin_id = key_configs[i].gpio_id;
            action = HOLD_PRESS;
            event_found = true;
            break;
        }
    }

    if (!event_found)
    {
        LOGI("event %d is not mapped to a key\r\n", event);
        return;
    }

    now_ms = rtos_get_time();
    if (s_has_last_event &&
        (s_last_pin_id == pin_id) && (s_last_action == action) &&
        ((now_ms - s_last_event_ms) < KEY_EVENT_THROTTLE_MS))
    {
        LOGI("key event %d throttled, delta=%u ms\r\n",
             event, (unsigned)(now_ms - s_last_event_ms));
        return;
    }
    s_last_event_ms = now_ms;
    s_last_pin_id = pin_id;
    s_last_action = action;
    s_has_last_event = true;

    for (uint16_t i = 0; i < s_action_count; i++)
    {
        key_action_cb_t callback = NULL;

        if (s_actions[i].pin_id != pin_id)
        {
            continue;
        }

        switch (action)
        {
            case SHORT_PRESS:
                callback = s_actions[i].short_callback;
                break;
            case DOUBLE_PRESS:
                callback = s_actions[i].double_callback;
                break;
            case LONG_PRESS:
                callback = s_actions[i].long_callback;
                break;
            case HOLD_PRESS:
                callback = s_actions[i].hold_callback;
                break;
            default:
                break;
        }

        if (callback != NULL)
        {
            callback();
        }
        else
        {
            LOGI("GPIO_%d action %d has no registered callback\r\n",
                 pin_id, action);
        }
        return;
    }

    LOGI("GPIO_%d has no registered action table\r\n", pin_id);
}

void bk_key_service_init(const key_action_cfg_t *actions, uint16_t num_actions)
{
    s_actions = actions;
    s_action_count = num_actions;
    s_last_event_ms = 0;
    s_has_last_event = false;

    bk_key_driver_init(key_configs, sizeof(key_configs) / sizeof(KeyConfig_t));
    rtos_delay_milliseconds(200);
    bk_key_register_event_handler(key_event_handler);
}
