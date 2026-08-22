#pragma once

#include <common/bk_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANCS_NOTIFICATION_UID_LEN        4
#define ANCS_NOTIFICATION_SOURCE_LEN     8
#define ANCS_PROTOCOL_MORE               1
#define ANCS_MAX_ATTR_LEN                256
#define ANCS_REASSEMBLY_SIZE             1024

#define ANCS_ATTR_MASK(id)               (1U << (id))
#define ANCS_DEFAULT_ATTR_MASK           (ANCS_ATTR_MASK(ANCS_ATTR_ID_APP_IDENTIFIER) | \
                                          ANCS_ATTR_MASK(ANCS_ATTR_ID_TITLE) | \
                                          ANCS_ATTR_MASK(ANCS_ATTR_ID_SUBTITLE) | \
                                          ANCS_ATTR_MASK(ANCS_ATTR_ID_MESSAGE) | \
                                          ANCS_ATTR_MASK(ANCS_ATTR_ID_DATE))

typedef enum
{
    ANCS_EVENT_NOTIFICATION_ADDED = 0,
    ANCS_EVENT_NOTIFICATION_MODIFIED,
    ANCS_EVENT_NOTIFICATION_REMOVED,
} ancs_event_id_t;

typedef enum
{
    ANCS_EVENT_FLAG_SILENT = (1 << 0),
    ANCS_EVENT_FLAG_IMPORTANT = (1 << 1),
    ANCS_EVENT_FLAG_PRE_EXISTING = (1 << 2),
    ANCS_EVENT_FLAG_POSITIVE_ACTION = (1 << 3),
    ANCS_EVENT_FLAG_NEGATIVE_ACTION = (1 << 4),
} ancs_event_flag_t;

typedef enum
{
    ANCS_CATEGORY_OTHER = 0,
    ANCS_CATEGORY_INCOMING_CALL,
    ANCS_CATEGORY_MISSED_CALL,
    ANCS_CATEGORY_VOICEMAIL,
    ANCS_CATEGORY_SOCIAL,
    ANCS_CATEGORY_SCHEDULE,
    ANCS_CATEGORY_EMAIL,
    ANCS_CATEGORY_NEWS,
    ANCS_CATEGORY_HEALTH_AND_FITNESS,
    ANCS_CATEGORY_BUSINESS_AND_FINANCE,
    ANCS_CATEGORY_LOCATION,
    ANCS_CATEGORY_ENTERTAINMENT,
} ancs_category_id_t;

typedef enum
{
    ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES = 0,
    ANCS_COMMAND_GET_APP_ATTRIBUTES,
    ANCS_COMMAND_PERFORM_NOTIFICATION_ACTION,
} ancs_command_id_t;

typedef enum
{
    ANCS_ATTR_ID_APP_IDENTIFIER = 0,
    ANCS_ATTR_ID_TITLE,
    ANCS_ATTR_ID_SUBTITLE,
    ANCS_ATTR_ID_MESSAGE,
    ANCS_ATTR_ID_MESSAGE_SIZE,
    ANCS_ATTR_ID_DATE,
    ANCS_ATTR_ID_POSITIVE_ACTION_LABEL,
    ANCS_ATTR_ID_NEGATIVE_ACTION_LABEL,
} ancs_attribute_id_t;

typedef enum
{
    ANCS_CLIENT_IDLE = 0,
    ANCS_CLIENT_ADVERTISING,
    ANCS_CLIENT_CONNECTED,
    ANCS_CLIENT_DISCOVERING,
    ANCS_CLIENT_SUBSCRIBING,
    ANCS_CLIENT_READY,
} ancs_client_state_t;

typedef struct
{
    uint8_t event_id;
    uint8_t event_flags;
    uint8_t category_id;
    uint8_t category_count;
    uint32_t notification_uid;
} ancs_notification_t;

typedef struct
{
    uint32_t notification_uid;
    uint32_t present_mask;
    char app_identifier[ANCS_MAX_ATTR_LEN + 1];
    char title[ANCS_MAX_ATTR_LEN + 1];
    char subtitle[ANCS_MAX_ATTR_LEN + 1];
    char message[ANCS_MAX_ATTR_LEN + 1];
    char date[ANCS_MAX_ATTR_LEN + 1];
} ancs_notification_attrs_t;

typedef struct
{
    ancs_client_state_t state;
    uint8_t peer_addr[6];
    uint16_t conn_id;
    uint16_t mtu;
    uint16_t notification_source_handle;
    uint16_t control_point_handle;
    uint16_t data_source_handle;
    uint16_t notification_source_cccd;
    uint16_t data_source_cccd;
    uint32_t pending_uid;
    bool attr_pending;
} ancs_client_status_t;

typedef void (*ancs_notification_callback_t)(const ancs_notification_attrs_t *attributes);

bk_err_t ancs_client_init(ancs_notification_callback_t callback);
bk_err_t ancs_client_adv_start(void);
bk_err_t ancs_client_adv_stop(void);
bk_err_t ancs_client_disconnect(void);
bk_err_t ancs_client_request_attrs(uint32_t notification_uid);
void ancs_client_get_status(ancs_client_status_t *status);

int ancs_protocol_parse_notification(const uint8_t *data, uint16_t len, ancs_notification_t *notification);
int ancs_protocol_build_get_attrs(uint32_t notification_uid, uint32_t attr_mask, uint8_t *buffer, size_t buffer_size, uint16_t *command_len);
int ancs_protocol_parse_attrs(const uint8_t *data, uint16_t len, uint32_t expected_mask, ancs_notification_attrs_t *attributes);
const char *ancs_event_name(uint8_t event_id);
const char *ancs_category_name(uint8_t category_id);

bk_err_t ancs_cli_init(void);

#ifdef __cplusplus
}
#endif
