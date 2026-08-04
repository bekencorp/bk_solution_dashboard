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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PBAP_CONTACTS_H */
