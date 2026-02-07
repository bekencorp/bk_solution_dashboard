# scooter_1024_600开发指南

* [English](./README.md)

## 1 项目概述

本项目是一个基于BK7258芯片和BK3515NS芯片的两轮车解决方案，实现了通过WiFi传输图像数据并在LCD屏幕上显示的功能。项目集成了丰富的多媒体处理能力、网络通信功能和用户界面显示，适用于智能出行设备的开发。

## 2 功能特性

### 2.1 WiFi通信
- 支持STA模式连接到现有WiFi网络
- 支持AP模式创建WiFi热点供其他设备连接
- 支持TCP/UDP协议传输图像数据

### 2.2 显示功能
- LCD屏幕显示
- LVGL图形库支持
- 支持显示切换（LVGL界面与wifi图传图像）
- AVI视频播放（可选）

### 2.3 蓝牙功能
- 蓝牙基础功能
- A2DP音频接收
- HFP免提通话
- BLE功能
- WiFi配网功能

## 3 快速开始

### 3.1 硬件信息
- 两轮车方案开发板V1.1 20250904（BK3515NS使用UART2与BK7258连接（开发板上从UART1开始标记））
- LCD屏幕：1024x600
- LCD转接板：v1.0 20250905(不兼容CAN功能)

.. note::
    两轮车方案开发板V1.1 20250904和V1.2 20251027的差异：
     - V1.2 20251027开发板上BK7258和BK3515NS的连接串口使用UART1；
     - V1.1 20250904开发板上BK7258和BK3515NS的连接串口使用UART2。

    LCD转接板V1.0 20250905和V1.1 20251114的差异：
     - V1.1 20251114兼容CAN功能，只能使用后缀名带V2的工程，V1.0 20250905不兼容CAN功能，只能使用后缀名不带V2的工程。

### 3.2 编译和烧录

编译流程参考 `Dashboard 解决方案 <../../index.html>`_

烧录流程参考 `烧录代码 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/get-started/index.html#id7>`_

编译生成的烧录bin文件路径：``projects/scooter/build/bk7258/scooter/package/all-app.bin``

BK3515NS参见：``https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/index.html#id6``

### 3.3 基本操作流程

详细操作流程请参考 `dashboard_app使用指南 <../dashboard_app/index.html>`_ 。

手机APP配置参数如下：

    +----------------+------------+------------+
    | 手机参数配置   | 蓝牙导航   | WIFI导航   |
    +================+============+============+
    | 图像宽度       | 1024       | 1024       |
    +----------------+------------+------------+
    | 图像高度       | 600        | 600        |
    +----------------+------------+------------+
    | 图像质量       | 0-100      | 0-100      |
    +----------------+------------+------------+
    | 旋转角度       | 0          | 0          |
    +----------------+------------+------------+
    | JPEG转换       | 打开或关闭 | 打开或关闭 |
    +----------------+------------+------------+


### 3.4 基本使用场景
1. 设备开机，显示lvgl界面
2. 连接到WiFi网络，显示wifi图传图像，同时将显示从lvgl切换到wifi图传，
3. 当做音响播放音乐，当做耳机接听拨打电话

## 4 命令行接口

项目提供了丰富的命令行接口，用于控制各种功能：

### 4.1 WiFi控制命令
```
// 连接到现有WiFi网络
ap_cmd test sta [ssid] [password]

// 创建WiFi热点
ap_cmd test ap [ssid] [password]
```

### 4.2 显示切换命令
```
// 切换到LVGL界面显示
ap_cmd test switch lvgl

// 切换到Wifi图传图像显示
ap_cmd test switch wifi
```

### 4.3 视频服务器控制命令
```
// 打开视频服务器
ap_cmd test video_server open

// 关闭视频服务器
ap_cmd test video_server close
```

### 4.4 蓝牙控制命令
- 必须和手机连接成功才能使用

