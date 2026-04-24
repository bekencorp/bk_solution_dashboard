#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <key_main.h>
#include <key_adapter.h>


#define KEY_GPIO_48   GPIO_48 
#define KEY_GPIO_49   GPIO_49
#define KEY_GPIO_50   GPIO_50

#define USER_CONFIG_NETWORK     (USER_EVENT_START + 0)  
#define USER_ERASE_INFO        (USER_EVENT_START + 1)   
#define USER_PET_TOGGLE        (USER_EVENT_START + 2)


#define KEY_DEFAULT_CONFIG_TABLE \
{ \
    { \
        .gpio_id = KEY_GPIO_48, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_CONFIG_NETWORK, \
        .double_event = EVENT_NONE,	\
        .long_event = EVENT_NONE \
    }, \
    { \
        .gpio_id = KEY_GPIO_49, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_ERASE_INFO, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    },\
    { \
        .gpio_id = KEY_GPIO_50, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_PET_TOGGLE, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    } \
}


#ifdef __cplusplus
}
#endif