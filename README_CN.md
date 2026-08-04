# Armino Dashboard 两轮车方案

* [English](./README.md)

## 概述

本仓库是基于 Armino SMP 的 Dashboard 两轮车解决方案，当前主工程为
[`projects/scooter_1280_720`](./projects/scooter_1280_720)，目标芯片为 BK7259。

方案采用 BK7259（AP，主控）+ BK3515N（蓝牙从控）双芯片架构，面向 1280x720 横屏仪表场景，当前已支持：

- MIPI DSI + DPU + LVGL 仪表显示；
- 开机 AVI 播放；
- 手机 JPEG 投屏；
- BLE / Wi-Fi 配网；
- Wi-Fi 图传与 BLE 图传；
- 经典蓝牙音频、导航语音与多控制器蓝牙；
- SD 卡存储能力，为后续录像业务预留基础。

## 目录结构

```text
.
├── components/                  # 显示、投屏、媒体、蓝牙、配网等方案组件
├── docs/                        # Sphinx 文档
├── projects/
│   └── scooter_1280_720/        # 两轮车产品工程主入口
├── README_CN.md
├── README.md
```

## 编译

进入主工程目录，通过 docker 构建脚本编译 BK7259 固件：

```bash
cd projects/scooter_1280_720
./dbuild.sh make bk7259
```

Windows PowerShell：

```powershell
cd projects/scooter_1280_720
.\dbuild.ps1 make bk7259
```

构建脚本默认以仓库根目录作为 `SDK_DIR`。如 Armino SMP SDK 不在默认位置，可通过环境变量指定：

```bash
export SDK_DIR=/path/to/bk_avdk_smp
./dbuild.sh make bk7259
```

编译产物位于工程 `build/` 目录下，请将生成的 BK7259 固件烧录到主控板。

## 使用入口

主要文档：

- [产品工程说明](./projects/scooter_1280_720/README_CN.md)
- [中文文档入口](./docs/bk7259/zh_CN/index.rst)
- [Dashboard App 使用指南](./docs/bk7259/zh_CN/projects/dashboard_app/index.rst)

Dashboard App 支持 Wi-Fi 图传和 BLE 图传两种流程。请先在 App 配置中心选择 BK7259、图传方式和图像参数，保存后按文档步骤进行配网、搜索设备并启动导航投屏。

## 文档构建

如需本地生成 HTML 文档：

```bash
cd docs
make doc
```

生成结果位于 `docs/bk7259/zh_CN/_build/latest` 与 `docs/bk7259/en/_build/latest`。
