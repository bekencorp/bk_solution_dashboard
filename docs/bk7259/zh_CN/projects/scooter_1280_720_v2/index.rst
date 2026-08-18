scooter_1280_720_v2 开发指南
================================


:link_to_translation:`en:[English]`

概述
--------


``scooter_1280_720_v2`` 是两轮车智能仪表方案（BK7259）的 V2 产品工程，面向
1280x720 横屏显示。工程采用 BK7259（AP，主控）+ BK3515N（蓝牙从控）双芯片架构，
集成 MIPI DSI + DPU + LVGL 仪表显示、开机 AVI 播放、行车记录、手机 JPEG 投屏、
BLE / Wi-Fi 配网、经典蓝牙音频、联系人与拨号、本地音乐和 OTA 界面。

与 ``scooter_1280_720`` 基础工程相比，V2 工程增加了完整的行车记录、联系人、本地音乐、
OTA 页面以及对应的物理按键交互。

目录结构
------------


.. code-block:: text

   scooter_1280_720_v2/
   ├── ap/                         # 主控（AP）应用
   │   ├── ap_main.c               # 板级、显示、蓝牙和按键初始化入口
   │   ├── config/bk7259_ap/       # AP defconfig 与 GPIO 配置
   │   ├── beken_generated/        # UI 设计器生成的页面、字体和图片资源
   │   ├── dashcam/                # 预览、录像、存储、回放及辅助视图
   │   ├── home/                   # 仪表主页及蓝牙状态联动
   │   ├── phone_book/             # 联系人、通话记录与拨号
   │   ├── music_player/           # SD 卡音乐扫描、播放列表和播放控制
   │   ├── ota/                    # OTA 进度界面
   │   ├── CMakeLists.txt          # AP 组件源码与依赖
   │   └── Kconfig.projbuild       # 显示、旋转和投屏参数
   ├── cp/                         # 协处理器（CP）应用与配置
   ├── partitions/bk7259/          # 分区表与 RAM region
   ├── Makefile / CMakeLists.txt   # 构建入口
   ├── dbuild.sh / dbuild.ps1      # Docker 构建脚本
   └── bk7259_bsp.ld               # 链接脚本



编译
--------


目标芯片为 ``bk7259``，通过 Docker 构建脚本编译：

.. code-block:: bash

   cd projects/scooter_1280_720_v2
   ./dbuild.sh make bk7259



Windows PowerShell：

.. code-block:: powershell

   cd projects/scooter_1280_720_v2
   .\dbuild.ps1 make bk7259



- 构建脚本默认以 ``../..`` （仓库根目录）作为 ``SDK_DIR``，必要时可通过环境变量覆盖。
- 编译产物位于工程 ``build/`` 目录，将生成的固件烧录到 BK7259。

关键配置
------------


工程配置主要位于 ``ap/config/bk7259_ap/defconfig`` 和 ``ap/Kconfig.projbuild``：

- ``CONFIG_LCD_ER68576B_MIPI_720x1280=y``：使用 ER68576B MIPI DSI 屏。
- ``CONFIG_SCOOTER_LVGL_HOR_RES=720``、``VER_RES=1280``：LVGL framebuffer 分辨率。
- ``CONFIG_SCOOTER_UI_ROTATION=270``：旋转为 1280x720 横屏。
- ``CONFIG_SCOOTER_UI_CANVAS_WIDTH=1280``、``HEIGHT=720``：UI 设计画布尺寸。
- ``CONFIG_SCOOTER_CAST_JPEG_*``：投屏源尺寸、目标尺寸和 90° 旋转参数。
- ``CONFIG_CSI_GC2053=y``、``CONFIG_CSI_CV2005=y``：行车记录摄像头支持。
- ``CONFIG_VIDEO_RECORDER=y``、``CONFIG_BK_ENCODER=y``：录像及编码能力。
- ``CONFIG_BLUETOOTH_MULTI_CONTROLLER=y``：启用 BK3515N 蓝牙从控。
- ``CONFIG_BK_BLE_PROVISIONING=y``：启用 BLE 配网。
- ``CONFIG_HFP_HF_DEMO=y``、``CONFIG_PBAP_CONTACTS=y``：免提通话及联系人同步。
- ``CONFIG_FATFS_SDCARD=y``、``CONFIG_SDCARD=y``：SD 卡媒体与录像存储。

BK3515N UART 链路默认使用 UART1，复位脚为 GPIO51，CTS / RTS 为 GPIO28 / GPIO29，
初始波特率为 115200，协商波特率为 921600。

启动流程
------------


``ap/ap_main.c`` 的 ``main()`` 按以下顺序初始化：

1. ``bk_init()``：系统初始化；
2. ``media_service_init()``：媒体服务初始化；
3. ``app_board_init()``：使能 LDO、初始化 frame buffer 和可选触摸；
4. ``app_display_init()``：注册 UI 回调，初始化 DPU / MIPI DSI / LCD，在播放开机 AVI 的同时
   预加载主页背景，然后启动 LVGL、注册投屏恢复 hook 并初始化媒体设备；

5. ``app_bt_init()``：创建 ``ap_bt_startup_task``，完成 MAC 同步、BK3515N BSC 初始化、
   蓝牙协议栈、媒体消息、SDP、经典蓝牙 profile 和 BLE 配网初始化；

6. ``app_key_init()``：注册物理按键操作。