```
// 主动连接对端设备
ap_cmd headset connect <xx:xx:xx:xx:xx:xx>

// 断开连接
ap_cmd headset disconnect <xx:xx:xx:xx:xx:xx>

// 播放
headset play

// 暂停
headset pause

// 下一曲
headset next

// 上一曲
headset prev

//快进(可指定按键时间，最终效果由手机决定)
headset fast_forward [msec]

//快退(可指定按键时间，最终效果由手机决定)
headset rewind [msec]

// 音量加
headset vol_up

// 音量减
headset vol_down

// 进入配对模式
headset pair_mode

```

## 5 API参考

### 5.1 帧缓冲区队列管理API

本方案中存在两种帧缓冲区队列：

1. frame_buffer_que：用于管理UVC摄像头输出的图像，和H264编码输出的图像；这里面UVC的图像会被解码后，经过H264编码成H264图像，最终可以存储到SD卡上
2. media_data_process_que：用于管理导航时wif收到的图像，这里的图像会被用来解码显示到LCD屏幕上

#### 5.1.1 frame_buffer_que

##### 5.1.1.1 frame_queue_init_all
```c
/**
 * @brief 初始化所有帧队列数据结构
 *
 * @return bk_err_t 初始化结果
 */
bk_err_t frame_queue_init_all(void);
```

##### 5.1.1.2 frame_queue_deinit_all
```c
/**
 * @brief 反初始化所有帧队列数据结构
 *
 * @return bk_err_t 反初始化结果
 */
bk_err_t frame_queue_deinit_all(void);
```

##### 5.1.1.3 frame_queue_malloc
```c
/**
 * @brief 分配帧缓冲区
 *
 * @param format 图像格式
 * @param size 请求的缓冲区大小（字节）
 * @return frame_buffer_t* 分配的帧缓冲区指针，失败返回NULL
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);
```

##### 5.1.1.4 frame_queue_get_frame
```c
/**
 * @brief 从就绪队列获取帧缓冲区
 *
 * @param format 图像格式
 * @param timeout 超时值（毫秒）
 * @return frame_buffer_t* 获取的帧缓冲区，超时或错误返回NULL
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);
```

##### 5.1.1.5 frame_queue_complete
```c
/**
 * @brief 将帧缓冲区返回就绪队列
 *
 * @param format 图像格式
 * @param frame 要返回队列的帧缓冲区
 * @return bk_err_t 操作结果
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);
```

##### 5.1.1.6 frame_queue_free
```c
/**
 * @brief 释放帧缓冲区并发送消息到空闲队列
 *
 * @param format 图像格式
 * @param frame 要释放的帧缓冲区
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);
```

#### 5.1.2 media_data_process_que

##### 5.1.2.1 media_frame_queue_init
```c
/**
 * @brief 初始化frame_queue数据结构
 *
 * 该函数根据指定的图像格式初始化frame_queue数据结构。
 * 它会根据图像格式设置队列的最大帧数量，并初始化队列的free_que和ready_que。
 * 初始化完成后，会返回初始化结果。
 *
 * @param format 图像格式
 * @return bk_err_t 初始化结果
 */
avdk_err_t media_frame_queue_init(image_format_t format);
```

##### 5.1.2.2 media_frame_queue_deinit
```c
/**
 * @brief 释放frame_queue数据结构
 *
 * @return avdk_err_t 释放结果
 */
avdk_err_t media_frame_queue_deinit(void);
```

##### 5.1.2.3 media_frame_queue_malloc
```c
/**
 * @brief 从frame_queue中申请一个frame_buffer
 *
 * @param size 申请的frame大小
 * @return frame_buffer_t* 申请到的frame_buffer，如果申请失败则返回NULL
 */
frame_buffer_t *media_frame_queue_malloc(uint32_t size);
```

