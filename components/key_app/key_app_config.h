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
#define USER_PET_ENTER         (USER_EVENT_START + 3)
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
#define USER_PET_DOUBLE        (USER_EVENT_START + 4)
#define USER_PET_DOUBLE_EVENT  ((key_event_t)USER_PET_DOUBLE)
#else
#define USER_PET_DOUBLE_EVENT  EVENT_NONE
#endif


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
        .double_event = USER_PET_DOUBLE_EVENT, \
        .long_event = (key_event_t)USER_PET_ENTER \
    } \
}


#ifdef __cplusplus
}
#endif