启动后默认进入 1280x720 LVGL 仪表主页，并启动行车记录后台流程。

功能说明
------------


> 以下源文件路径均相对于 solution 根目录。

开机动画与仪表主页
,,,,,,,,,,,,,,,,,,,,,,


``app_display_init()`` 在 LVGL 启动前调用 ``boot_avi_play()`` 播放 SD 卡中的开机 AVI。
同时，``boot_bg_preload`` 在线程中预解码主页 JPEG 背景，避免进入主页时同步解码造成卡顿。

主要源文件：

- ``projects/scooter_1280_720_v2/ap/ap_main.c``：显示初始化编排；
- ``components/boot_avi_play/``：开机 AVI 播放及主页背景预加载；
- ``projects/scooter_1280_720_v2/ap/home/home_ui.c``：车速、灯光、蓝牙音乐和通话状态显示；
- ``projects/scooter_1280_720_v2/ap/beken_generated/``：UI 页面与资源。

行车记录
,,,,,,,,,,,,


行车记录模块支持后台连续分段录像、辅助视图、录像列表、H.264 回放、存储管理和网络就绪后
启动 FTP 服务。录像文件保存在 ``/sd0/dashcam``。

主要源文件：

- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_app.c``：摄像头、录像和回放状态机；
- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_camera.c``：ISP / CSI 摄像头管理；
- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_recorder.c``：H.264 录像封装；
- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_storage.c``：目录扫描、文件命名、空间统计和旧文件回收；
- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_player.c``、``dashcam_video_v3.c``：
  录像解码、缩放和 LVGL 显示；

- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_assitview.c``：转向辅助视图；
- ``projects/scooter_1280_720_v2/ap/dashcam/dashcam_ui.c``：录像列表与按键操作。

默认参数定义在 ``dashcam/dashcam_config.h``：预览为 960x540，录像为 1280x720 @ 25 fps，
回放显示为 960x540，每段 60 秒。开发配置最多保留 1000 个文件；发布配置可切换为按剩余
空间删除最旧文件的循环录像模式。

手机投屏、导航与配网
,,,,,,,,,,,,,,,,,,,,,,,,


手机 App 通过 SDP 发现设备并建立 CTRL / VIDEO / AUDIO 通道，将 JPEG 视频流送入
``jpeg_stream_pipeline`` 解码、旋转并显示。断开连接后关闭投屏管线、恢复 LVGL 并重新启动
SDP 广播。

BLE boarding 通道同时承载 Wi-Fi 配网、导航控制及导航文件分片传输：

- opcode 50：文件传输控制；
- opcode 51：文件数据分片；
- opcode 52：导航投屏启动 / 停止；
- opcode 53：选择 BT / Wi-Fi 导航类型。

相关实现位于 ``components/media/``、``components/network_provisioning/``、
``components/cast_pipeline/`` 和 ``components/display_ui/``。

蓝牙音乐、通话与联系人
,,,,,,,,,,,,,,,,,,,,,,,,,,


- A2DP Sink / AVRCP 支持手机音乐播放、音量和播放控制；
- HFP HF 支持 SCO 免提通话，通话音频优先于背景音乐；
- PBAP 将手机联系人同步到本地缓存；
- ``ap/phone_book/phone_book_ui.c`` 显示联系人和通话记录，并支持物理按键选择、拨号；
- ``ap/home/home_ui.c`` 将歌曲信息、播放状态、来电和去电状态同步到仪表主页。

本地音乐
,,,,,,,,,,,,


``ap/music_player/music_player_ui.c`` 扫描 SD 卡 ``/sd0/Music`` 目录，识别
MP3、WAV、FLAC、M4A、AAC、OGG 和 APE 文件，并读取可用的媒体标签。
当前播放器注册 MP3、WAV 和 AAC 解码器，支持播放列表、上一首 / 下一首、暂停 / 恢复、
顺序循环与单曲循环等操作。

OTA 界面
,,,,,,,,,,,,


``ap/ota/ota_ui.c`` 管理 OTA 页面进入、退出以及进度动画。该模块当前提供升级入口和
进度展示界面，实际升级业务需要接入对应 OTA 服务。

物理按键
------------


默认按键行为：

- 上键：接听电话；双击挂断；
- 左键：清除网络配置并重启；
- 右键：清除网络配置；
- 中键：短按、双击和长按由当前页面处理，用于页面切换、焦点移动、确认或返回。

CLI 命令
------------


工程通过 ``.cli_cmdtabl`` 注册 ``dashboard`` 命令：

.. code-block:: text

   dashboard np_erase [reboot]        # 擦除已保存的 BLE 配网信息，可选重启
   dashboard np_start_advertise       # 重新开始 BLE 配网广播
   dashboard dashcam                  # 进入行车记录页面
   dashboard phone_book               # 进入联系人页面
   dashboard pet_toggle               # 模拟中键短按
   dashboard pet_double               # 模拟中键双击
   dashboard pet_enter                # 模拟中键长按
   dashboard dashcam_count            # 显示录像数量与 SD 卡剩余空间
   dashboard dashcam_trim [target]    # 删除最旧录像，保留到指定数量
   dashboard dashcam_rec_start        # 启动后台录像
   dashboard dashcam_rec_stop         # 停止后台录像
   dashboard dashcam_turn_left        # 启动转向辅助视图
   dashboard dashcam_turn_off         # 关闭转向辅助视图