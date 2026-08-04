# Scooter Dashboard V1.0 V2 使用说明

* [English](./README.md)

## 开发板使用说明

本说明仅适用于 Dashboard V1.0 硬件。

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

本工程面向 Dashboard V1.0 硬件，包含主页导航、Dashcam 循环录像与录像回放、Assist View、蓝牙电话按键控制、网络配网以及 SD 卡 MTP 导出功能。

- SD 卡录像文件保存于 `/sd0/dashcam`。
- 进入 Dashcam 页面时会先停止并完成当前录像，避免录像写入与视频回放同时访问 SD 卡。
- 离开 Dashcam 页面后会自动重新开始录像。
- 所有已映射的按键事件带有 500 ms 防抖/节流：连续触发间隔小于 500 ms 的事件会被忽略。

## 页面说明

| 页面 | 用途 | 进入方式 |
| --- | --- | --- |
| 主页（Home） | 系统默认页面，用于选择 Dashcam 或 OTA 功能页面 | 上电后默认进入；在功能页面中中键长按返回 |
| Dashcam 页面 | 浏览 SD 卡中的已完成录像并进行视频回放；录像列表显示时间和文件大小 | 主页中选择 Dashcam 后中键长按进入 |
| OTA 页面 | 提供 OTA 升级状态和进度展示 | 主页中选择 OTA 后中键长按进入 |
| Assist View | 全屏显示摄像头实时画面；进入期间录像保持运行 | 左键或右键长按进入；中键短按退出并回到主页 |

## 按键使用

Dashboard V1.0 按键 GPIO 定义如下：

| 按键 | GPIO | 操作 | 功能 |
| --- | --- | --- | --- |
| 上 | GPIO32 | 短按 | 接听蓝牙 HFP 来电 |
| 上 | GPIO32 | 双击 | 挂断蓝牙 HFP 通话 |
| 下 | GPIO27 | 长按 | 清除已保存的网络配网信息并重启 |
| 左 | GPIO31 | 长按 | 打开 Assist View |
| 右 | GPIO29 | 长按 | 打开 Assist View |
| 中 | GPIO30 | 短按 | 主页中切换 Dashcam / OTA 选择；Dashcam 列表聚焦时切换下一条录像；Assist View 中退出并回到主页 |
| 中 | GPIO30 | 双击 | Dashcam 页面中进入或退出录像列表聚焦模式 |
| 中 | GPIO30 | 长按 | 主页中进入当前选择页面；Dashcam 列表聚焦时播放当前选中的录像 |

### 中键页面状态逻辑

中键的行为会随当前页面和 Dashcam 列表状态变化：

| 当前状态 | 中键短按 | 中键双击 | 中键长按 |
| --- | --- | --- | --- |
| 主页 | 在 Dashcam 与 OTA 入口之间切换选中项 | 无操作 | 进入当前选中的 Dashcam 或 OTA 页面 |
| Dashcam 页面，未聚焦录像列表 | 返回主页 | 进入录像列表聚焦模式 | 返回主页 |
| Dashcam 页面，录像列表已聚焦 | 切换到下一条录像，末尾自动回到第一条 | 退出录像列表聚焦模式 | 播放当前选中的录像 |
| OTA 页面 | 返回主页 | 无操作 | 返回主页 |
| Assist View | 退出 Assist View 并回到主页 | 无操作 | 无操作 |

进入 Dashcam 页面时，系统会停止并完成当前录像，然后加载 SD 卡中已完成的录像列表。

### Dashcam 回放操作

1. 中键短按切换到 Dashcam 项目。
2. 中键长按进入 Dashcam 页面。进入页面后录像会停止。
3. 中键双击进入录像列表聚焦模式。
4. 中键短按选择下一条录像。
5. 中键长按播放当前录像。
6. 中键双击退出列表聚焦模式；中键长按返回主页。离开 Dashcam 页面后录像自动恢复。

录像列表的第一行显示录像时间，第二行显示文件大小（MB）。

## CLI 命令

串口 CLI 中可使用 `ap_cmd dashboard <command>`：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd dashboard dashcam` | 直接进入 Dashcam 页面 |
| `ap_cmd dashboard dashcam_count` | 查询录像数量与 SD 卡剩余空间 |
| `ap_cmd dashboard dashcam_trim [target]` | 删除最旧录像，直到剩余录像数量不大于 `target` |
| `ap_cmd dashboard dashcam_rec_start` | 手动启动录像 |
| `ap_cmd dashboard dashcam_rec_stop` | 手动停止并完成录像 |
| `ap_cmd dashboard dashcam_turn_left` | 打开 Assist View |
| `ap_cmd dashboard dashcam_turn_off` | 关闭 Assist View |
| `ap_cmd dashboard np_erase [reboot]` | 清除网络配网信息；追加 `reboot` 后立即重启 |
| `ap_cmd dashboard np_start_advertise` | 重新开启 BLE 配网广播 |

> 注意：执行 `ap_cmd dashboard dashcam_trim [target]` 前，先使用 `ap_cmd dashboard dashcam_rec_stop` 停止录像，避免删除文件时与录像写入同时访问 SD 卡。

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

1. 插入已格式化的 SD 卡，并使用 USB 连接设备与 PC。
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