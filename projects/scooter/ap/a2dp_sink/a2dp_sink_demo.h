/**
 * @file a2dp_sink_demo.h
 *
 */

#ifndef A2DP_SINK_DEMO_H
#define A2DP_SINK_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int a2dp_sink_demo_init(uint8_t aac_supported);
int a2dp_sink_demo_deinit(void);
int32_t bk_bt_app_avrcp_ct_get_attr(uint32_t attr);
void bk_bt_a2dp_sink_demo_set_auto_accept_connect_req(uint8_t accept);
void a2dp_sink_demo_set_mix(uint8_t enable);

/**
 * @brief Enable bluetooth audio player service
 *
 * This API enables or disables bluetooth audio player service.
 *
 * @param[in] enable  1 to enable, 0 to disable
 *
 */
void bk_bt_app_a2dp_audio_spk_enable(uint8_t enable);

void bk_bt_a2dp_sink_demo_debug_info(void);
int32_t bk_bt_a2dp_sink_demo_try_connect(void);
int32_t bk_bt_a2dp_sink_demo_try_disconnect_current(void);
void bk_bt_app_avrcp_stop_reconnect(void);
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*A2DP_SINK_DEMO_H*/