##### 5.1.2.4 media_frame_queue_get_frame
```c
/**
 * @brief 从frame_queue的ready队列中获取一个frame_buffer
 *
 * @param timeout 超时时间（毫秒）
 * @return frame_buffer_t* 获取到的frame_buffer，如果获取失败则返回NULL
 */
frame_buffer_t *media_frame_queue_get_frame(uint32_t timeout);
```

##### 5.1.2.5 media_frame_queue_complete
```c
/**
 * @brief 将frame_buffer放回ready_que队列
 *
 * @param frame 要放回队列的frame_buffer
 * @return bk_err_t 操作结果
 */
bk_err_t media_frame_queue_complete(frame_buffer_t *frame);
```

##### 5.1.2.6 media_frame_queue_free
```c
/**
 * @brief 根据图像格式释放frame_buffer，并将消息发送到free_que
 *
 * @param frame 要释放的frame_buffer
 * @return void
 */
void media_frame_queue_free(frame_buffer_t *frame);
```

### 5.2 LVGL应用API

#### 5.2.1 lvgl_app_init
```c
/**
 * @brief 初始化LVGL应用
 * @details 配置LCD显示，初始化LVGL图形库，创建UI界面
 */
void lvgl_app_init(void);
```

#### 5.2.2 lvgl_app_deinit
```c
/**
 * @brief 反初始化LVGL应用
 * @details 关闭LCD背光，清理LVGL资源
 * @return 成功返回BK_OK，失败返回具体错误码
 */
bk_err_t lvgl_app_deinit(void);
```

#### 5.2.3 lvgl_app_suspend_display
```c
/**
 * @brief 暂停LVGL显示
 * @details 停止LVGL渲染，用于切换到其他显示模式
 * @return 成功返回BK_OK，失败返回具体错误码
 */
bk_err_t lvgl_app_suspend_display(void);
```

#### 5.2.4 lvgl_app_resume_display
```c
/**
 * @brief 恢复LVGL显示
 * @details 恢复LVGL渲染，用于从其他显示模式切换回LVGL界面
 * @return 成功返回BK_OK，失败返回具体错误码
 */
bk_err_t lvgl_app_resume_display(void);
```

### 5.3 导航应用API

针对流媒体来说导航的基本流程如下：

1. 启动导航服务，初始化导航完整数据队列
2. 接收导航数据
3. 解析导航数据，输出完整帧
4. 解码导航完整帧数据
5. LCD上显示导航信息
6. 结束导航服务

.. note::
    本解决方案是基于自定义的数据传输协议和导航数据格式的，用户可以自定义导航的数据格式和对应的解包流程

#### 5.3.1 启动导航服务

启动导航服务包含两个方面：

1. 初始化导航服务，准备接收导航数据，启动导航数据接收线程，监听导航数据端口
2. 初始化解码服务，准备读取导航完整帧数据，启动解码线程，将LVGL显示切换到导航显示

##### 5.3.1.1 初始化接收线程
```c
/**
 * @brief 初始化视频数据处理线程
 * @details 准备接收视频数据，启动视频数据处理线程
 * @param width 视频宽度
 * @param height 视频高度
 * @param format 视频格式
 * @return avdk_err_t 初始化结果
 */
avdk_err_t video_data_process_open(uint16_t width, uint16_t height, image_format_t format);
```

##### 5.3.1.2 初始化解码线程
```c
/**
 * @brief 启动JPEG解码管理器
 *
 * 此函数负责初始化JPEG解码管理器的配置参数，创建解码器实例，并设置LCD显示模块为解码器模式（从LVGL切换到导航显示）。
 * 函数会配置解码队列大小、线程优先级、栈大小、输出格式等参数，并注册解码完成回调函数。
 *
 * @return int 返回操作结果
 *         - BK_OK: 操作成功
 *         - 其他错误码: 操作失败
 */
avdk_err_t av_server_jpeg_decode_manager_turn_off(void);
```

