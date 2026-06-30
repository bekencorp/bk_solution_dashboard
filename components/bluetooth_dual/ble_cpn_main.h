#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configure and start BLE advertising for the dashboard companion.
 *
 * Builds a legacy connectable advertisement carrying the local name
 * (SCOOTER-xxxxxx, derived from the BLE identity address) plus the Beken
 * manufacturer id, then enables advertising. bk_dm_prf_gatts_main() no longer
 * auto-enables advertising, so this must be called once after the GATT
 * server has been initialised.
 */
void ble_cpn_main_init(void);

/* Start/stop advertising at runtime. enable==0 stops, otherwise (re)starts. */
int ble_cpn_adv_enable(uint8_t enable);

#ifdef __cplusplus
}
#endif
