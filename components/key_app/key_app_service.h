#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include "key_app_config.h"

/*
 * Initialize the key service with an application-provided action table.
 * Each entry maps one pin to its short/double/long callbacks, so the service
 * never calls solution code directly.
 */
void bk_key_service_init(const key_action_cfg_t *actions, uint16_t num_actions);

#ifdef __cplusplus
}
#endif
