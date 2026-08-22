#include "ancs_client.h"

#include <string.h>

static uint32_t ancs_get_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void ancs_copy_attr(char *dst, size_t dst_size, const uint8_t *src, uint16_t src_len)
{
    size_t copy_len = src_len;

    if (copy_len >= dst_size)
    {
        copy_len = dst_size - 1;
    }

    if (copy_len)
    {
        memcpy(dst, src, copy_len);
    }
    dst[copy_len] = '\0';
}

const char *ancs_event_name(uint8_t event_id)
{
    switch (event_id)
    {
        case ANCS_EVENT_NOTIFICATION_ADDED:
            return "added";
        case ANCS_EVENT_NOTIFICATION_MODIFIED:
            return "modified";
        case ANCS_EVENT_NOTIFICATION_REMOVED:
            return "removed";
        default:
            return "unknown";
    }
}

const char *ancs_category_name(uint8_t category_id)
{
    static const char *const names[] =
    {
        "other",
        "incoming_call",
        "missed_call",
        "voicemail",
        "social",
        "schedule",
        "email",
        "news",
        "health_fitness",
        "business_finance",
        "location",
        "entertainment",
    };

    if (category_id < (sizeof(names) / sizeof(names[0])))
    {
        return names[category_id];
    }

    return "unknown";
}

int ancs_protocol_parse_notification(const uint8_t *data, uint16_t len, ancs_notification_t *notification)
{
    if (!data || !notification || len != ANCS_NOTIFICATION_SOURCE_LEN)
    {
        return BK_ERR_PARAM;
    }

    notification->event_id = data[0];
    notification->event_flags = data[1];
    notification->category_id = data[2];
    notification->category_count = data[3];
    notification->notification_uid = ancs_get_le32(&data[4]);

    if (notification->event_id > ANCS_EVENT_NOTIFICATION_REMOVED)
    {
        return BK_FAIL;
    }

    return BK_OK;
}

int ancs_protocol_build_get_attrs(uint32_t notification_uid, uint32_t attr_mask, uint8_t *buffer, size_t buffer_size, uint16_t *command_len)
{
    size_t index = 0;

    if (!buffer || !command_len || buffer_size < 5)
    {
        return BK_ERR_PARAM;
    }

    buffer[index++] = ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES;
    buffer[index++] = notification_uid & 0xff;
    buffer[index++] = (notification_uid >> 8) & 0xff;
    buffer[index++] = (notification_uid >> 16) & 0xff;
    buffer[index++] = (notification_uid >> 24) & 0xff;

    for (uint8_t id = ANCS_ATTR_ID_APP_IDENTIFIER; id <= ANCS_ATTR_ID_NEGATIVE_ACTION_LABEL; id++)
    {
        if ((attr_mask & ANCS_ATTR_MASK(id)) == 0)
        {
            continue;
        }

        if (index >= buffer_size)
        {
            return BK_ERR_NO_MEM;
        }

        buffer[index++] = id;
        if (id == ANCS_ATTR_ID_TITLE ||
            id == ANCS_ATTR_ID_SUBTITLE ||
            id == ANCS_ATTR_ID_MESSAGE)
        {
            if ((index + 2) > buffer_size)
            {
                return BK_ERR_NO_MEM;
            }
            buffer[index++] = ANCS_MAX_ATTR_LEN & 0xff;
            buffer[index++] = (ANCS_MAX_ATTR_LEN >> 8) & 0xff;
        }
    }

    *command_len = (uint16_t)index;
    return BK_OK;
}

int ancs_protocol_parse_attrs(const uint8_t *data, uint16_t len, uint32_t expected_mask, ancs_notification_attrs_t *attributes)
{
    uint16_t offset = 5;

    if (!data || !attributes)
    {
        return BK_ERR_PARAM;
    }

    if (len < 5)
    {
        return ANCS_PROTOCOL_MORE;
    }

    if (data[0] != ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES)
    {
        return BK_FAIL;
    }

    memset(attributes, 0, sizeof(*attributes));
    attributes->notification_uid = ancs_get_le32(&data[1]);

    while (offset < len)
    {
        uint8_t attr_id;
        uint16_t attr_len;
        const uint8_t *attr_data;

        if ((uint16_t)(len - offset) < 3)
        {
            return ANCS_PROTOCOL_MORE;
        }

        attr_id = data[offset++];
        attr_len = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
        offset += 2;

        if (attr_len > (uint16_t)(len - offset))
        {
            return ANCS_PROTOCOL_MORE;
        }

        attr_data = &data[offset];
        offset += attr_len;

        if (attr_id <= ANCS_ATTR_ID_NEGATIVE_ACTION_LABEL)
        {
            attributes->present_mask |= ANCS_ATTR_MASK(attr_id);
        }

        switch (attr_id)
        {
            case ANCS_ATTR_ID_APP_IDENTIFIER:
                ancs_copy_attr(attributes->app_identifier, sizeof(attributes->app_identifier), attr_data, attr_len);
                break;
            case ANCS_ATTR_ID_TITLE:
                ancs_copy_attr(attributes->title, sizeof(attributes->title), attr_data, attr_len);
                break;
            case ANCS_ATTR_ID_SUBTITLE:
                ancs_copy_attr(attributes->subtitle, sizeof(attributes->subtitle), attr_data, attr_len);
                break;
            case ANCS_ATTR_ID_MESSAGE:
                ancs_copy_attr(attributes->message, sizeof(attributes->message), attr_data, attr_len);
                break;
            case ANCS_ATTR_ID_DATE:
                ancs_copy_attr(attributes->date, sizeof(attributes->date), attr_data, attr_len);
                break;
            default:
                break;
        }
    }

    if ((attributes->present_mask & expected_mask) != expected_mask)
    {
        return ANCS_PROTOCOL_MORE;
    }

    return BK_OK;
}
