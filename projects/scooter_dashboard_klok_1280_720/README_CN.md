# Scooter Dashboard Klok 1280x720 使用说明

* [English](./README.md)

## 开发板使用说明

本工程适用于 SCH-7259 V1.0 五键开发板，目标芯片为 BK7259。

> **备注**：板子正常启动需要使用 DC 12V 供电。

### 跳线与接口

- BK7259 下载和串口日志使用 **USB-UART** 接口。上电前请确认
  `P11 ↔ RXD`、`P10 ↔ TXD` 已短接。
- 媒体文件保存在板载 SD-NAND 的 `/sd0` 分区。
- MTP 使用开发板的 **USB** 接口。连接 PC 后执行
  `ap_cmd mtp start`，即可将 `/sd0` 作为媒体设备访问。

## 功能概览

本工程提供 K 歌和 MV 播放演示，主要功能包括：

- 1280x720 LVGL 界面，包含首页、Klok 主页、歌曲列表和 MV 播放页；
- SD Card MP4 / AVI 文件扫描和播放；
- H.264 硬件解码和 AAC 音频解码；
- 全屏 Flexa 直显以及 PP-OSD 播放控件叠加；
- Klok 主页和歌曲列表中的 576x304 RGB565 视频预览；
- 触摸操作、五键播放控制和串口 CLI 控制；
- 通过 USB MTP 浏览和复制 `/sd0` 中的媒体文件。

设备上电后默认进入 `home` 页面，点击左侧卡片可进入 Klok 主页。

## 页面导航

| 页面 | 进入方式 | 返回目标 |
| --- | --- | --- |
| `home` | 上电默认页面，或执行 `klok page home` | - |
| `klok_main` | 点击首页左侧卡片，或执行 `klok page main` | `home` |
| `song_list` | 从 Klok 主页进入歌名/歌手页面，或执行 `klok page list` | `klok_main` |
| `mv_play` | 点击歌曲、预览区域，或执行 `klok play [file]` | `song_list` |

## 视频直出模式与预览模式

工程默认设置 `KLOK_VIDEO_FLEXA_DIRECT_MODE=1`，播放器会根据当前
页面在两种视频输出模式之间切换。切换只重建视频解码器，容器解析器、
音频解码器、当前音轨和音频设备保持运行。

### Flexa 直出模式

进入 `mv_play` 全屏播放页时使用 Flexa 直出：

1. H.264 解码器输出 NV12，并在解码侧执行 90° 旋转；
2. Flexa 将画面处理为压缩 ARGB8888，直接提交给 DPU；
3. 直出模式关闭 OSD Snapshot，不再把 LVGL 控件融合到视频帧；
4. 直出期间 Flexa 独占 VG-Lite HSRAM，LVGL GPU 实例和常规刷新
   暂停，避免 LVGL 与解码器同时访问 GPU/DPU。

该路径不需要把完整视频帧交给 LVGL，适合全屏播放，可减少一次
LVGL 图像更新和全帧拷贝。由于 LVGL 刷新暂停，全屏直出期间应优先
使用五键或串口 CLI 控制播放和返回。

### RGB565 预览模式

`klok_main` 和 `song_list` 页面使用预览模式：

1. H.264 帧解码器输出 1280x720 RGB565，解码器不旋转；
2. 显示工作线程通过 `lv_async_call` 把帧交回 LVGL 线程；
3. LVGL 将视频缩放显示在固定的 576x304 预览区域；
4. LVGL 恢复对 VG-Lite 和 DPU 的控制，可正常绘制完整页面。

### 模式切换时序

- 从歌曲列表进入 `mv_play`：先暂停并排空当前视频解码链路，再暂停
  LVGL 刷新、进入 Flexa 直出，最后创建直出解码器并继续播放。
- 从 `mv_play` 返回歌曲列表：先退出直出并恢复 LVGL GPU/刷新，
  再创建 RGB565 帧解码器，继续显示同一文件的预览。
- 同一文件切换输出模式时，音频解码器和播放位置保持不变；选择新
  文件进入全屏页时，使用新文件重新启动播放。
- 切换过程中会屏蔽重复的返回操作，避免同时销毁或创建两个视频
  解码器。

如需关闭直出路径，可在 `ap/CMakeLists.txt` 中将
`KLOK_VIDEO_FLEXA_DIRECT_MODE` 改为 `0`。关闭后固定使用 RGB565
帧解码和 PP-OSD 路径，不再进行运行时输出模式切换。

## 媒体文件准备

1. 将媒体文件复制到 `/sd0` 根目录。推荐使用 H.264 视频和 AAC
   音频的 MP4 文件，也支持 AVI 容器。
2. 默认演示文件名为 `/sd0/klok_demo.mp4`。不带文件名执行
   `ap_cmd klok play` 时会播放该文件。
3. 歌曲列表最多扫描 64 个媒体文件。
4. CLI 中的相对文件名会自动添加 `/sd0/` 前缀。例如：

```text
ap_cmd klok play demo.mp4
```

等价于播放 `/sd0/demo.mp4`。也可以直接使用绝对路径：

```text
ap_cmd klok play /sd0/demo.mp4
```

> **格式说明**：歌曲列表会识别 `.mp4`、`.avi`、`.mkv` 和 `.mov`
> 扩展名，但当前播放器仅注册了 MP4 和 AVI 容器解析器，建议使用
> `.mp4` 或 `.avi` 文件。

## 五键控制

五键均为低电平触发，当前只注册短按回调。只有播放器已经成功启动
时按键才生效；尚未播放、播放已停止或 UI 尚未初始化时，按键会被
忽略。

