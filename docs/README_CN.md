* [English](./README.md)

# 两轮车 demo

## 概述

此为两轮车解决方案，支持导航信息的投屏显示，语音导航/提示音的播放，支持 1280X720 分辨率的行车记录仪功能。
由两块芯片搭配使用：一块为 BK7258，负责音视频数据的处理；一块为 BK3515N，负责蓝牙音频的传输。

## 工程编译

- 此工程依赖 `bk_avdk_smp_main`。编译时需要下载 `bk_avdk_smp_main` 代码。
- 编译方法：在两轮车方案目录 `./projects/scooter` 下编译。
- 编译之前需要先修改 Makefile 文件（`./projects/scooter/Makefile`），将依赖的源码映射到 `bk_avdk_smp_main` 上，参考如下：

```makefile
# 映射依赖的源码到 bk_avdk_smp_main 上
SDK_DIR ?= $(abspath ../..)

# change to
SDK_DIR = /home/user.name/bk_avdk_smp_main
```

- 编译命令：`make bk7258`
- 上面是 BK7258 的编译命令，编译完成后会生成 bin 文件，路径：`./projects/scooter/build/bk7258/scooter/package/all-app.bin`。
- 烧录此固件到 BK7258 上。

## 开机演示

开机默认 LCD 屏幕上显示 LVGL 动画（分辨率：480X272）。

## 导航演示

1. 连接蓝牙设备（如手机）到 BK3515N 上。
2. 发送命令，BK7258 作为 STA 连接到手机热点，手机热点定义为 `scooter`，密码为 `12345678`。命令：`ap_cmd test sta scooter 12345678`。
3. 连接成功后，打开手机上的导航软件，配置好 IP 地址，即可开始导航。
4. LCD 屏幕会自动从 LVGL 画面切到导航画面，导航语音会从 BK7258 的 speaker 中输出。

支持导航画面和 LVGL 画面切换显示：

- 发送命令 `ap_cmd test switch lvgl`，即可从导航画面切换到 LVGL 画面。
- 发送命令 `ap_cmd test switch camera`，即可从 LVGL 画面切换到导航画面。

## 行车记录仪

行车记录仪通过 UVC 采集实时画面，并将其编码压缩成 H.264 格式数据，存储在 SD 卡中，默认每十分钟为一段。

启动：

1. 发送命令 `ap_cmd test uvc open 1280X720`，即可打开 UVC 且输出分辨率为 1280X720。
2. 发送命令 `ap_cmd test h264e open`，即可打开编码器，开始压缩数据。
3. 发送命令 `ap_cmd test storage open test.h264`，即可打开存储器，开始存储数据到 xxx_test.h264 中。
4. 上面 xxx 的范围是 0-65535，默认每个文件存储 10 分钟的视频。

关闭：

1. 发送命令 `ap_cmd test storage close`，即可关闭存储器。
2. 发送命令 `ap_cmd test h264e close`，即可关闭编码器。
3. 发送命令 `ap_cmd test uvc close`，即可关闭 UVC。
