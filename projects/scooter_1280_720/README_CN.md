# scooter_1280_720 开发指南

- [English](./README.md)

## 概述

`scooter_1280_720` 是两轮车方案（BK7259）的产品工程主入口，对应 1280x720 显示规格。
工程基于 BK7259（AP，主控）+ BK3515（蓝牙从控）双芯片架构，当前已落地
**MIPI DSI + DPU + LVGL 仪表显示、开机 AVI 播放、手机 JPEG 投屏、BLE / Wi-Fi 配网、
经典蓝牙音频与多控制器蓝牙** 等基础能力，是后续“前后双 UVC 双录产品”的开发底座。


## 目录结构

```
scooter_1280_720/
├── ap/                       # 主控（AP）应用
│   ├── ap_main.c             # 主入口：板级/显示/蓝牙/CLI 初始化编排
│   ├── boot_avi_play.c/.h    # 开机 AVI 播放
│   ├── config/bk7259_ap/     # AP 侧 defconfig 与 GPIO 配置
│   ├── beken_generated/      # UI 设计器生成代码（页面、字体、图片、宠物 GIF 页等）
│   ├── assets/               # 图片资源
│   └── Kconfig.projbuild     # 工程可配置项（显示分辨率、旋转、投屏裁剪等）
├── cp/                       # 协处理器（CP）应用与配置
├── partitions/bk7259/        # 分区表与 RAM region
├── Makefile / CMakeLists.txt # 构建入口
├── dbuild.sh / dbuild.ps1    # docker 构建脚本
└── bk7259_bsp.ld             # 链接脚本
```



## 编译

目标芯片为 `bk7259`，通过 docker 构建脚本编译：

```bash
cd projects/scooter_1280_720
./dbuild.sh make bk7259        # Linux / macOS
# Windows PowerShell:
# .\dbuild.ps1 make bk7259
```

- 构建脚本默认以 `../..`（仓库根目录）作为 `SDK_DIR`，必要时可通过环境变量 `SDK_DIR` 指定。
- 编译产物位于工程 `build/` 目录下，将对应固件烧录到 BK7259。



## 关键配置

工程的可配置项主要集中在 `ap/config/bk7259_ap/defconfig` 与 `ap/Kconfig.projbuild`：


| 配置                                             | 默认值        | 说明                      |
| ---------------------------------------------- | ---------- | ----------------------- |
| `CONFIG_LCD_HX8394F_MIPI_720x1280`             | y          | MIPI DSI 屏型号（720x1280）  |
| `CONFIG_SCOOTER_LVGL_HOR_RES` / `VER_RES`      | 720 / 1280 | LVGL framebuffer 分辨率    |
| `CONFIG_SCOOTER_UI_ROTATION`                   | 270        | UI 旋转角度（旋转为横屏 1280x720） |
| `CONFIG_SCOOTER_UI_CANVAS_WIDTH` / `HEIGHT`    | 1280 / 720 | UI 设计器画布尺寸              |
| `CONFIG_BLUETOOTH_MULTI_CONTROLLER`            | y          | 启用多控制器（BK3515 从控）       |
| `CONFIG_BK_BLE_PROVISIONING`                   | y          | 启用 BLE 配网               |
| `CONFIG_A2DP_SINK_DEMO` / `CONFIG_HFP_HF_DEMO` | y          | 经典蓝牙音频 / 免提通话           |
| `CONFIG_MEDIA_RECEIVE_DEMO`                    | y          | 手机投屏 / SDP 发现           |
| `CONFIG_FATFS_SDCARD` / `CONFIG_SDCARD`        | y          | SD 卡存储（开机媒体、录像）         |


BK3515 从控 UART 链路相关引脚（默认值，可在 defconfig 调整）：


| 配置                                                                | 默认值             |
| ----------------------------------------------------------------- | --------------- |
| `CONFIG_BLUETOOTH_BSC_UART_ID`                                    | 1               |
| `CONFIG_BLUETOOTH_BSC_RESET_PIN`                                  | 51              |
| `CONFIG_BLUETOOTH_BSC_CTS_PIN` / `RTS_PIN`                        | 28 / 29         |
| `CONFIG_BLUETOOTH_BSC_INIT_BAUD` / `NEGO_BAUD`                    | 115200 / 921600 |




## 启动流程

