#ifndef __DOORBELL_NETWORK_TRANSFER_H__
#define __DOORBELL_NETWORK_TRANSFER_H__

typedef struct
{
    uint32_t wifi_transfer_frame_count;
    uint32_t wifi_transfer_frame_size;
    uint32_t wifi_transfer_rate;
} media_data_process_debug_info_t;

bk_err_t media_bk_network_transfer_init(char *service_name, void *param);
bk_err_t media_bk_network_transfer_deinit(char *service_name);
void get_last_debug_info(media_data_process_debug_info_t *info);

#endif
