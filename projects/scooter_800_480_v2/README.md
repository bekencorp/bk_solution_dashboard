# Scooter Project Development Guide

* [中文](./README_CN.md)

## 1 Project Overview

This project is a Scooter solution based on the BK7258 chip and BK3515NS chip, enabling image data transmission via WiFi and display on LCD screens. The project integrates rich multimedia processing capabilities, network communication functions, and user interface display, suitable for the development of smart mobility devices.

## 2 Features

### 2.1 WiFi Communication
- Support STA mode to connect to existing WiFi networks
- Support AP mode to create WiFi hotspots for other devices to connect
- Support TCP/UDP protocol for image data transmission

### 2.2 Display Functions
- LCD screen display
- LVGL graphics library support
- Support display switching (LVGL interface and WiFi image transmission)
- AVI video playback (optional)

### 2.3 Bluetooth Functions
- Support A2DP/HFP/HID protocols
- Support classic Bluetooth and BLE controllers (multi-controller mode)
- Support BLE broadcast and connection
- Support Bluetooth power management

## 3 Quick Start

### 3.1 Hardware Information
- The Two-Wheeled Vehicle Development Board V1.2(BK3515NS defaults to using UART1 to connect with BK7258(Start from UART1 in development board))
- LCD screen: 800x480
- LCD Adapter Board V1.1 (Compatible with CAN Function)

### 3.2 Compilation and Burning

Please refer to the [Dashboard Solution](../../index.html) for compilation process