`ap/ap_main.c` 的 `main()` 按以下顺序初始化：

1. `bk_init()`：系统初始化；
2. `media_service_init()`：媒体服务初始化；
3. `app_board_init()`：使能 LDO、初始化 frame buffer（及可选 TP）；
4. `app_display_init()`：初始化显示硬件（DPU / MIPI DSI / LCD），播放开机 AVI，启动 LVGL，注册投屏恢复 hook；
5. `app_bt_init()`：创建 `ap_bt_startup_task` 独立线程，依次完成 MAC 同步、BK3515 BSC 初始化、
   `bk_bluetooth_init()`、媒体消息、SDP 发现、经典蓝牙 profile、BLE 配网业务初始化；
6. `app_cli_init()`：初始化 CLI 命令与按键服务。

开机后屏幕默认显示 LVGL 仪表 / 主页面（显示分辨率 1280x720）。

## 功能演示

> 以下各小节标注的“涉及源文件”路径均相对于仓库根目录。



### 开机动画

开机后在 `app_display_init()` 中、LVGL 启动之前调用 `boot_avi_play()`，从 SD 卡播放开机 AVI，
播放结束后再启动 LVGL 仪表页面。

涉及源文件：

- `projects/scooter_1280_720/ap/ap_main.c` — `app_display_init()` 中依次执行
  `display_ui_init_display_hw() → boot_avi_play() → display_ui_start_lvgl()`；
- `projects/scooter_1280_720/ap/boot_avi_play.c` — 开机动画主实现；
- `components/jpeg_stream_pipeline/src/jpeg_stream_pipeline.c` — JPEG 解码 / 缩放 / 旋转流水线。

处理流程：

1. `sdcard_mount()` 挂载 SD 卡，`f_stat()` 查找固定路径 1:/boot.avi（不存在则跳过开机动画）；
2. 通过 `modules/avilib.h` 的 `AVI_open_input_file()` 解析 AVI，逐帧读取 MJPEG 数据；
3. 帧兼容性修复：MJPEG 帧常缺少 DHT / QT1，硬件解码器（VCDEC）要求其存在，
   分别由 `jpeg_has_dht()` + 内置 `s_mjpeg_dht` 表与 `jpeg_inject_qt1_if_missing()` 补齐；
4. 当 AVI 为竖屏（`avi_w == dpu_h && avi_h == dpu_w`）时，流水线设置 `rotate_degree = 90` 由 GPU 旋转；
5. 解码帧经 `boot_frame_display_cb()` 调用 `bk_display_flush()` 上屏；播放期间 DPU 临时切到
   `ARGB8888 + decompress`（`dpu_switch_to_argb8888_decompress()`），结束后恢复（`dpu_restore_rgb565()`）。



### 手机投屏（JPEG）

工程已具备局域网 JPEG 投屏闭环：手机 App 通过 SDP 发现设备并建立连接，将 JPEG 视频流推送到本地解码上屏。

涉及源文件：

- `projects/scooter_1280_720/ap/ap_main.c` — `ap_bt_startup_task()` 中 `media_msg_init()` 与
  `ntwk_sdp_start("media-tcp", ...)` 启动媒体线程与 SDP 广播；`app_display_init()` 中
  `display_ui_register_cast_hooks_once()` 注册投屏显示 hook；
- `components/media/src/media_network_transfer.c` — CTRL / VIDEO / AUDIO 网络通道收发；
- `components/media/src/media_msg.c` — 媒体事件状态机 `media_message_handle()`；
- `components/media/include/media_msg.h` — `MEDIA_EVT_*` 事件定义；
- `components/media/src/media_devices.c` — `av_server_jpeg_decode_manager_turn_on/off()`；
- `components/cast_pipeline/cast_jpeg_pipeline.c` — 投屏 JPEG 解码显示管线；
- `components/display_ui/display_ui_cast_hooks.c` — 投屏时停 LVGL / 切 DPU / 恢复 LVGL 的显示 hook。

处理流程：

1. App 发现设备并建立 CTRL / VIDEO / AUDIO 通道（`media_network_transfer.c`）；
2. 控制通道连接后，`media_network_transfer.c` 投递 `MEDIA_EVT_REMOTE_DEVICE_CONNECTED`，
   `media_msg.c` 的状态机调用 `av_server_jpeg_decode_manager_turn_on()`
   （`media_devices.c`）→ `cast_jpeg_pipeline_turn_on()`（`cast_jpeg_pipeline.c`）启动解码显示链路；
