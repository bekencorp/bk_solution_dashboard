#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <common/sys_config.h>
#include <key_main.h>
#include <key_adapter.h>


#define USER_CONFIG_NETWORK     (USER_EVENT_START + 0)
#define USER_ERASE_INFO        (USER_EVENT_START + 1)
#define USER_PET_TOGGLE        (USER_EVENT_START + 2)
#define USER_PET_ENTER         (USER_EVENT_START + 3)

#if defined(CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0) && CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0

/* V1.0 board: P27_KEY1 / P30_KEY2 / P32_KEY3 / P31_KEY4 / P29_KEY5 */
#define KEY_GPIO_27   GPIO_27
#define KEY_GPIO_29   GPIO_29
#define KEY_GPIO_30   GPIO_30
#define KEY_GPIO_31   GPIO_31
#define KEY_GPIO_32   GPIO_32

/*
 * V1.0-only placeholder events. Start at +10 to avoid colliding with the
 * shared/V2 events (USER_PET_ENTER=+3, USER_PET_DOUBLE=+4, USER_PHONE_*=+5/+6),
 * even though V1.0 and V2 are mutually exclusive builds today.
 */
#define USER_KEY4_PLACEHOLDER  (USER_EVENT_START + 10)
#define USER_KEY5_PLACEHOLDER  (USER_EVENT_START + 11)

#define KEY_DEFAULT_CONFIG_TABLE \
{ \
    { \
        .gpio_id = KEY_GPIO_27, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_CONFIG_NETWORK, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    }, \
    { \
        .gpio_id = KEY_GPIO_30, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_ERASE_INFO, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    }, \
    { \
        .gpio_id = KEY_GPIO_32, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_PET_TOGGLE, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    }, \
    { \
        .gpio_id = KEY_GPIO_31, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_KEY4_PLACEHOLDER, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    }, \
    { \
        .gpio_id = KEY_GPIO_29, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_KEY5_PLACEHOLDER, \
        .double_event = EVENT_NONE, \
        .long_event = EVENT_NONE \
    } \
}

#else /* !CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0 */

#define KEY_GPIO_48   GPIO_48
#define KEY_GPIO_49   GPIO_49
#define KEY_GPIO_50   GPIO_50
#define KEY_GPIO_5    GPIO_5

#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
#define USER_PET_DOUBLE        (USER_EVENT_START + 4)
#define USER_PET_DOUBLE_EVENT  ((key_event_t)USER_PET_DOUBLE)
/* Phone-scenario control button (GPIO_5): short=answer, double=hang up/reject. */
#define USER_PHONE_ANSWER      (USER_EVENT_START + 5)
#define USER_PHONE_HANGUP      (USER_EVENT_START + 6)
#else
#define USER_PET_DOUBLE_EVENT  EVENT_NONE
#endif

/*
 * Optional phone-control button on GPIO_5 (scooter V2 only). Kept as a separate
 * macro because a preprocessor #if cannot live inside the table initializer
 * macro below. Expands to a leading-comma table entry, or to nothing elsewhere.
 */
#if defined(CONFIG_PROJECT_SCOOTER_V2) && CONFIG_PROJECT_SCOOTER_V2
#define KEY_PHONE_CONFIG_ENTRY \
    , { \
        .gpio_id = KEY_GPIO_5, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_PHONE_ANSWER, \
        .double_event = (key_event_t)USER_PHONE_HANGUP, \
        .long_event = EVENT_NONE \
    }
#else
#define KEY_PHONE_CONFIG_ENTRY
#endif

#define KEY_DEFAULT_CONFIG_TABLE \
{ \
    { \
        .gpio_id = KEY_GPIO_48, \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)USER_CONFIG_NETWORK, \
        .double_event = EVENT_NONE, \
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
    KEY_PHONE_CONFIG_ENTRY \
}

#endif /* CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0 */


#ifdef __cplusplus
}
#endif
