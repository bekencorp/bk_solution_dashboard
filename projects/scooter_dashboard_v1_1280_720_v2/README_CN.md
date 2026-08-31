# Scooter Dashboard V1.0 1280×720 V2 使用说明

* [English](./README.md)

## 开发板使用说明

本说明适用于搭载 1280×720 横屏界面的 Dashboard V1.0 硬件，对应工程 `scooter_dashboard_v1_1280_720_v2`。

> **备注**：板子正常启动需要用 DC 12V 供电。

### 工程适配信息

| 配置项 | 本工程配置 |
| --- | --- |
| 主控 | BK7259 |
| LCD | ER68576B MIPI，物理分辨率 720×1280 |
| LVGL 帧缓冲 | 720×1280 |
| LVGL 画布 | 1280×720 横屏 |
| UI 旋转 | 270°，将竖屏面板映射为横屏界面 |
| 启动背景 | SD 卡中的 `/sd0/home_bg.jpg` |
| 投屏 JPEG | 输入/输出 1280×720，旋转 90° |
| Dashcam 采集与录像 | ISP MP 1280×720，H.264 录像 25 FPS |
| Assist View | 输出 1280×720，摄像头画面旋转 90° |
| 录像回放 | 解码缩放目标预交换尺寸 720×1280，管线旋转 90°后输出 1280×720 |
| 分辨率资源 | 使用文件名带 `1280x720` 的背景图片及适配该分辨率的布局、图标和字体 |

本工程固件及 UI 资源仅适用于 1280×720 横屏界面，请勿与 `scooter_dashboard_v1_1024_600_v2` 的固件或生成资源混用。

### 跳线帽配置

上电前请确认以下跳线帽已短接：

| 用途 | 短接位置 |
| --- | --- |
| BK7259 下载与串口日志 | `P11 ↔ RXD`、`P10 ↔ TXD` |
| BK3515NS 下载 | `BT_P0 ↔ RXD`、`BT_P1 ↔ TXD` |
| BK7259与BK3515NS串口通信 | `P71 ↔ BT_RST`、`P0 ↔ BT_P0`、`P1 ↔ BT_P1` |

### 接口使用

- BK7259 下载和串口日志均使用 **USB-UART** 接口。
- BK3515NS 下载使用 **USB-UART** 接口，需按上表切换对应跳线，仅下载连接，下载完成请拔掉。
- MTP 使用开发板的 **USB** 接口；连接 PC 后，在串口 CLI 执行 `ap_cmd mtp start` 以启用 SD 卡媒体设备。

## 功能概览

本工程面向 Dashboard V1.0 1280×720 横屏界面版本，包含主页导航、Dashcam 循环录像与录像回放、SD 卡本地音乐播放、电话簿、Assist View、蓝牙电话控制、网络配网以及 SD 卡 MTP 导出功能。

- SD 卡录像文件保存于 `/sd0/dashcam`。
- 进入 Dashcam 页面后会异步停止并完成当前录像，再加载 SD 卡录像列表，避免录像写入与视频回放同时访问 SD 卡。
- 离开 Dashcam 页面后会自动重新开始录像。
- Music Player 扫描 `/sd0/Music` 中的音频文件；进入页面时暂停后台录像，离开后恢复录像，避免播放与录像同时访问 SD 卡。

## 页面说明

| 页面 | 用途 | 进入方式 |
| --- | --- | --- |
| 主页（Home） | 系统默认页面，用于选择 Dashcam、OTA、电话簿或 Music Player | 上电后默认进入；在功能页面中双击中键返回 |
| Dashcam 页面 | 浏览 SD 卡中的已完成录像并进行视频回放；录像列表显示时间和文件大小 | 主页中用左/右键选择 Dashcam，再短按中键进入 |
| OTA 页面 | 提供 OTA 升级状态和进度展示 | 主页中用左/右键选择 OTA，再短按中键进入 |
| 电话簿页面 | 浏览联系人和通话记录，并通过蓝牙 HFP 拨号 | 主页中用左/右键选择电话簿，再短按中键进入 |
| Music Player | 扫描并播放 `/sd0/Music` 中的本地音频，最多加载 100 首 | 主页中用左/右键选择 Music Player，再短按中键进入 |
| Assist View | 全屏显示摄像头实时画面；进入期间录像保持运行 | 在主页长按左键或右键进入；双击中键退出并回到主页 |

## 按键使用

Dashboard V1.0 按键 GPIO 定义如下：

| 按键 | GPIO | 操作 | 功能 |
| --- | --- | --- | --- |
| 上 | GPIO32 | 短按 | 电话簿或 Music Player 播放列表中选择上一项 |
| 上 | GPIO32 | 长按 | 进入经典蓝牙配对模式，用于手机取消配对后重新搜索连接 |
| 下 | GPIO27 | 短按 | 电话簿或 Music Player 播放列表中选择下一项 |
| 下 | GPIO27 | 长按 | 仅在主页清除已保存的网络配网信息并重启 |
| 左 | GPIO31 | 短按 | 选择上一项；电话簿中切换列表；Music Player 中切换控制项或播放列表 |
| 左 | GPIO31 | 长按 | 仅在主页打开 Assist View |
| 右 | GPIO29 | 短按 | 选择下一项；电话簿中切换列表；Music Player 中切换控制项或播放列表 |
| 右 | GPIO29 | 长按 | 仅在主页打开 Assist View |
| 中 | GPIO30 | 短按 | 确认当前选项：进入页面、播放录像或音乐、执行音乐控制、拨打电话或执行来电/通话操作 |
| 中 | GPIO30 | 双击 | 从功能页面返回主页；在 Assist View 中退出并回到主页 |

