#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <common/sys_config.h>
#include <key_main.h>
#include <key_adapter.h>


/* ==================== Directional key events ==================== */

/*
 * Keys are modeled purely as directions (up/down/left/right/middle). Every key
 * registers its short/double/long presses as directional events. This header
 * stays generic: it knows nothing about what each event does. The project
 * provides one action-table entry per pin with short/double/long callbacks.
 */
#define USER_KEY_UP_SHORT       (USER_EVENT_START + 0)
#define USER_KEY_UP_DOUBLE      (USER_EVENT_START + 1)
#define USER_KEY_UP_LONG        (USER_EVENT_START + 2)
#define USER_KEY_UP_HOLD        (USER_EVENT_START + 3)
#define USER_KEY_DOWN_SHORT     (USER_EVENT_START + 4)
#define USER_KEY_DOWN_DOUBLE    (USER_EVENT_START + 5)
#define USER_KEY_DOWN_LONG      (USER_EVENT_START + 6)
#define USER_KEY_DOWN_HOLD      (USER_EVENT_START + 7)
#define USER_KEY_LEFT_SHORT     (USER_EVENT_START + 8)
#define USER_KEY_LEFT_DOUBLE    (USER_EVENT_START + 9)
#define USER_KEY_LEFT_LONG      (USER_EVENT_START + 10)
#define USER_KEY_LEFT_HOLD      (USER_EVENT_START + 11)
#define USER_KEY_RIGHT_SHORT    (USER_EVENT_START + 12)
#define USER_KEY_RIGHT_DOUBLE   (USER_EVENT_START + 13)
#define USER_KEY_RIGHT_LONG     (USER_EVENT_START + 14)
#define USER_KEY_RIGHT_HOLD     (USER_EVENT_START + 15)
#define USER_KEY_MIDDLE_SHORT   (USER_EVENT_START + 16)
#define USER_KEY_MIDDLE_DOUBLE  (USER_EVENT_START + 17)
#define USER_KEY_MIDDLE_LONG    (USER_EVENT_START + 18)
#define USER_KEY_MIDDLE_HOLD    (USER_EVENT_START + 19)


/* ==================== Action registration types ==================== */

typedef void (*key_action_cb_t)(void);

typedef struct
{
    uint8_t         pin_id;
    key_action_cb_t short_callback;
    key_action_cb_t double_callback;
    key_action_cb_t long_callback;
    key_action_cb_t hold_callback;   /* Long-press hold, repeats while held */
} key_action_cfg_t;


/* ==================== Board pin tables ==================== */

/* One config-table entry that registers all four presses for a key. */
#define KEY_DIR_ENTRY(pin, sev, dev, lev, hev) \
    { \
        .gpio_id = (pin), \
        .active_level = LOW_LEVEL_TRIGGER, \
        .short_event = (key_event_t)(sev), \
        .double_event = (key_event_t)(dev), \
        .long_event = (key_event_t)(lev), \
        .hold_event = (key_event_t)(hev) \
    }

#if defined(CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0) && CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0

/* DASHBOARD_V_1_0 board: 5 directional keys. */
#define KEY_PIN_UP      GPIO_32
#define KEY_PIN_DOWN    GPIO_27
#define KEY_PIN_LEFT    GPIO_31
#define KEY_PIN_RIGHT   GPIO_29
#define KEY_PIN_MIDDLE  GPIO_30

#define KEY_DEFAULT_CONFIG_TABLE \
{ \
    KEY_DIR_ENTRY(KEY_PIN_UP,     USER_KEY_UP_SHORT,     USER_KEY_UP_DOUBLE,     USER_KEY_UP_LONG,     USER_KEY_UP_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_DOWN,   USER_KEY_DOWN_SHORT,   USER_KEY_DOWN_DOUBLE,   USER_KEY_DOWN_LONG,   USER_KEY_DOWN_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_LEFT,   USER_KEY_LEFT_SHORT,   USER_KEY_LEFT_DOUBLE,   USER_KEY_LEFT_LONG,   USER_KEY_LEFT_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_RIGHT,  USER_KEY_RIGHT_SHORT,  USER_KEY_RIGHT_DOUBLE,  USER_KEY_RIGHT_LONG,  USER_KEY_RIGHT_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_MIDDLE, USER_KEY_MIDDLE_SHORT, USER_KEY_MIDDLE_DOUBLE, USER_KEY_MIDDLE_LONG, USER_KEY_MIDDLE_HOLD) \
}

#else /* !CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0 */

/* non-DASHBOARD board: 4 directional keys (no down). */
#define KEY_PIN_UP      GPIO_5
#define KEY_PIN_LEFT    GPIO_48
#define KEY_PIN_RIGHT   GPIO_49
#define KEY_PIN_MIDDLE  GPIO_50

#define KEY_DEFAULT_CONFIG_TABLE \
{ \
    KEY_DIR_ENTRY(KEY_PIN_UP,     USER_KEY_UP_SHORT,     USER_KEY_UP_DOUBLE,     USER_KEY_UP_LONG,     USER_KEY_UP_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_LEFT,   USER_KEY_LEFT_SHORT,   USER_KEY_LEFT_DOUBLE,   USER_KEY_LEFT_LONG,   USER_KEY_LEFT_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_RIGHT,  USER_KEY_RIGHT_SHORT,  USER_KEY_RIGHT_DOUBLE,  USER_KEY_RIGHT_LONG,  USER_KEY_RIGHT_HOLD), \
    KEY_DIR_ENTRY(KEY_PIN_MIDDLE, USER_KEY_MIDDLE_SHORT, USER_KEY_MIDDLE_DOUBLE, USER_KEY_MIDDLE_LONG, USER_KEY_MIDDLE_HOLD) \
}

#endif /* CONFIG_PROJECT_SCOOTER_DASHBOARD_V_1_0 */


#ifdef __cplusplus
}
#endif
