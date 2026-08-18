# Scooter Dashboard V1.0 使用说明

* [English](./README.md)

## 开发板使用说明

本说明仅适用于 Dashboard V1.0 硬件。

> **备注**：板子正常启动需要使用 DC 12V 供电。

### 跳线帽配置

上电前请确认以下跳线帽已短接：

| 用途 | 短接位置 |
| --- | --- |
| BK7259 下载与串口日志 | `P11 ↔ RXD`、`P10 ↔ TXD` |
| BK3515NS 下载 | `BT_P0 ↔ RXD`、`BT_P1 ↔ TXD` |
| BK7259 与 BK3515NS 串口通信 | `P71 ↔ BT_RST`、`P0 ↔ BT_P0`、`P1 ↔ BT_P1` |

### 接口使用

- BK7259 下载和串口日志均使用 **USB-UART** 接口。
- BK3515NS 下载使用 **USB-UART** 接口；请按上表切换跳线，下载完成后拔掉下载连接。
- MTP 使用开发板的 **USB** 接口；连接 PC 后执行 `ap_cmd mtp start`，将 SD Card 作为媒体设备访问。

## 功能概览

本工程是 Dashboard V1.0 硬件的基础演示工程，提供：

- 1280x720 MIPI DSI + DPU + LVGL 仪表界面；
- 开机 AVI 播放；
- 仪表车速往返动画和左转向灯闪烁演示；
- 从 SD Card 加载宠物 GIF 页面；
- 手机 JPEG 投屏及断开后的仪表界面恢复；
- BLE / Wi-Fi 配网；
- BK3515NS 经典蓝牙音乐及 HFP 免提通话；
- 通过 USB MTP 浏览和复制 SD Card 文件。

## 页面与按键

### 仪表主页

上电并播放开机动画后，默认进入 1280x720 仪表主页。页面自动演示：

- 车速表在 0 到 100 之间往返变化；
- 左转向灯每 500 ms 在绿色和灰色状态之间切换。

### 宠物 GIF 页面

将 `pet_1.gif` 或 `pet_2.gif` 放在 SD Card 根目录。短按上键后，系统随机选择一个存在的
GIF 并进入宠物页面；再次短按上键返回仪表主页。

Dashboard V1.0 按键 GPIO 与当前功能如下：

| 按键 | GPIO | 操作 | 功能 |
| --- | --- | --- | --- |
| 上 | GPIO32 | 短按 | 在仪表主页和宠物 GIF 页面之间切换 |
| 下 | GPIO27 | 短按 | 清除已保存的网络配网信息并重启 |
| 左 | GPIO31 | 短按 / 双击 / 长按 | 当前未分配功能 |
| 右 | GPIO29 | 短按 / 双击 / 长按 | 当前未分配功能 |
| 中 | GPIO30 | 短按 | 清除已保存的网络配网信息，不重启 |

其他双击、长按和持续按住操作当前未注册业务回调。

## 手机投屏

手机 App 通过 SDP 发现设备并建立 CTRL / VIDEO / AUDIO 通道，将 JPEG 视频流发送到
设备解码和显示。投屏期间 LVGL 仪表暂停；连接断开后，系统恢复仪表主页、车速动画和
转向灯动画，并重新启动 SDP 广播。

## 蓝牙与网络配网

- BK7259 通过 UART1 与 BK3515NS 通信：RX / TX 为 GPIO0 / GPIO1，RTS / CTS 为
  GPIO2 / GPIO3，复位脚为 GPIO71；
- UART 初始波特率为 115200，协商波特率为 921600；
- BLE boarding 支持 Wi-Fi 配网；
- A2DP Sink / AVRCP 支持手机音乐播放和控制；
- HFP HF 支持免提通话。

## CLI 命令

串口 CLI 中可使用：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd dashboard np_erase [reboot]` | 清除网络配网信息；追加 `reboot` 后立即重启 |
| `ap_cmd dashboard np_start_advertise` | 重新开启 BLE 配网广播 |

## SD Card MTP

工程启动时注册 `mtp` CLI，但不会自动开启 USB MTP 设备。

1. 使用开发板 USB 接口连接设备与 PC，无需额外插入存储卡。
2. 在串口执行 `ap_cmd mtp start`。
3. PC 将 SD Card 枚举为 MTP 媒体设备，可浏览和复制 `/sd0` 中的文件。
4. 使用结束后执行 `ap_cmd mtp stop`。

支持的 MTP 命令：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd mtp start` | 挂载 SD Card 并启动 MTP USB 设备 |
| `ap_cmd mtp stop` | 停止 MTP USB 设备 |
| `ap_cmd mtp status` | 查询 MTP 当前状态 |
| `ap_cmd mtp ls [path]` | 通过串口列出 SD Card 目录；默认路径为 `/sd0` |

> 建议在 PC 文件复制完成后再执行 `ap_cmd mtp stop`，避免 USB 传输被中断。

## 编译

目标芯片为 `bk7259`，通过 Docker 构建脚本编译：

```bash
cd projects/scooter_dashboard_v1_1280_720
./dbuild.sh make bk7259
```

Windows PowerShell：

```powershell
cd projects/scooter_dashboard_v1_1280_720
.\dbuild.ps1 make bk7259
```