3. 视频数据经 `media_network_transfer.c` 的 `cast_jpeg_pipeline_push_frame_buffer()`
   送入 `jpeg_stream_pipeline` 解码 / 旋转，再由 `bk_display_flush()` 上屏；
4. 断连时投递 `MEDIA_EVT_CAST_SESSION_TEARDOWN`，由 `media_cast_lan_session_teardown()`
   调用 `av_server_jpeg_decode_manager_turn_off()` → `cast_jpeg_pipeline_turn_off()`，
   关闭投屏管线、恢复 LVGL 并重启 SDP 广播。



### BLE 配网与导航控制

涉及源文件：

- `projects/scooter_1280_720/ap/ap_main.c` — `ap_bt_startup_task()` 调用 `bk_sl_np_init()` 启动配网业务；
- `components/network_provisioning/network_provisioning.c` — 配网与导航控制业务逻辑；
- `components/network_provisioning/network_provisioning.h` — opcode 与数据结构定义；
- `components/bluetooth_dual/wifi_boarding/wifi_boarding_demo_service.c` — BLE boarding GATT 服务与消息分发；
- `components/media/src/media_navigation_transfer.c` — 导航文件分片接收与上层推送。

能力说明：

- BLE 配网支持手机发现设备、STA / AP / P2P 配置、Wi-Fi 扫描结果分包上报；
- 复用 boarding 通道扩展导航投屏与文件传输，opcode 定义见 `network_provisioning.h`：

  | Opcode | 枚举                                    | 功能                   |
  | ------ | ------------------------------------- | -------------------- |
  | 50     | `BOARDING_OP_TRANSFER_FILE_CONTROL`   | 文件传输控制（开始 / 停止 / 完成） |
  | 51     | `BOARDING_OP_TRANSFER_FILE_DATA`      | 按 packet num 传输文件分片  |
  | 52     | `BOARDING_OP_NAVIGATION_CONTROL`      | 导航投屏启动 / 停止          |
  | 53     | `BOARDING_OP_NAVIGATION_TYPE_CONTROL` | 导航类型选择（BT / Wi-Fi）   |

- opcode 52 由 `network_provisioning.c` 的 `handle_navigation_control_msg()` 处理，START / STOP 分别调用
  `av_server_jpeg_decode_manager_turn_on()` / `..._turn_off()`，与手机投屏复用同一条 JPEG 解码显示链路；
- opcode 50/51 的文件传输由 `media_navigation_transfer_begin()` / `..._push()` / `..._cancel()` 承接
  （`media_navigation_transfer.c`）。



### 经典蓝牙音频

涉及源文件：

- `projects/scooter_1280_720/ap/ap_main.c` — `ap_bt_app_init()` 中调用 `a2dp_sink_demo_init(0, 1)`
  与 `hfp_hf_demo_init(0)` 完成 profile 初始化；
- `components/bluetooth_dual/a2dp_sink/a2dp_sink_demo.c` — A2DP Sink / AVRCP 实现；
- `components/bluetooth_dual/hfp_hf/hfp_hf_demo.c` — HFP HF 免提通话实现；
- `components/blue_audio_player_service/`、`components/blue_audio_recorder_service/` — 播放 / 录音服务。

能力说明：

- A2DP Sink 播放手机音频，AVRCP 提供音量 / 播放控制；A2DP 收到音频开始事件后按 codec（SBC / AAC）
  创建播放器服务，并将音频帧写入 `blue_audio_player_service`；
- HFP HF 免提通话（SCO，CVSD / mSBC），上行麦克风采集经 `blue_audio_recorder_service` 发送给手机，
  通话优先级高于 A2DP 背景音乐。



### 宠物 GIF

`projects/scooter_1280_720/ap/beken_generated/pet_page.c` 支持从 SD 卡加载宠物 GIF 页面。

## CLI 命令

工程注册了 `widgets` 命令（见 `cli_widgets_cmd`）：

```text
ap_cmd widgets np_erase [reboot]        # 擦除已保存的 BLE 配网信息（可选擦除后重启）
ap_cmd widgets np_start_advertise       # 重新开始 BLE 配网广播
```