#### 5.3.2 接收导航数据
```c
/**
 * @brief 处理视频数据接收完成回调
 * @details 当socket监控收到数据，且为视频数据时，会调用该回调函数
 * @param data 接收的视频数据指针
 * @param length 接收的视频数据长度
 * @param type 传输协议类型，参考： `video_types.h`
 * @return uint32_t 处理结果
 */
uint32_t video_data_receive_complete(uint8_t *data, uint32_t length, video_send_type_t type);
```

#### 5.3.3 导航完整帧数据管理

导航完整的帧数据管理是由上面 `media_frame_queue_` 系列函数实现的，在导航开始之前需要初始化导航完整帧数据队列。

#### 5.3.4 关闭导航

在不考虑网络连接是否要断开的情况下，关闭导航服务需要执行以下步骤：

1. 关闭导航数据接收线程
2. 关闭JPEG解码管理器
3. 释放导航完整帧数据队列

##### 5.3.4.1 释放导航接收线程

```c
/**
 * @brief 关闭视频数据处理线程
 * @details 关闭视频数据处理线程，释放相关资源
 * @return avdk_err_t 关闭结果
 */
avdk_err_t video_data_process_close(void);
```

##### 5.3.4.2 关闭解码线程
```c
/**
 * @brief 关闭JPEG解码管理器
 *
 * 此函数负责关闭JPEG解码管理器，释放解码器实例占用的资源。
 * 函数会停止解码线程，取消注册的解码完成回调函数，并将LCD显示模块切换回LVGL模式。
 *
 * @return int 返回操作结果
 *         - BK_OK: 操作成功
 *         - 其他错误码: 操作失败
 */
avdk_err_t av_server_jpeg_decode_manager_turn_off(void);
```

##### 5.3.4.3 释放导航完整帧数据队列

释放导航完整帧数据队列是由上面 `media_frame_queue_deinit` 函数实现的

## 6 注意事项

1. 确保手机与设备在同一WiFi网段
2. 设备IP地址可通过串口命令查看
3. 切到导航图像时会暂停LVGL界面显示
4. 结束后可通过命令恢复LVGL显示: `ap_cmd test switch lvgl`
5. 使用UVC摄像头时，确保正确连接硬件并提供足够的电源
6. 存储功能需要SD卡支持
7. 修改WiFi名称和密码时，确保符合WiFi规范
8. BK7258默认使用GPIO_28控制USB的LDO，拉高上电，注意GPIO冲突问题

## 7 系统架构

项目采用模块化设计，主要包含以下模块：

1. **WiFi模块**：负责网络连接和数据传输
2. **媒体处理模块**：处理图像采集、编码和存储
3. **显示模块**：管理LCD显示和LVGL界面
4. **蓝牙模块**：提供蓝牙通信功能

各模块之间通过明确的API接口进行交互，保证了系统的可维护性和扩展性。

## 8 重要流程

本工程重要流程

### 8.1 蓝牙
### 8.1.1 a2dp/avrcp 初始化
在a2dp_sink_demo_init中对a2dp进行初始化：