Please refer to [Burning Code](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.0.1/get-started/index.html#burn-code) for burning process

The path of the compiled burning bin file: ``projects/scooter/build/bk7258/scooter/package/all-app.bin``

The path The path to obtain the bk3515ns bin file: ``https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller/v02/``

### 3.3 Basic Operation Flow

For detailed operation flow, please refer to [dashboard_app User Guide](../dashboard_app/index.html).

The mobile app configuration is as follows:

    +---------------+------------+------------+
    |Mobile Config  |Bluetooth   |WIFI Nav    |
    +===============+============+============+
    |Image Width    |800         |480         |
    +---------------+------------+------------+
    |Image Height   |480         |320         |
    +---------------+------------+------------+
    |Image Quality  |0-100       |0-100       |
    +---------------+------------+------------+
    |Rotation Angle |0           |0           |
    +---------------+------------+------------+
    |JPEG Convert   |On or Off   |On or Off   |
    +---------------+------------+------------+

### 3.4 Basic Usage Scenarios
1. The device powers on and displays the lvgl interface
2. Connect to WiFi network, display WiFi image transmission, and switch display from lvgl to WiFi image transmission
3. Use UVC camera to capture images, encode to H.264 format, and store to SD card

## 4 Command Line Interface

The project provides rich command line interfaces for controlling various functions:

### 4.1 WiFi Control Commands
```
// Connect to an existing WiFi network
ap_cmd test sta [ssid] [password]

// Create a WiFi hotspot
ap_cmd test ap [ssid] [password]
```

### 4.2 Display Switching Commands
```
// Switch to LVGL interface display
ap_cmd test switch lvgl

// Switch to WiFi image transmission display
ap_cmd test switch wifi
```

### 4.3 Video Server Control Commands
```
// Open video server
ap_cmd test video_server open

// Close video server
ap_cmd test video_server close
```

### 4.4 bluetooth ctrl cmd
- can be used when connection completed

```
// connection to peer
ap_cmd headset connect <xx:xx:xx:xx:xx:xx>

// disconnect
ap_cmd headset disconnect <xx:xx:xx:xx:xx:xx>

// play
headset play

// pause
headset pause

// next
headset next

// prev
headset prev

//fast forward(can specify time ms)
headset fast_forward [msec]

//rewind (can specify time ms)
headset rewind [msec]

// volume up
headset vol_up

// volume down
headset vol_down

// entry pair mod
headset pair_mode

```

## 5 API Reference

### 5.1 Frame Buffer Queue Management API

This section describes the frame buffer queue management API in the scooter project. The project uses two types of frame buffer queues:

1. frame_buffer_que：use to manage UVC camera output images and H.264 encoded images; UVC images will be decoded and H.264 encoded, and finally stored on the SD card
2. media_data_process_que：use to manage images received over WiFi during navigation; these images will be decoded and displayed on the LCD screen

#### 5.1.1 frame_buffer_que

##### 5.1.1.1 frame_queue_init_all
```c
/**
 * @brief Initialize all frame queue data structures
 * 
 * @return bk_err_t Initialization result
 */
bk_err_t frame_queue_init_all(void);
```

##### 5.1.1.2 frame_queue_deinit_all
```c
/**
 * @brief Deinitialize all frame queue data structures
 * 
 * @return bk_err_t Deinitialization result
 */
bk_err_t frame_queue_deinit_all(void);
```

##### 5.1.1.3 frame_queue_malloc
```c
/**
 * @brief Allocate a frame buffer
 * 
 * @param format Image format
 * @param size Requested buffer size in bytes
 * @return frame_buffer_t* Pointer to allocated frame buffer, NULL on failure
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);
```

##### 5.1.1.4 frame_queue_get_frame
```c
/**
 * @brief Get a frame buffer from the ready queue
 * 
 * @param format Image format
 * @param timeout Timeout value in milliseconds
 * @return frame_buffer_t* Retrieved frame buffer, NULL if timeout or error
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);
```

##### 5.1.1.5 frame_queue_complete
```c
/**
 * @brief Return a frame buffer to the ready queue
 * 
 * @param format Image format
 * @param frame Frame buffer to return to queue
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);
```

##### 5.1.1.6 frame_queue_free
```c
/**
 * @brief Free a frame buffer and send message to free queue
 * 
 * @param format Image format
 * @param frame Frame buffer to free
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);
```

#### 5.1.2 media_data_process_que

##### 5.1.2.1 media_frame_queue_init
```c
/**
 * @brief Initialize frame_queue data structure
 *
 * This function initializes the frame_queue data structure based on the specified image format.
 * It sets the maximum number of frames in the queue and initializes the free_que and ready_que.
 * After initialization, it returns the initialization result.
 *
 * @param format Image format
 * @return bk_err_t Initialization result
 */
avdk_err_t media_frame_queue_init(image_format_t format);
```

##### 5.1.2.2 media_frame_queue_deinit
```c
/**
 * @brief Deinitialize frame_queue data structure
 *
 * This function deinitializes the frame_queue data structure.
 * It frees the memory allocated for the frame_queue and its associated queues.
 * After deinitialization, it returns the deinitialization result.
 *
 * @return avdk_err_t Deinitialization result
 */
avdk_err_t media_frame_queue_deinit(void);
```

##### 5.1.2.3 media_frame_queue_malloc
```c
/**
 * @brief Allocate a frame buffer from the frame_queue
 *
 * This function allocates a frame buffer of the specified size from the frame_queue.
 * If the allocation is successful, it returns a pointer to the allocated frame buffer.
 * If the allocation fails, it returns NULL.
 *
 * @param size Requested buffer size in bytes
 * @return frame_buffer_t* Pointer to allocated frame buffer, NULL on failure
 */
frame_buffer_t *media_frame_queue_malloc(uint32_t size);
```

##### 5.1.2.4 media_frame_queue_get_frame
```c
/**
 * @brief Get a frame buffer from the ready queue
 *
 * This function retrieves a frame buffer from the ready queue of the frame_queue.
 * If a frame buffer is available within the specified timeout period, it returns the frame buffer.
 * If no frame buffer is available within the timeout period, it returns NULL.
 *
 * @param timeout Timeout value in milliseconds
 * @return frame_buffer_t* Retrieved frame buffer, NULL if timeout or error
 */
frame_buffer_t *media_frame_queue_get_frame(uint32_t timeout);
```

##### 5.1.2.5 media_frame_queue_complete
```c
/**
 * @brief Return a frame buffer to the ready queue
 *
 * This function returns a frame buffer to the ready queue of the frame_queue.
 * If the frame buffer is successfully returned to the queue, it returns AVDK_ERR_OK.
 * If the frame buffer cannot be returned to the queue, it returns a specific error code.
 *
 * @param frame Frame buffer to return to queue
 * @return avdk_err_t Operation result
 */
avdk_err_t media_frame_queue_complete(frame_buffer_t *frame);
```

##### 5.1.2.6 media_frame_queue_free
```c
/**
 * @brief Free a frame buffer and send message to free queue
 *
 * This function frees the memory allocated for the specified frame buffer.
 * It also sends a message to the free queue to indicate that the frame buffer is available for reuse.
 *
 * @param frame Frame buffer to free
 */
void media_frame_queue_free(frame_buffer_t *frame);
```

### 5.2 LVGL Application API

#### 5.2.1 lvgl_app_init
```c
/**
 * @brief Initialize LVGL application
 * @details Configure LCD display, initialize LVGL graphics library, create UI interface
 */
void lvgl_app_init(void);
```

#### 5.2.2 lvgl_app_deinit
```c
/**
 * @brief Deinitialize LVGL application
 * @details Turn off LCD backlight, clean up LVGL resources
 * @return Returns BK_OK on success, specific error code on failure
 */
bk_err_t lvgl_app_deinit(void);
```

#### 5.2.3 lvgl_app_suspend_display
```c
/**
 * @brief Suspend LVGL display
 * @details Stop LVGL rendering, used to switch to other display modes
 * @return Returns BK_OK on success, specific error code on failure
 */
bk_err_t lvgl_app_suspend_display(void);
```

#### 5.2.4 lvgl_app_resume_display
```c
/**
 * @brief Resume LVGL display
 * @details Resume LVGL rendering, used to switch back to LVGL interface from other display modes
 * @return Returns BK_OK on success, specific error code on failure
 */
bk_err_t lvgl_app_resume_display(void);
```

### 5.3 Navigation Application API

For streaming navigation, the basic flow is as follows:

1. Start navigation service
2. Receive navigation data
3. Parse navigation data, output complete frame
4. Decode navigation complete frame data
5. Display navigation information on LCD
6. End navigation service

.. note::
    This solution is based on a custom data transmission protocol and navigation data format. Users can customize the navigation data format and corresponding unpacking process.

#### 5.3.1 Start Navigation Service

Starting the navigation service includes the following aspects:

1. Initialize the navigation service, prepare to receive navigation data, start the navigation data receiving thread, and listen to the navigation data port
2. Initialize the decoding service, prepare to read navigation complete frame data, start the decoding thread, and switch the LVGL display to navigation display

##### 5.3.1.1 Initialize Receiving Thread
```c
/**
 * @brief Initialize video data processing thread
 * @details Prepare to receive video data, start the video data processing thread
 * @param width Video width
 * @param height Video height
 * @param format Video format
 * @return avdk_err_t Initialization result
 */
avdk_err_t video_data_process_open(uint16_t width, uint16_t height, image_format_t format);
```

##### 5.3.1.2 Initialize Decoding Thread
```c
/**
 * @brief Initialize JPEG decoding manager
 * @details Initialize JPEG decoding manager configuration parameters, create decoder instance, and set LCD display module to decoder mode (switch from LVGL to navigation display)
 * @param queue_size Decoding queue size
 * @param thread_priority Thread priority
 * @param stack_size Thread stack size
 * @param output_format Output format, refer to: `image_format_t`
 * @return avdk_err_t Initialization result
 */
avdk_err_t av_server_jpeg_decode_manager_turn_on(uint32_t queue_size, uint32_t thread_priority, uint32_t stack_size, image_format_t output_format);
```

#### 5.3.2 Receive Navigation Data
```c
/**
 * @brief Handle video data receive complete callback
 * @details When socket monitoring receives data and it is video data, this callback function will be called
 * @param data Pointer to received video data
 * @param length Length of received video data
 * @param type Transport protocol type, refer to: `video_types.h`
 * @return uint32_t Processing result
 */
uint32_t video_data_receive_complete(uint8_t *data, uint32_t length, video_send_type_t type);
```

#### 5.3.3 Navigation Complete Frame Data Management

Navigation complete frame data management is implemented by the `media_frame_queue_` series functions, please initialize the frame queue before using.

#### 5.3.4 Close Navigation Service

In cases where network disconnection is not a concern, closing the navigation service requires the following steps:

1. Close the navigation data receiving thread
2. Close the JPEG decoding manager
3. Release the navigation complete frame data queue

##### 5.3.4.1 Release Navigation Receiving Thread
```c
/**
 * @brief Close video data processing thread
 * @details Close video data processing thread, release related resources
 * @return avdk_err_t Closing result
 */
avdk_err_t video_data_process_close(void);
```

##### 5.3.4.2 Close Decoding Thread
```c
/**
 * @brief Close JPEG decoding manager
 * @details Close JPEG decoding manager, release decoder instance resources
 * @return avdk_err_t Closing result
 */
avdk_err_t av_server_jpeg_decode_manager_turn_off(void);
```

##### 5.3.4.3 Release Navigation Complete Frame Data Queue

Release navigation complete frame data queue is implemented by the `media_frame_queue_deinit` function.

## 6 Notes

1. Ensure the phone and device are on the same WiFi network segment
2. Device IP address can be viewed through serial commands
3. LVGL interface display will be paused during image transmission
4. After transmission, you can resume LVGL display with the command: `ap_cmd test switch lvgl`
5. When using a UVC camera, ensure correct hardware connection and sufficient power supply
6. Storage function requires SD card support, and the SD card needs to be properly formatted
7. Ensure the phone and device are on the same WiFi network segment
8. default BK7258 uses GPIO_28 to control USB LDO, pull high to power on, note GPIO conflict issues

## 7 System Architecture

The project adopts a modular design, mainly including the following modules:

1. **WiFi Module**: Responsible for network connection and data transmission
2. **Media Processing Module**: Handles image acquisition, encoding, storage, and WiFi image transmission decoding
3. **Display Module**: Manages LCD display and LVGL interface
4. **Bluetooth Module**: Provides Bluetooth communication functions

Each module interacts through clear API interfaces, ensuring system maintainability and scalability.



## 7 Important Processes

Key processes in this project

### 7.1 Bluetooth
#### 8.1.1 A2DP/AVRCP Initialization
Initialize A2DP in a2dp_sink_demo_init:

```c
// Register callback with bt manager, mainly for handling connect/disconnect events
btm_callback_s btm_cb =
{
   .gap_cb = gap_event_cb,
   .start_connect_cb = bk_bt_a2dp_connect,
   .start_disconnect_cb = bk_bt_a2dp_disconnect,
   .stop_connect_cb = bk_bt_a2dp_stop_connect,
};
bt_manager_register_callback(&btm_cb);

// Initialize AVRCP controller
bk_bt_avrcp_ct_init();
bk_bt_avrcp_ct_register_callback(bk_bt_app_avrcp_ct_cb);

// Initialize AVRCP target
bk_bt_avrcp_tg_init();
bk_bt_avrcp_tg_register_callback(avrcp_tg_cb);

// Get SDK AVRCP target supported capabilities
bk_avrcp_rn_evt_cap_mask_t tmp_cap = {0};
bk_bt_avrcp_tg_get_rn_evt_cap(BK_AVRCP_RN_CAP_API_METHOD_ALLOWED, &tmp_cap);

// Add user-required capabilities, here adding volume change
bk_avrcp_rn_evt_cap_mask_t final_cap =
{
   .bits = 0
   | (1 << BK_AVRCP_RN_VOLUME_CHANGE)
   ,
};
final_cap.bits &= tmp_cap.bits;
bk_bt_avrcp_tg_set_rn_evt_cap(&final_cap);

// Register A2DP sink callback
bk_bt_a2dp_register_callback(bk_bt_app_a2dp_sink_cb);

// Enable A2DP sink, AAC not enabled here
bk_bt_a2dp_sink_init(aac_supported);

// Set A2DP capabilities (SBC, mono, dual channel, stereo, joint stereo)
bk_a2dp_codec_cap_t cap =
{
   .type = BK_A2DP_CODEC_TYPE_SBC,
   .param.sbc_codec_cap.channel_mode =
            BK_A2DP_SBC_CHANNEL_MODE_MONO
            | BK_A2DP_SBC_CHANNEL_MODE_DUAL
            | BK_A2DP_SBC_CHANNEL_MODE_STEREO
            | BK_A2DP_SBC_CHANNEL_MODE_JOINT_STEREO
            ,
   .param.sbc_codec_cap.bit_pool_max = 35,
};

ret = bk_bt_a2dp_set_cap(1, &cap);

// Register A2DP data callback
bk_bt_a2dp_sink_register_data_callback(&bt_audio_sink_media_data_ind);
```

### 8.1.2 A2DP Callback Processing
Process in previously registered bk_bt_app_a2dp_sink_cb:

```c
switch (event)
{
case BK_A2DP_PROF_STATE_EVT:
{
   // A2DP sink initialization completed, generally need to set semaphore here for synchronous operation
}
break;

case BK_A2DP_CONNECTION_STATE_EVT:
{
   uint8_t *bda = a2dp->conn_state.remote_bda;

   // A2DP sink connection status report
   // Disconnected
   if (BK_A2DP_CONNECTION_STATE_DISCONNECTED == a2dp->conn_state.state)
   {
      s_bt_env.a2dp_state = 0;
      if (BK_A2DP_AUDIO_STATE_STARTED == s_audio_state)
      {
            s_audio_state = BK_A2DP_AUDIO_STATE_SUSPEND;
            bt_audio_a2dp_sink_suspend_ind();
      }
      if(s_a2dp_connect_sema)
      {
            rtos_set_semaphore(&s_a2dp_connect_sema);
      }
   }
   // Connected
   else if (BK_A2DP_CONNECTION_STATE_CONNECTED == a2dp->conn_state.state)
   {
      bt_manager_set_connect_state(BT_STATE_PROFILE_CONNECTED);
      s_bt_env.a2dp_state = 1;
      if (0 == s_bt_env.avrcp_state)
      {
            // Some phones don't connect AVRCP automatically, manually connect here
            bk_bt_start_avrcp_connect();
      }
   }
}
break;

case BK_A2DP_AUDIO_STATE_EVT:
{
   // AVDTP audio status report
   // Start
   if (BK_A2DP_AUDIO_STATE_STARTED == a2dp->audio_state.state)
   {
      s_audio_state = a2dp->audio_state.state;
      // Send start message to bt_audio_sink_demo_main to prepare for data reception
      bt_audio_a2dp_sink_start_ind(&bt_audio_a2dp_sink_codec);
   }
   // Suspend
   else if ((BK_A2DP_AUDIO_STATE_SUSPEND == a2dp->audio_state.state) && (BK_A2DP_AUDIO_STATE_STARTED == s_audio_state))
   {
      s_audio_state = a2dp->audio_state.state;
      // Send suspend message to bt_audio_sink_demo_main to prepare for data reception
      bt_audio_a2dp_sink_suspend_ind();
   }
}
break;

case BK_A2DP_AUDIO_CFG_EVT:
{
   // Final negotiated A2DP audio parameters, used during decoding
   bt_audio_a2dp_sink_codec = a2dp->audio_cfg.mcc;
}
break;

case BK_A2DP_L2CAP_CONNECT_REQ_EVT:
{
   struct a2dp_l2cap_connect_req_param *param = (typeof(param))p_param;
   // Whether to automatically accept A2DP L2CAP connection? Default is automatic acceptance
   param->accept = s_auto_accept_connect_req;

   if(!param->accept)
   {
      bt_manager_set_connect_state(BT_STATE_PROFILE_CONNECTED);
   }
}
break;

case BK_A2DP_SET_CAP_COMPLETED_EVT:
{
   struct a2dp_set_cap_completed_param *param = (typeof(param))p_param;
   // A2DP capability setting successful, need to set semaphore

   if (s_bt_api_event_cb_sema)
   {
      rtos_set_semaphore( &s_bt_api_event_cb_sema );
   }
}
break;

default:
   break;
}
```

### 8.1.3 A2DP Audio Data Callback Processing
Don't process audio data directly in bt_audio_sink_media_data_ind, but send message to bt_audio_sink_demo_main for processing:

```c
static void bt_audio_sink_media_data_ind(const uint8_t *data, uint16_t data_len)
{
   // Send message
   demo_msg.type = BT_AUDIO_D2DP_DATA_IND_MSG;
   rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);
}
```

In bt_audio_sink_demo_main

```c
static void bt_audio_sink_demo_main(void *arg)
{
   while (1)
   {
      bk_err_t err;
      bt_audio_sink_demo_msg_t msg;

      err = rtos_pop_from_queue(&bt_audio_sink_demo_msg_que, &msg, BEKEN_WAIT_FOREVER);

      if (kNoErr == err)
      {
         switch (msg.type)
         {
         case BT_AUDIO_D2DP_START_MSG:
         {
            bk_a2dp_mcc_t *p_codec_info = (bk_a2dp_mcc_t *)msg.data;
            if (CODEC_AUDIO_SBC == p_codec_info->type)
            {
               // Event sent by previous BK_A2DP_AUDIO_STATE_EVT (contains SBC encoding information)
               uint8_t chnl_mode = p_codec_info->cie.sbc_codec.channel_mode;
               uint8_t chnls = p_codec_info->cie.sbc_codec.channels;
               uint8_t subbands = p_codec_info->cie.sbc_codec.subbands;
               uint8_t blocks = p_codec_info->cie.sbc_codec.block_len;
               uint8_t bitpool = p_codec_info->cie.sbc_codec.bit_pool;

               // Calculate frame length based on parameters (calculation formula refers to A2DP spec)
               if(chnl_mode == A2DP_SBC_CHANNEL_MONO || chnl_mode == A2DP_SBC_CHANNEL_DUAL)
               {
                  frame_length = 4 + ((4 * subbands * chnls)>>3) + ((blocks*chnls*bitpool+7)>>3);
               }
               else if(chnl_mode == A2DP_SBC_CHANNEL_STEREO)
               {
                  frame_length = 4 + ((4 * subbands * chnls)>>3) + ((blocks*bitpool+7)>>3);
               }
               else //A2DP_SBC_CHANNEL_JOINT_STEREO
               {
                  frame_length = 4 + ((4 * subbands * chnls)>>3) + ((subbands+blocks*bitpool+7)>>3);
               }

               org_frame_len = frame_length;
               frame_length *= 2;
            }
         }
         break;

         case BT_AUDIO_D2DP_DATA_IND_MSG:
         {
            if (CODEC_AUDIO_SBC == bt_audio_a2dp_sink_codec.type)
            {
               // Get AVDTP header (contains frame count)
               uint8_t payload_header = *fb++;
               uint8_t frame_num = payload_header & 0xF;

               for(uint8_t i = 0; i < frame_num; i++)
               {
                  if (blue_audio_player_get_free_frame_num(gl_audio_player_handle))
                  {
                     // Calculate each frame length based on frame count, and write to blue audio player
                     uint16_t tmp_len = (msg.len - 1) / frame_num;

                     blue_audio_player_write_frame_data(gl_audio_player_handle, (char *)fb, tmp_len);
                     fb += tmp_len;
                  }
                  else
                  {
                     LOGW("%s blue_audio_player frame nodes buffer(sbc) is full \n", __func__);
                     break;
                  }
               }

               // If (msg.len - 1 != org_frame_len * frame_num) is false, it means the frame count from peer doesn't match actual count
               // Need real-time parsing, refer to corresponding code processing
            }
         }
         break;
         ...
         }
      }
   }
}
```

### 8.1.4 avrcp Callback Processing
avrcp ct

```c
static void bk_bt_app_avrcp_ct_cb(bk_avrcp_ct_cb_event_t event, bk_avrcp_ct_cb_param_t *param)
{
   bk_avrcp_ct_cb_param_t *avrcp = (bk_avrcp_ct_cb_param_t *)(param);

   switch (event)
   {
      case BK_AVRCP_CT_CONNECTION_STATE_EVT:
      {
         //get peer cap if connection completed
         bk_bt_avrcp_ct_send_get_rn_capabilities_cmd(bda);
      }
      break;

      case BK_AVRCP_CT_GET_RN_CAPABILITIES_RSP_EVT:
      {
         //register cap refer by peer
         if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_PLAY_STATUS_CHANGE))
         {
            //register play status
            bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_PLAY_STATUS_CHANGE, 0);
         }
      }
      break;

      case BK_AVRCP_CT_CHANGE_NOTIFY_EVT:
      {
         //This event is received upon a change in the peer's state. The AVRCP protocol requires re-registration upon receiving it.
         bt_av_notify_evt_handler(avrcp->change_ntf.event_id, &avrcp->change_ntf.event_parameter);
      }
      break;
   }
}
```

avrcp tg

```c
static void avrcp_tg_cb(bk_avrcp_tg_cb_event_t event, bk_avrcp_tg_cb_param_t *param)
{
   switch (event)
   {
   case BK_AVRCP_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
   {
      //set abs vol by peer
      //Note that for volume setting, the headset is always the TG (Target), and the phone is always the CT (Controller).
      s_a2dp_vol = param->set_abs_vol.volume;

      bk_bt_dac_set_gain(s_a2dp_vol);
      if (s_a2dp_vol)
      {
         //sys_hal_aud_dacmute_en(0); //TODO
      }
      else
      {
         //sys_hal_aud_dacmute_en(1); //TODO
      }
      bluetooth_storage_save_volume(bt_manager_get_connected_device(), s_a2dp_vol);
   }
   break;

   case BK_AVRCP_TG_REGISTER_NOTIFICATION_EVT:
   {
      //peer register local cap, only BK_AVRCP_RN_VOLUME_CHANGE now.
      //Note that for volume setting, the headset is always the TG (Target), and the phone is always the CT (Controller).

      bk_avrcp_rn_param_t cmd;

      os_memset(&cmd, 0, sizeof(cmd));

      switch (param->reg_ntf.event_id)
      {
      case BK_AVRCP_RN_VOLUME_CHANGE:
         cmd.volume = s_a2dp_vol;
         break;

      default:
         LOGW("%s unknow reg event 0x%x\n", __func__, param->reg_ntf.event_id);
         goto error;
      }

      ret = bk_bt_avrcp_tg_send_rn_rsp(avrcp_remote_bda, param->reg_ntf.event_id, BK_AVRCP_RN_RSP_INTERIM, &cmd);

      if (ret)
      {
         LOGE("%s send rn rsp err %d\n", __func__, ret);
      }
   }
   break;
   }
}
```

### 8.1.5 avrcp active control
call bk_bt_app_avrcp_ct_xxx

### 8.1.6 hfp init

```c
int hfp_hf_demo_init(uint8_t msbc_supported)
{
   //startup task
   bt_audio_hf_demo_task_init();

   //register callback
   ret = bk_bt_hf_client_register_callback(bk_bt_app_hfp_client_cb);
   if (ret)
   {
      LOGI("%s bk_bt_hf_client_register_callback err %d\n", __func__, ret);
      return -1;
   }

   ret = bk_bt_hf_client_init(msbc_supported);
   if (ret)
   {
      LOGI("%s bk_bt_hf_client_init err %d\n", __func__, ret);
      return -1;
   }
   //register data callback
   ret = bk_bt_hf_client_register_data_callback(bt_audio_hfp_client_voice_data_ind);
   if (ret)
   {
      LOGI("%s bk_bt_hf_client_register_data_callback err %d\n", __func__, ret);
      return -1;
   }

    return ret;
}
```

### 8.1.7 hfp calllback processing

```c
static void bk_bt_app_hfp_client_cb(bk_hf_client_cb_event_t event, bk_hf_client_cb_param_t *param)
{
    switch (event)
    {
        case BK_HF_CLIENT_AUDIO_STATE_EVT:
        {
            //sco has established，send msg to create blue audio player

            if (BK_HF_CLIENT_AUDIO_STATE_DISCONNECTED == param->audio_state.state)
            {
               //destory
               bt_audio_hf_sco_disconnected();
            }
            else if (BK_HF_CLIENT_AUDIO_STATE_CONNECTED == param->audio_state.state)
            {
               //create
               bt_audio_hf_sco_connected();
            }

        }
        break;
        case BK_HF_CLIENT_CONNECTION_STATE_EVT:
        {
            if (param->conn_state.state == BK_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED)
            {
               //hfp profile connection success，get calling status first(currently)
               bk_bt_hf_client_query_current_calls(param->remote_bda);
               s_connect_status = 1;
            }
            else if (param->conn_state.state == BK_HF_CLIENT_CONNECTION_STATE_DISCONNECTED)
            {
               //disconnect
               ...
            }
        }
        break;
        case BK_HF_CLIENT_BVRA_EVT:
        {
            //voice assistor
            LOGI("+BRVA: HPF voice recognition activation status: %d \n", param->bvra.value);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_EVT:
        {
            //BK_HF_CLIENT_CIND_CALL_EVT and BK_HF_CLIENT_CIND_CALL_SETUP_EVT are two states for making a phone call.
            LOGI("+CIND: HFP call staus:%d \n", param->call.status);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_SETUP_EVT:
        {
            //BK_HF_CLIENT_CIND_CALL_EVT and BK_HF_CLIENT_CIND_CALL_SETUP_EVT are two states for making a phone call.
            LOGI("+CIND: HFP call_setup status:%d \n", param->call_setup.status);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_HELD_EVT:
        {
            //Call holding is used for handling call waiting and three-party calls.
            LOGI("+CIND: HFP call_hold status:%d \n", param->call_held.status);
        }
        break;
        case BK_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        {
            //Mobile network operator status
            LOGI("+CIND: HFP service availability ind: %d\n", param->service_availability.status);
        }
        break;
        case BK_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        {
            //Mobile network operator signal
            LOGI("+CIND: HFP signal strength ind: %d\n", param->signal_strength.value);
        }
        break;
        case BK_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        {
            //Mobile roaming status
            LOGI("+CIND: HFP roming status:%d \n", param->roaming.status);
        }
        break;
        case BK_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        {
            //Mobile battery level
            LOGI("+CIND: HFP battery ind:%d \n", param->battery_level.value);
        }
        break;
        case BK_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        {
            //Mobile network operator name
            LOGI("+COPS: HFP network operator name:%s \n", param->cops.name);
        }
        break;
        case BK_HF_CLIENT_BTRH_EVT:
        {
            //Hold status used during three-party calls
            LOGI("+BTRH: HFP Hold status: %d \n", param->btrh.status);
        }
        break;
        case BK_HF_CLIENT_CLIP_EVT:
        {
            //The mobile phone actively reports the current phone number and contact name (some mobile phones do not support this)
            LOGI("+CLIP: HFP calling line number: %s, name:%s \n", param->clip.number, param->clip.name);
        }
        break;
        break;
        case BK_HF_CLIENT_CCWA_EVT:
        {
            //The mobile phone actively reports the current call-waiting phone number and contact name (some mobile phones do not support this).
            LOGI("+CCWA: HFP calling waiting number:%s, name: %s\n", param->ccwa.number, param->ccwa.name);
        }
        break;
        case BK_HF_CLIENT_CLCC_EVT:
        {
            //When queried locally, the mobile phone reports the current phone number and contact name (this is generally supported by most mobile phones).
            LOGI("+CLCC: HFP calls result dir:%d, idx:%d, mpty:%d, number:%s, status:%d \n", param->clcc.dir, param->clcc.idx, param->clcc.mpty, param->clcc.number, param->clcc.status);
        }
        break;
        case BK_HF_CLIENT_VOLUME_CONTROL_EVT:
        {
            //Mobile control headset vol
            if (param->volume_control.type == BK_HF_VOLUME_CONTROL_TARGET_SPK)
            {
                LOGI("+VGS: HPF Speaker gain: %d \n", param->volume_control.volume);
                s_hfp_ctx.spk_vol = param->volume_control.volume;
                bluetooth_storage_save_hfp_volume(hfp_peer_addr, 1, s_hfp_ctx.spk_vol);
                bk_bt_dac_set_gain(NULL, &s_hfp_ctx.spk_vol);
            }
            else if (param->volume_control.type == BK_HF_VOLUME_CONTROL_TARGET_MIC)
            {
                LOGI("+VGM: HPF Microphone gain: %d \n", param->volume_control.volume);
                s_hfp_ctx.mic_vol = param->volume_control.volume;
                bluetooth_storage_save_hfp_volume(hfp_peer_addr, 0, s_hfp_ctx.mic_vol);
                bk_bt_dac_set_gain(&s_hfp_ctx.mic_vol, NULL);
            }
        }
        break;
        case BK_HF_CLIENT_AT_RESPONSE_EVT:
        {
            {
                //Every time a local request is sent, it is necessary to wait for the BK_HF_CLIENT_AT_RESPONSE_EVT status before sending the next one. Therefore, a state machine is used here to control the process.
                if(param->at_response.code == BK_HF_AT_RESPONSE_CODE_OK)
                {
                    LOGI("BK_HF_CLIENT_AT_RESPONSE_EVT ok, asso_cmd %d, status %d\n", param->at_response.asso_cmd, s_hfp_status_mach);
                }
                else
                {
                    LOGI("BK_HF_CLIENT_AT_RESPONSE_EVT normal err 0x%x, asso_cmd %d, status %d\n", param->at_response.code, param->at_response.asso_cmd, s_hfp_status_mach);
                }

                switch(s_hfp_status_mach)
                {
                case HFP_STATUS_WAIT_QUERY_CALL:
                    s_hfp_status_mach = HFP_STATUS_WAIT_VGS;
                    bk_bt_hf_client_volume_update(hfp_profile_peer_addr, BK_HF_VOLUME_CONTROL_TARGET_SPK, 7);
                    break;

                case HFP_STATUS_WAIT_VGS:
                    s_hfp_status_mach = HFP_STATUS_WAIT_VGM;
                    bk_bt_hf_client_volume_update(hfp_profile_peer_addr, BK_HF_VOLUME_CONTROL_TARGET_MIC, 7);
                    break;

                case HFP_STATUS_WAIT_VGM:
                    s_hfp_status_mach = HFP_STATUS_WAIT_CGMI;//HFP_STATUS_WAIT_DONE;
                    const char *at_cgmi = "AT+CGMI?";
                    hfp_demo_cust_cmd((uint8_t *)at_cgmi);
                    break;

                case HFP_STATUS_WAIT_CGMI:
                    s_hfp_status_mach = HFP_STATUS_WAIT_DONE;
                    LOGI("%s end op\n", __func__);
                    break;
                }
            }
        }
        break;

        case BK_HF_CLIENT_BSIR_EVT:
        {
            //mobile in band ring status
            LOGI("+BSIR: HFP In-band Ring tone staus: %d\n", param->bsir.state);
        }
        break;
        case BK_HF_CLIENT_RING_IND_EVT:
        {
            //Mobile ring now
            LOGI("RING HPF incoming call ind evt\n");
        }
        break;
        case BK_HF_CLIENT_UNKNOWN_DATA_IND_EVT:
        {
            //Peer response when send cust cmd. distinglish iphone here
            LOGI("unknown data received (len %d)\n", param->unknown_data.data_len);
            const char* CGMI_IPHONE = "Apple Inc";
            if (os_strstr(param->unknown_data.data, CGMI_IPHONE))
            {
                s_hfp_hf_is_iphone = 1;
                LOGI("iphone device\r\n");
            }
        }
        break;
        default:
            LOGW("Invalid HFP client event: %d\r\n", event);
            break;
    }
}
```

### 8.1.8 HFP Downstream Data Processing
Similar to A2DP audio data callback processing, but the data format is PCM.

### 8.1.9 HFP Upstream Data Processing

When a BT_AUDIO_VOICE_START_MSG is received, first start the Blue Audio Recorder, then call mic_task_init to launch the mic_task task. This task reads data (currently PCM) from the Blue Audio Recorder.

```c
static void mic_task(void *arg)
{
    int32_t ret = 0;

    LOGI("%s init success!! \r\n", __func__);

    while (hf_auido_start)
    {
        int32_t expect_len = 0;
        int32_t read_len = 0;
        int32_t read_all_len = 0;

        if(bt_audio_hfp_hf_codec == CODEC_VOICE_CVSD)
        {
            expect_len = SCO_CVSD_SAMPLES_PER_FRAME * 2;

        }
        else if(bt_audio_hfp_hf_codec == CODEC_VOICE_MSBC)
        {
            expect_len = 58;
        }

        if (hf_mic_data_count + expect_len > sizeof(hf_mic_sco_data))
        {
            LOGE("%s mic data buffer overflow\n", __func__);
            hf_mic_data_count = 0;
        }

        while(hf_auido_start && read_all_len < expect_len)
        {
            // Read a fixed number of bytes of data
            read_len = blue_audio_recorder_read_frame_data(s_bar_handle, (char *)(hf_mic_sco_data + hf_mic_data_count + read_all_len), expect_len - read_all_len);

            if(read_len <= 0)
            {
                LOGE("%s blue audio recorder read ret %d !!!\n", __func__, read_len);
                continue;
            }

            if(bt_audio_hfp_hf_codec == CODEC_VOICE_MSBC)
            {
                LOGI("read msbc len %d\n", read_len);
            }

            read_all_len += read_len;

            if(read_all_len < expect_len)
            {
                continue;
            }

            hf_mic_data_count += read_all_len;

            if(hf_mic_data_count)
            {
                LOGV("send sco %d\n", hf_mic_data_count);
               // Write to the Bluetooth SDK
                bk_bt_hf_client_voice_out_write(hfp_peer_addr, hf_mic_sco_data, hf_mic_data_count);
                hf_mic_data_count = 0;
            }

            break;
        }
    }

   ...
}

```

## 9 Configuration Instructions

The main configuration options of the project are located in the Kconfig file. Specific functions can be enabled or disabled by modifying the configuration:

- CONFIG_MEDIA_DEMO_MODE_TCP: Use TCP mode for media data transmission
- CONFIG_AVI_PLAYER: Enable AVI player function
- CONFIG_TP: Enable touch screen support
- CONFIG_MP3_PLAY_TEST: Enable MP3 playback test function
- CONFIG_BLUETOOTH_AUTO_ENABLE: Automatically enable Bluetooth function. If enabled, Bluetooth will be automatically started in bk_init. If disabled, this project will create a task in ap_main.c to start Bluetooth
- CONFIG_A2DP_SINK_DEMO: Enable Bluetooth A2DP sink
- CONFIG_HFP_HF_DEMO: Enable Bluetooth HFP hands-free
- CONFIG_BLUETOOTH_MULTI_CONTROLLER: Whether to enable second Bluetooth controller
- CONFIG_BLUETOOTH_HOST_ENABLE_H5: For external Bluetooth controller, whether to enable H5 verification for communication
- CONFIG_BLUETOOTH_BSC_UART_ID: UART ID used by local end for external Bluetooth controller
- CONFIG_BLUETOOTH_BSC_RESET_PIN: GPIO ID for reset pin control of external Bluetooth controller
- CONFIG_BLUETOOTH_BSC_CTS_PIN: Local end software flow control CTS GPIO for external Bluetooth controller
- CONFIG_BLUETOOTH_BSC_RTS_PIN: Local end software flow control RTS GPIO for external Bluetooth controller
- CONFIG_BLUETOOTH_BSC_INIT_BAUD: Initial baud rate for external Bluetooth controller
- CONFIG_BLUETOOTH_BSC_NEGO_BAUD: Negotiated baud rate for external Bluetooth controller, used for higher throughput

For detailed configuration instructions, please refer to the Kconfig file.

## 10 Troubleshooting

### 10.1 Common Issues and Solutions

1. **WiFi Connection Failure**

   - Check if SSID and password are correct
   - Ensure WiFi signal strength is sufficient
   - Try reinitializing the WiFi module

2. **Image Display Abnormality**

   - Check if LCD connection cables are correct
   - Confirm that the LCD model is consistent with code configuration
   - Try switching display modes and restoring

3. **Camera Not Recognized**

   - Check if UVC camera connection is correct
   - Ensure camera power supply is normal
   - Confirm if camera driver is loaded correctly

4. **Storage Function Abnormality**

   - Check if SD card is inserted correctly
   - Confirm if SD card format is supported
   - Check file system permission settings
