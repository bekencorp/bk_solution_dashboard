/**
 * @file hfp_hf_demo.h
 *
 */

#ifndef HFP_HF_DEMO_H
#define HFP_HF_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int hfp_hf_demo_init(uint8_t msbc_supported);
int hfp_hf_demo_deinit(void);
void hfp_demo_vr(uint8_t enable);
void hfp_demo_dial(uint8_t enable, uint8_t *num);
void hfp_demo_answer(uint8_t accept);
void hfp_demo_cust_cmd(uint8_t *cmd);
int32_t hfp_demo_chld_cmd(uint8_t op);
int32_t hfp_demo_btrh_cmd(uint8_t op);
uint8_t hfp_hf_check_is_iphone(void);

void bk_bt_app_hfp_audio_spk_enable(uint8_t enable);
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*HFP_HF_DEMO_H*/