```c
   //向bt manager注册回调，主要用于connect/disconnect evt的处理
   btm_callback_s btm_cb =
   {
      .gap_cb = gap_event_cb,
      .start_connect_cb = bk_bt_a2dp_connect,
      .start_disconnect_cb = bk_bt_a2dp_disconnect,
      .stop_connect_cb = bk_bt_a2dp_stop_connect,
   };
   bt_manager_register_callback(&btm_cb);

   //初始化avrcp ct
   bk_bt_avrcp_ct_init();
   bk_bt_avrcp_ct_register_callback(bk_bt_app_avrcp_ct_cb);

   //初始化avrcp tg
   bk_bt_avrcp_tg_init();
   bk_bt_avrcp_tg_register_callback(avrcp_tg_cb);

   //获取sdk avrcp tg支持的cap
   bk_avrcp_rn_evt_cap_mask_t tmp_cap = {0};
   bk_bt_avrcp_tg_get_rn_evt_cap(BK_AVRCP_RN_CAP_API_METHOD_ALLOWED, &tmp_cap);

   //添加用户需要的cap，这里添加了vol change
   bk_avrcp_rn_evt_cap_mask_t final_cap =
   {
      .bits = 0
      | (1 << BK_AVRCP_RN_VOLUME_CHANGE)
      ,
   };
   final_cap.bits &= tmp_cap.bits;
   bk_bt_avrcp_tg_set_rn_evt_cap(&final_cap);

   //注册a2dp sink回调
   bk_bt_a2dp_register_callback(bk_bt_app_a2dp_sink_cb);

   //开启a2dp sink，这里没有启用aac
   bk_bt_a2dp_sink_init(aac_supported);

   //设置a2dp cap(SBC，单声道，双声道，立体声，联合立体声)
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

   //注册a2dp data回调
   bk_bt_a2dp_sink_register_data_callback(&bt_audio_sink_media_data_ind);
```

### 8.1.2 a2dp回调处理
在先前注册的bk_bt_app_a2dp_sink_cb里处理：

```c

   switch (event)
   {
   case BK_A2DP_PROF_STATE_EVT:
   {
      //a2dp sink初始化完成，一般要在这里set信号量，这样a2dp_sink_demo_init可以同步操作
   }
   break;

   case BK_A2DP_CONNECTION_STATE_EVT:
   {
      uint8_t *bda = a2dp->conn_state.remote_bda;

      //a2dp sink连接状态上报
      //断连
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
      //连接
      else if (BK_A2DP_CONNECTION_STATE_CONNECTED == a2dp->conn_state.state)
      {
         bt_manager_set_connect_state(BT_STATE_PROFILE_CONNECTED);
         s_bt_env.a2dp_state = 1;
         if (0 == s_bt_env.avrcp_state)
         {
               //某些手机不会连接avrcp，此处手动连接
               bk_bt_start_avrcp_connect();
         }
      }
   }
   break;

   case BK_A2DP_AUDIO_STATE_EVT:
   {
      //avdtp音频状态上报
      //start
      if (BK_A2DP_AUDIO_STATE_STARTED == a2dp->audio_state.state)
      {
         s_audio_state = a2dp->audio_state.state;
         //发送start消息给bt_audio_sink_demo_main，让其准备接收数据
         bt_audio_a2dp_sink_start_ind(&bt_audio_a2dp_sink_codec);
      }
      //suspend
      else if ((BK_A2DP_AUDIO_STATE_SUSPEND == a2dp->audio_state.state) && (BK_A2DP_AUDIO_STATE_STARTED == s_audio_state))
      {
         s_audio_state = a2dp->audio_state.state;
         //发送suspend消息给bt_audio_sink_demo_main，让其准备接收数据
         bt_audio_a2dp_sink_suspend_ind();
      }
   }
   break;

   case BK_A2DP_AUDIO_CFG_EVT:
   {
      //最终协商的a2dp 音频参数，解码时要用
      bt_audio_a2dp_sink_codec = a2dp->audio_cfg.mcc;
   }
   break;

   case BK_A2DP_L2CAP_CONNECT_REQ_EVT:
   {
      struct a2dp_l2cap_connect_req_param *param = (typeof(param))p_param;
      //是否自动接受a2dp l2cap的连接？默认是自动接受
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
      //设置a2dp cap成功，需要设置信号量

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

### 8.1.3 a2dp音频数据回调处理
在bt_audio_sink_media_data_ind里不直接处理音频数据，而是发送消息至bt_audio_sink_demo_main处理

```c
static void bt_audio_sink_media_data_ind(const uint8_t *data, uint16_t data_len)
{
   //发送消息

   demo_msg.type = BT_AUDIO_D2DP_DATA_IND_MSG;
   rc = rtos_push_to_queue(&bt_audio_sink_demo_msg_que, &demo_msg, BEKEN_NO_WAIT);
}
```

在bt_audio_sink_demo_main里(以SBC为例):

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
               //由之前的BK_A2DP_AUDIO_STATE_EVT发送的evt（包含sbc编码信息）
               uint8_t chnl_mode = p_codec_info->cie.sbc_codec.channel_mode;
               uint8_t chnls = p_codec_info->cie.sbc_codec.channels;
               uint8_t subbands = p_codec_info->cie.sbc_codec.subbands;
               uint8_t blocks = p_codec_info->cie.sbc_codec.block_len;
               uint8_t bitpool = p_codec_info->cie.sbc_codec.bit_pool;

               //根据参数计算每个frame的长度（计算公式参见a2dp spec）
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
               //获取avdtp的头（包含frame个数）
               uint8_t payload_header = *fb++;
               uint8_t frame_num = payload_header & 0xF;

               for(uint8_t i = 0; i < frame_num; i++)
               {
                  if (blue_audio_player_get_free_frame_num(gl_audio_player_handle))
                  {
                     //根据frame count计算每个frame长度，并写入blue audio player中
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

               //如果(msg.len - 1 != org_frame_len * frame_num)为否，说明对端传入的frame个数与实际个数不相等
               //需要实时解析，参见代码对应处理
            }
         }
         break;
         ...
         }
      }
   }
}
```

