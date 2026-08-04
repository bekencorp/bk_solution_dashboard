/**
 * @file pbap_contacts.h
 *
 * Self-contained PBAP-based contact cache for caller-ID name resolution.
 *
 * Flow: the module registers a bt_manager GAP callback and drives itself off
 * ACL up/down. When a remote device connects, it connects PBAP PCE to that
 * device, pulls the main phonebook (telecom/pb.vcf, FN/N/TEL only) and caches
 * <normalized-number, name> pairs in PSRAM. Any consumer (e.g. the HFP layer on
 * an incoming call) calls pbap_contacts_lookup() to turn a number into a name.
 * The module has no dependency on HFP/A2DP.
 */

#ifndef PBAP_CONTACTS_H
#define PBAP_CONTACTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Initialize the PBAP contact module: allocate the PSRAM cache, create
 *        the worker queue/task, init PBAP PCE, and register a bt_manager GAP
 *        callback so phonebook pulls are triggered automatically on ACL up.
 *        Call once after the Bluetooth stack (bt_manager) is enabled. Safe to
 *        call multiple times.
 */
void pbap_contacts_init(void);

/**
 * @brief Resolve a phone number to a cached contact name.
 *
 * @param number  Phone number string (may contain '+', spaces, dashes).
 * @return Pointer to a NUL-terminated name (valid until the next lookup call),
 *         or NULL if no match is cached.
 */
const char *pbap_contacts_lookup(const char *number);

/**
 * @brief One cached contact, as exported to UI consumers.
 */
typedef struct {
    char name[40];     /* FN preferred, else N; UTF-8 */
    char number[32];   /* Raw TEL value as pulled from the phonebook */
} pbap_contact_info_t;

/**
 * @brief Number of contacts currently cached (0 when nothing pulled yet).
 */
int pbap_contacts_count(void);

/**
 * @brief Copy up to @p max cached contacts into @p out (thread-safe snapshot).
 *
 * @param out  Destination array supplied by the caller.
 * @param max  Capacity of @p out in elements.
 * @return Number of contacts actually copied (<= max).
 */
int pbap_contacts_snapshot(pbap_contact_info_t *out, int max);

/**
 * @brief Call-history entry direction/type.
 */
typedef enum {
    PBAP_CALL_UNKNOWN = 0,
    PBAP_CALL_DIALED,     /* outgoing */
    PBAP_CALL_RECEIVED,   /* incoming */
    PBAP_CALL_MISSED,     /* missed */
} pbap_call_type_t;

/**
 * @brief One cached call-history (recents) entry, as exported to UI consumers.
 */
typedef struct {
    char    name[40];     /* FN preferred, else N, else the number; UTF-8 */
    char    number[32];   /* Raw TEL value */
    char    datetime[16]; /* Raw "YYYYMMDDThhmmss" (may be empty) */
    uint8_t type;         /* pbap_call_type_t */
} pbap_recent_info_t;

/**
 * @brief Whether a remote device ACL is currently connected.
 * @return 1 if connected, else 0.
 */
int pbap_contacts_is_connected(void);

/**
 * @brief Friendly name of the connected remote device.
 * @return NUL-terminated name ("" when unknown or not connected). The pointer is
 *         valid until the next connection/name change.
 */
const char *pbap_contacts_peer_name(void);

/**
 * @brief Number of call-history entries currently cached.
 */
int pbap_recents_count(void);

/**
 * @brief Copy up to @p max cached recents into @p out (thread-safe snapshot).
 *        Entries are in the order returned by the phone (typically newest first).
 *
 * @return Number of entries actually copied (<= max).
 */
int pbap_recents_snapshot(pbap_recent_info_t *out, int max);

/**
 * @brief Callback invoked once a phonebook pull finishes and the cache is
 *        refreshed. Runs in the PBAP worker-thread context, so a UI consumer
 *        must marshal any LVGL work to the LVGL thread (e.g. lv_async_call).
 *        Fired after the contacts pull and again after the call-history pull.
 */
typedef void (*pbap_contacts_updated_cb_t)(void *user_data);

/**
 * @brief Register (or clear, with NULL) the cache-updated callback.
 */
void pbap_contacts_set_updated_cb(pbap_contacts_updated_cb_t cb, void *user_data);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PBAP_CONTACTS_H */