### 页面按键逻辑

| 当前页面或状态 | 方向键 | 中键短按 | 中键双击 |
| --- | --- | --- | --- |
| 主页 | 左/右键在 Dashcam、OTA、电话簿、Music Player 之间循环切换 | 进入当前选中的页面 | 保持或返回主页 |
| Dashcam 页面 | 左/右键选择上一条或下一条录像，首尾循环 | 播放当前选中的录像 | 返回主页 |
| OTA 页面 | 无页面内方向键操作 | 无操作 | 返回主页 |
| 电话簿页面 | 左/右键切换联系人和通话记录；上/下键选择列表项 | 拨打当前选中的号码 | 返回主页 |
| Music Player | 左/右键切换播放控制项和播放列表；上/下键选择歌曲 | 执行当前控制项或播放选中的歌曲 | 返回主页 |
| HFP 来电 | 左/右键选择接听或挂断 | 执行当前选中的操作 | 返回主页 |
| HFP 去电或通话中 | 当前通话操作自动获得焦点 | 挂断通话 | 返回主页 |
| Assist View | 无操作 | 无操作 | 退出 Assist View 并回到主页 |

进入 Dashcam 页面后会先显示加载状态，后台停止并完成当前录像，然后扫描 SD 卡中已完成的录像；离开页面后录像自动恢复。

### Dashcam 回放操作

1. 在主页短按左键或右键选择 Dashcam。
2. 短按中键进入 Dashcam 页面，等待录像列表加载完成。
3. 短按左键或右键选择上一条或下一条录像。
4. 短按中键播放当前选中的录像。
5. 双击中键返回主页；离开 Dashcam 页面后录像自动恢复。

录像列表的第一行显示录像时间，第二行显示文件大小（MB）。

### Music Player 操作

1. 将支持的音频文件保存到 SD 卡的 `/sd0/Music` 目录，支持 MP3、WAV、FLAC、M4A、AAC、OGG 和 APE。
2. 在主页用左键或右键选择 Music Player，再短按中键进入。
3. 使用左/右键在播放控制项和播放列表之间切换，使用上/下键选择歌曲。
4. 短按中键执行当前控制项或播放选中的歌曲。
5. 双击中键返回主页；离开页面后后台录像自动恢复。

进入 Music Player 时会停止并完成当前录像，以避免音乐读取和录像写入同时访问 SD 卡。

## CLI 命令

串口 CLI 中可使用 `ap_cmd dashboard <command>`：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd dashboard dashcam` | 直接进入 Dashcam 页面 |
| `ap_cmd dashboard phone_book` | 直接进入电话簿页面 |
| `ap_cmd dashboard key_prev` | 模拟左键短按，选择上一项 |
| `ap_cmd dashboard key_next` | 模拟右键短按，选择下一项 |
| `ap_cmd dashboard key_enter` | 模拟中键短按，确认当前选项 |
| `ap_cmd dashboard key_home` | 模拟中键双击，返回主页 |
| `ap_cmd dashboard dashcam_count` | 查询录像数量与 SD 卡剩余空间 |
| `ap_cmd dashboard dashcam_trim` | 自动停止并完成录像，然后删除整个 `/sd0/dashcam` 录像目录 |
| `ap_cmd dashboard dashcam_rec_start` | 手动启动录像 |
| `ap_cmd dashboard dashcam_rec_stop` | 手动停止并完成录像 |
| `ap_cmd dashboard dashcam_turn_left` | 打开 Assist View |
| `ap_cmd dashboard dashcam_turn_off` | 关闭 Assist View |
| `ap_cmd dashboard np_erase [reboot]` | 清除网络配网信息；追加 `reboot` 后立即重启 |
| `ap_cmd dashboard np_start_advertise` | 重新开启 BLE 配网广播 |

> 注意：`dashcam_trim` 会删除全部录像；下次启动录像时会自动重建目录。

## FTP Server

设备完成网络连接后会自动启动 FTP Server，并将 SD 卡挂载目录 `/sd0` 作为 FTP 根目录。录像文件位于 `/sd0/dashcam`。

使用 FTP 客户端连接设备当前的网络 IP 地址：

| 配置项 | 值 |
| --- | --- |
| 协议 | FTP |
| 端口 | `21` |
| 用户名 | `bk7259` |
| 密码 | `123456` |
| 录像目录 | `/sd0/dashcam` |

连接后可使用目录浏览或文件下载功能获取录像文件。FTP 服务使用明文认证，仅建议在受信任的局域网环境中使用。

## SD 卡 MTP

工程启动时已注册 `mtp` CLI，但不会自动开启 USB MTP 设备。

1. 使用 USB 连接设备与 PC。
2. 在串口执行 `ap_cmd mtp start`。
3. PC 会将设备枚举为 MTP 媒体设备，可浏览和拷贝 `/sd0` 中的文件，包括 `/sd0/dashcam` 录像。
4. 使用结束后执行 `ap_cmd mtp stop`。

支持的 MTP 命令：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd mtp start` | 挂载 SD 卡并启动 MTP USB 设备 |
| `ap_cmd mtp stop` | 停止 MTP USB 设备 |
| `ap_cmd mtp status` | 查询 MTP 当前状态 |
| `ap_cmd mtp ls [path]` | 通过串口列出 SD 卡目录；默认路径为 `/sd0` |

> 建议在 PC 复制录像文件完成后再执行 `ap_cmd mtp stop`，避免 USB 传输被中断。