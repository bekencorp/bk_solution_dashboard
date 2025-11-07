#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <key_main.h>
#include <key_adapter.h>


#define KEY_GPIO_32   GPIO_32 
#define KEY_GPIO_33   GPIO_33
#define KEY_GPIO_34   GPIO_34

#define USER_CONFIG_NETWORK     (USER_EVENT_START + 0)  
#define USER_ERASE_INFO        (USER_EVENT_START + 1)   


#define KEY_DEFAULT_CONFIG_TABLE \
{ \
    { \
        .gpio_id = KEY_GPIO_32, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_CONFIG_NETWORK, \
        .double_event = EVENT_NONE,	\
        .long_event = EVENT_NONE \
    }, \
    { \
        .gpio_id = KEY_GPIO_33, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_ERASE_INFO, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    },\
    { \
        .gpio_id = KEY_GPIO_34, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = EVENT_NONE, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    } \
}


#ifdef __cplusplus
}
#endif