### 8.1.4 avrcp回调处理
avrcp ct

```c
static void bk_bt_app_avrcp_ct_cb(bk_avrcp_ct_cb_event_t event, bk_avrcp_ct_cb_param_t *param)
{
   bk_avrcp_ct_cb_param_t *avrcp = (bk_avrcp_ct_cb_param_t *)(param);

   switch (event)
   {
      case BK_AVRCP_CT_CONNECTION_STATE_EVT:
      {
         //如果连接成功，获取对端的cap
         bk_bt_avrcp_ct_send_get_rn_capabilities_cmd(bda);
      }
      break;

      case BK_AVRCP_CT_GET_RN_CAPABILITIES_RSP_EVT:
      {
         //根据对端CAP，注册CAP
         if (avrcp->get_rn_caps_rsp.evt_set.bits & (0x01 << BK_AVRCP_RN_PLAY_STATUS_CHANGE))
         {
            //注册播放状态变化
            bk_bt_avrcp_ct_send_register_notification_cmd(avrcp_remote_bda, BK_AVRCP_RN_PLAY_STATUS_CHANGE, 0);
         }
      }
      break;

      case BK_AVRCP_CT_CHANGE_NOTIFY_EVT:
      {
         //当对端状态改变时会收到这个evt，根据avrcp协议规定，需要再次注册
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
      //对端设置绝对音量
      //注意，对于设置音量来讲，headset永远是tg，手机是ct
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
      //对端注册本地cap，目前只有BK_AVRCP_RN_VOLUME_CHANGE
      //注意，对于设置音量来讲，headset永远是tg，手机是ct

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

### 8.1.5 avrcp主动控制
调用bk_bt_app_avrcp_ct_xxx

### 8.1.6 hfp初始化

```c
int hfp_hf_demo_init(uint8_t msbc_supported)
{
   //开启task
   bt_audio_hf_demo_task_init();

   //注册回调
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
   //注册数据回调
   ret = bk_bt_hf_client_register_data_callback(bt_audio_hfp_client_voice_data_ind);
   if (ret)
   {
      LOGI("%s bk_bt_hf_client_register_data_callback err %d\n", __func__, ret);
      return -1;
   }

    return ret;
}
```

### 8.1.7 hfp回调处理

```c
static void bk_bt_app_hfp_client_cb(bk_hf_client_cb_event_t event, bk_hf_client_cb_param_t *param)
{
    switch (event)
    {
        case BK_HF_CLIENT_AUDIO_STATE_EVT:
        {
            //sco链路已建立，发送消息创建blue audio player

            if (BK_HF_CLIENT_AUDIO_STATE_DISCONNECTED == param->audio_state.state)
            {
               //销毁
               bt_audio_hf_sco_disconnected();
            }
            else if (BK_HF_CLIENT_AUDIO_STATE_CONNECTED == param->audio_state.state)
            {
               //创建
               bt_audio_hf_sco_connected();
            }

        }
        break;
        case BK_HF_CLIENT_CONNECTION_STATE_EVT:
        {
            if (param->conn_state.state == BK_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED)
            {
               //hfp profile连接成功，首先获取当前状态(当前拨打状态)
               bk_bt_hf_client_query_current_calls(param->remote_bda);
               s_connect_status = 1;
            }
            else if (param->conn_state.state == BK_HF_CLIENT_CONNECTION_STATE_DISCONNECTED)
            {
               //断连
               ...
            }
        }
        break;
        case BK_HF_CLIENT_BVRA_EVT:
        {
            //语音助手
            LOGI("+BRVA: HPF voice recognition activation status: %d \n", param->bvra.value);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_EVT:
        {
            //BK_HF_CLIENT_CIND_CALL_EVT BK_HF_CLIENT_CIND_CALL_SETUP_EVT为拨打电话的两种状态
            LOGI("+CIND: HFP call staus:%d \n", param->call.status);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_SETUP_EVT:
        {
            //BK_HF_CLIENT_CIND_CALL_EVT BK_HF_CLIENT_CIND_CALL_SETUP_EVT为拨打电话的两种状态
            LOGI("+CIND: HFP call_setup status:%d \n", param->call_setup.status);
        }
        break;
        case BK_HF_CLIENT_CIND_CALL_HELD_EVT:
        {
            //呼叫保持，三方时使用
            LOGI("+CIND: HFP call_hold status:%d \n", param->call_held.status);
        }
        break;
        case BK_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        {
            //手机运营商状态
            LOGI("+CIND: HFP service availability ind: %d\n", param->service_availability.status);
        }
        break;
        case BK_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        {
            //手机运营商信号
            LOGI("+CIND: HFP signal strength ind: %d\n", param->signal_strength.value);
        }
        break;
        case BK_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        {
            //手机漫游状态
            LOGI("+CIND: HFP roming status:%d \n", param->roaming.status);
        }
        break;
        case BK_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        {
            //手机电量
            LOGI("+CIND: HFP battery ind:%d \n", param->battery_level.value);
        }
        break;
        case BK_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        {
            //手机运营商名字
            LOGI("+COPS: HFP network operator name:%s \n", param->cops.name);
        }
        break;
        case BK_HF_CLIENT_BTRH_EVT:
        {
            //hold状态，三方时使用
            LOGI("+BTRH: HFP Hold status: %d \n", param->btrh.status);
        }
        break;
        case BK_HF_CLIENT_CLIP_EVT:
        {
            //手机主动上报当前电话号码和联系人名字（某些手机不支持）
            LOGI("+CLIP: HFP calling line number: %s, name:%s \n", param->clip.number, param->clip.name);
        }
        break;
        break;
        case BK_HF_CLIENT_CCWA_EVT:
        {
            //手机主动上报当前呼叫等待电话号码和联系人名字（某些手机不支持）
            LOGI("+CCWA: HFP calling waiting number:%s, name: %s\n", param->ccwa.number, param->ccwa.name);
        }
        break;
        case BK_HF_CLIENT_CLCC_EVT:
        {
            //本地主动查询时，手机上报当前电话号码和联系人名字（一般手机都支持)
            LOGI("+CLCC: HFP calls result dir:%d, idx:%d, mpty:%d, number:%s, status:%d \n", param->clcc.dir, param->clcc.idx, param->clcc.mpty, param->clcc.number, param->clcc.status);
        }
        break;
        case BK_HF_CLIENT_VOLUME_CONTROL_EVT:
        {
            //手机控制headset音量
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
                //每次本地发送请求时，都需要等待BK_HF_CLIENT_AT_RESPONSE_EVT状态才能发送下一个，所以这里用状态机控制
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
            //手机是否支持in band ring
            LOGI("+BSIR: HFP In-band Ring tone staus: %d\n", param->bsir.state);
        }
        break;
        case BK_HF_CLIENT_RING_IND_EVT:
        {
            //手机正在响铃
            LOGI("RING HPF incoming call ind evt\n");
        }
        break;
        case BK_HF_CLIENT_UNKNOWN_DATA_IND_EVT:
        {
            //发送CUST命令时，对端的回复，这里判断是否为IPHONE
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

### 8.1.8 hfp下行数据处理
同a2dp音频数据回调处理，只不过数据格式是pcm

### 8.1.9 hfp上行数据处理
当收到BT_AUDIO_VOICE_START_MSG时，先开启blue audio recorder，再调用mic_task_init，开启mic_task task，从blue audio recorder读取数据(当前是pcm)

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
            //读取固定字节的数据
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
               //写入蓝牙sdk
                bk_bt_hf_client_voice_out_write(hfp_peer_addr, hf_mic_sco_data, hf_mic_data_count);
                hf_mic_data_count = 0;
            }

            break;
        }
    }

   ...
}

```


## 9 配置说明

项目的主要配置选项位于Kconfig文件中，可以通过修改配置来启用或禁用特定功能：

- CONFIG_MEDIA_DEMO_MODE_TCP：使用TCP模式传输媒体数据
- CONFIG_AVI_PLAYER：启用AVI播放器功能
- CONFIG_TP：启用触摸屏支持
- CONFIG_MP3_PLAY_TEST：启用MP3播放测试功能
- CONFIG_BLUETOOTH_AUTO_ENABLE：自动启用蓝牙功能，开启后，会在bk_init自动开启蓝牙。如果关闭，本工程会在ap_main.c里创建task开启蓝牙
- CONFIG_A2DP_SINK_DEMO: 启用蓝牙a2dp sink
- CONFIG_HFP_HF_DEMO: 启用蓝牙hfp hf
- CONFIG_BLUETOOTH_MULTI_CONTROLLER: 是否开启第二蓝牙controller
- CONFIG_BLUETOOTH_HOST_ENABLE_H5: 外接第二蓝牙controller，是否通讯开启h5校验
- CONFIG_BLUETOOTH_BSC_UART_ID: 外接第二蓝牙controller，本端使用的串口id
- CONFIG_BLUETOOTH_BSC_RESET_PIN: 外接第二蓝牙controller，控制reset pin的gpio id
- CONFIG_BLUETOOTH_BSC_CTS_PIN: 外接第二蓝牙controller，本端软流控cts gpio
- CONFIG_BLUETOOTH_BSC_RTS_PIN: 外接第二蓝牙controller，本端软流控rts gpio
- CONFIG_BLUETOOTH_BSC_INIT_BAUD: 外接第二蓝牙controller，初始化时波特率
- CONFIG_BLUETOOTH_BSC_NEGO_BAUD: 外接第二蓝牙controller，协商后的波特率，用于加大吞吐

## 10 故障排除

### 10.1 常见问题及解决方案

1. **WiFi连接失败**

   - 检查SSID和密码是否正确
   - 确保WiFi信号强度足够
   - 尝试重新初始化WiFi模块

2. **图像显示异常**

   - 检查LCD连接线是否正确
   - 确认LCD型号与代码配置一致
   - 尝试切换显示模式并重新恢复

3. **摄像头无法识别**

   - 检查UVC摄像头连接是否正确
   - 确保摄像头电源供应正常
   - 确认摄像头驱动是否正确加载

4. **存储功能异常**

   - 检查SD卡是否正确插入
   - 确认SD卡格式是否支持
   - 检查文件系统权限设置