| 按键 | GPIO | 功能 |
| --- | --- | --- |
| 下 | GPIO27 | 从全屏播放页返回歌曲列表，同时从 Flexa 直出切换为 RGB565 预览 |
| 中 | GPIO30 | 在暂停和继续播放之间切换，保持当前文件和播放位置 |
| 上 | GPIO32 | 在音轨 0（原唱）和音轨 1（伴唱）之间切换 |
| 左 | GPIO31 | 重新打开当前文件，使音频和视频一起从头播放 |
| 右 | GPIO29 | 按 `/sd0` 媒体缓存顺序播放下一首，到末尾后回到第一首 |

### 按键处理说明

- 按键回调不会直接操作播放器，而是把动作放入深度为 5 的控制
  队列，由独立工作线程串行执行，避免在按键回调中阻塞。
- 下键返回时会先检查播放状态、返回请求是否已在处理以及输出模式
  是否正在切换。任一条件不满足时，本次按键被忽略并输出诊断日志。
- 直出模式下 LVGL 刷新已暂停，因此下键返回由按键控制线程持有
  显示锁后完成，不依赖 LVGL 异步回调。
- 上键只有在媒体文件确实包含对应音轨时才能成功切换。工程约定
  音轨 0 为原唱、音轨 1 为伴唱。
- 右键使用启动播放时建立的 `/sd0` 媒体缓存；列表为空时不会切歌。
- 快速连续按键可能因控制队列已满或输出模式正在切换而被丢弃。
- 双击、长按和持续按住当前未注册业务回调。

## CLI 命令

### Klok 页面与播放控制

| 命令 | 说明 |
| --- | --- |
| `ap_cmd klok page <home\|main\|list\|play> [file]` | 切换页面；进入播放页时可指定文件 |
| `ap_cmd klok back` | 按 `mv_play → song_list → klok_main → home` 返回 |
| `ap_cmd klok play [file]` | 进入播放页并播放指定文件；省略文件时播放默认文件 |
| `ap_cmd klok full` | 进入全屏播放页 |
| `ap_cmd klok next` / `prev` | 播放下一首或上一首 |
| `ap_cmd klok replay` | 从头重播当前文件 |
| `ap_cmd klok stop` | 停止播放 |
| `ap_cmd klok pause` / `resume` | 暂停或继续播放 |
| `ap_cmd klok playpause` | 切换暂停和播放状态 |
| `ap_cmd klok audio <0\|1>` | 选择音轨；`0` 为原唱，`1` 为伴唱 |
| `ap_cmd klok vocal` / `accompany` | 切换到原唱或伴唱音轨 |
| `ap_cmd klok mute` | 切换静音状态 |
| `ap_cmd klok volup` / `voldown` | 增大或减小音量 |

### 底层播放器调试

| 命令 | 说明 |
| --- | --- |
| `ap_cmd video_play_engine start <file>` | 直接播放指定绝对路径文件 |
| `ap_cmd video_play_engine stop` | 停止播放器 |
| `ap_cmd video_play_engine pause` / `resume` | 暂停或继续播放器 |
| `ap_cmd video_play_engine seek <ms>` | 跳转到指定毫秒位置 |
| `ap_cmd video_play_engine vol_up <n>` | 音量增加 `n` |
| `ap_cmd video_play_engine vol_down <n>` | 音量减少 `n` |
| `ap_cmd video_play_engine mute <on\|off>` | 设置静音状态 |
| `ap_cmd video_play_engine audio_track <index>` | 选择音轨 |

## SD Card MTP

工程启动时注册 `mtp` CLI，但不会自动启动 USB MTP 设备。

1. 使用开发板 USB 接口连接设备与 PC。
2. 在串口执行 `ap_cmd mtp start`。
3. PC 枚举出 MTP 媒体设备后，可浏览和复制 `/sd0` 中的文件。
4. 文件复制完成后执行 `ap_cmd mtp stop`。

| 命令 | 说明 |
| --- | --- |
| `ap_cmd mtp start` | 挂载 SD Card 并启动 MTP USB 设备 |
| `ap_cmd mtp stop` | 停止 MTP USB 设备 |
| `ap_cmd mtp status` | 查询 MTP 当前状态 |
| `ap_cmd mtp ls [path]` | 通过串口列出目录；默认路径为 `/sd0` |

> 请在 PC 文件复制完成后再停止 MTP，避免 USB 传输被中断。

## 关键配置

- 默认 MIPI 面板为 ER68576B，物理分辨率为 720x1280，LVGL
  旋转后使用 1280x720 逻辑分辨率。
- 触摸控制器为 GT911。
- SCH-7259 V1.0 的 SD-NAND 使用 SDIO1。为避免持续 4-bit
  读取出现 `DATA_END_BIT` 错误，工程固定使用 20 MHz 1-bit 模式。
- `KLOK_VIDEO_FLEXA_DIRECT_MODE=1` 用于启用全屏 Flexa 输出和
  RGB565 预览之间的切换。

## 已知限制

- `/sd0/klok_demo.mp4` 不存在时，默认播放命令会失败。
- 当前仅支持 MP4 和 AVI 容器解析，列表中出现的 MKV / MOV 文件
  不保证可以播放。
- 视频输出模式切换期间，重复点击或按键可能被暂时忽略。
- 排行榜、收藏等部分 UI 卡片为演示占位，尚未连接后端业务。
- 全屏播放中的重播通过重新打开当前文件实现，并非执行 `seek(0)`。

## 编译

Linux：

```bash
cd projects/scooter_dashboard_klok_1280_720
./dbuild.sh make bk7259
```

Windows PowerShell：

```powershell
cd projects/scooter_dashboard_klok_1280_720
.\dbuild.ps1 make bk7259
```
