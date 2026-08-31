# 博通集成 BK7259 两轮车解决方案

- [English](./README.md)

## 概述

**BK7259 两轮车解决方案**是 BEKEN 发布并开源的两轮车智能仪表整机方案。该方案基于 **BK7259** 主控芯片，依赖 Armino 基础 SDK **BK_AVDK_SMP**，采用 BK7259 与 BK3515N 双芯片架构，面向 1280x720 横屏仪表场景，支持 MIPI DSI + DPU + LVGL 仪表显示、开机 AVI 播放、手机 JPEG 投屏、BLE / Wi-Fi 配网、Wi-Fi 图传与 BLE 图传、经典蓝牙音频、导航语音、多控制器蓝牙，以及 SD 卡存储等能力，并提供完整的端到端示例工程。

## 文档

- [BK7259 两轮车方案在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/index.html)
- [Armino SMP SDK（BK AVDK SMP）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html)



## 硬件

本方案硬件使用 BK7259 两轮车开发板（EVB），采用 BK7259 主控芯片与 BK3515N 蓝牙从控芯片，集成 1280x720 横屏 MIPI DSI 显示屏、SD 卡存储，以及复位（RESET）、配网复位（K1）等按键。

- [BK7259 两轮车开发板硬件资料](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/hw-reference/index.html)
- [BK7259 Datasheet](https://docs.bekencorp.com/spec/BK7259/BK7259_Datasheet.pdf)

开发套件购买链接：即将上架。

## 版本策略

本方案采用“维护分支 + 发布标签（Tag）”的版本管理方式：

- `release/v4.0.1` 为持续维护分支，用于该版本系列的功能迭代和问题修复，始终包含最新代码，但其中可能包含尚未完成完整发布测试的改动。
- `release/v4.0.1.x` 为正式发布标签，例如 `release/v4.0.1.1`、`release/v4.0.1.2`。每个标签均经过完整的测试与发布流程，可作为量产版本使用。
- BK7259 两轮车方案与 Armino SMP SDK 必须使用完全相同的版本标签。例如，方案代码使用 `release/v4.0.1.6` 时，SDK 也必须使用 `release/v4.0.1.6`。

建议客户选择 `release/v4.0.1.x` 系列中版本号最大的标签进行开发和量产。仅需体验最新功能或参与开发时，才建议使用 `release/v4.0.1` 分支。

## 获取代码

BK7259 两轮车方案和 Armino SMP SDK 均同步发布至 GitHub、Gitee 和 GitLab，可根据网络环境及访问权限选择代码源。

BK7259 两轮车方案：

- GitHub：[https://github.com/bekencorp/bk_solution_dashboard](https://github.com/bekencorp/bk_solution_dashboard)
- Gitee：[https://gitee.com/bekencorp/bk_solution_dashboard](https://gitee.com/bekencorp/bk_solution_dashboard)
- GitLab：[https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_dashboard](https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_dashboard)

Armino SMP SDK：

- GitHub：[https://github.com/bekencorp/bk_avdk_smp](https://github.com/bekencorp/bk_avdk_smp)
- Gitee：[https://gitee.com/bekencorp/bk_avdk_smp](https://gitee.com/bekencorp/bk_avdk_smp)
- GitLab：[https://gitlab.bekencorp.com/armino/bk_avdk_smp](https://gitlab.bekencorp.com/armino/bk_avdk_smp)

GitHub 和 Gitee 可公开访问。GitLab 仅面向企业客户开放；企业客户如需访问，请联系对接的 FAE 或销售人员申请开通权限。

**Windows 用户注意**：使用 Git for Windows 获取代码时，建议在克隆前关闭自动换行符转换，避免脚本或源文件被转换为 CRLF 而导致编译失败。Linux、macOS 和 WSL 环境无需执行。

```bash
git config --global core.autocrlf false
```

如果代码已经克隆，修改该配置不会自动恢复文件，建议设置后重新克隆。

以下命令以 GitHub 和 `release/v4.0.1.6` 标签为例。**实际获取代码时，请选择最新发布的标签，并确保方案代码与 SDK 使用完全相同的标签**。使用其他代码源时，替换对应的仓库地址即可。

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_avdk_smp.git

# BK7259 两轮车方案
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_solution_dashboard.git
```



## 编译环境安装

Armino SMP 支持本地编译和 Docker 编译，可根据开发平台选择以下方式部署编译环境。

### Linux 本地编译

进入 SDK 目录并运行环境安装脚本：

```bash
# 安装脚本位于 bk_avdk_smp 仓库内
cd ~/armino/bk_avdk_smp
sudo bash tools/env_tools/setup/armino_env_setup.sh
```



### Windows 本地编译

下载并安装 [Armino Bash](https://dl.bekencorp.com/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.3.0.exe)。

### Docker 编译

Docker 编译镜像为 [`bekencorp/armino-idk`](https://hub.docker.com/r/bekencorp/armino-idk/tags)，请选择 `1.5` 或更高版本的镜像标签，支持 Windows / Linux / macOS。

详细安装步骤请参阅 [Armino SMP 快速入门：环境部署及编译](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 编译工程

以 `scooter_1280_720_v2` 工程为例（位于 `projects/scooter_1280_720_v2`），通过 `SDK_DIR` 指向 Armino SMP SDK 后本地编译：

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_1280_720_v2
make bk7259 SDK_DIR=~/armino/bk_avdk_smp        # 也可先 export SDK_DIR 再执行 make bk7259
```

也支持 Docker 编译（Linux / macOS 用 `./dbuild.sh`，Windows PowerShell 用 `.\dbuild.ps1`）：

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_1280_720_v2
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

编译成功后，用于烧录的固件文件位于以下路径（相对于 `bk_solution_dashboard/` 仓库根目录）：

```text
projects/scooter_1280_720_v2/build/bk7259/scooter_1280_720_v2/package/all-app.bin
```

编译命令的详细说明请参阅 [两轮车方案在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/index.html)。

## 烧录固件

可选择以下任一方式烧录 BK7259 主控固件：

- 下载并使用 [BKFIL 本地烧录工具](https://dl.bekencorp.com/tools/bkfil/v4)
- 使用 [BKFIL 网页烧录工具](https://connect.aclsemi.com/)

烧录时请选择上一节编译生成的 `all-app.bin` 固件文件。

除 BK7259 主控固件外，BK3515N 蓝牙从控还需单独烧录对应固件，详细步骤请参阅在线文档。

详细烧录流程请参阅 [Armino SMP 快速入门](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 参考工程

| 工程名 | 主要功能 | 详细说明 |
| --- | --- | --- |
| [scooter_1280_720_v2](../projects/scooter_1280_720_v2/) | V2 产品工程（1280×720）：MIPI DSI + LVGL 仪表、开机 AVI、行车记录、手机 JPEG 投屏、BLE / Wi-Fi 配网、经典蓝牙音频、联系人与拨号、本地音乐、OTA 界面及物理按键交互。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/projects/scooter_1280_720_v2/index.html) |
| [scooter_dashboard_v1_1280_720_v2](../projects/scooter_dashboard_v1_1280_720_v2/) | Dashboard V1.0 1280×720 横屏：主页导航、Dashcam 循环录像与回放、SD 卡本地音乐（Music Player）、电话簿、Assist View、蓝牙电话、配网、SD 卡 MTP 及 FTP。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/projects/scooter_dashboard_v1_1280_720_v2/index.html) |
| [scooter_dashboard_v1_1024_600_v2](../projects/scooter_dashboard_v1_1024_600_v2/) | Dashboard V1.0 1024×600：主页导航、Dashcam 循环录像与回放、SD 卡本地音乐（Music Player）、电话簿、Assist View、蓝牙电话、配网、SD 卡 MTP；固件与 UI 资源不可与 1280×720 V2 混用。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/projects/scooter_dashboard_v1_1024_600_v2/index.html) |
| [scooter_dashboard_klok_1280_720](../projects/scooter_dashboard_klok_1280_720/) | Dashboard Klok 1280×720（ER68576B）：K 歌与 MV 播放演示，含首页/Klok 主页/歌曲列表/播放页、SD 卡音视频扫描播放、H.264/AAC 解码、全屏 Flexa 直显与按键/触摸/CLI 控制、SD 卡 MTP。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/projects/scooter_dashboard_klok_1280_720/index.html) |
| [scooter_dashboard_klok_fl7703_1280_720](../projects/scooter_dashboard_klok_fl7703_1280_720/) | Dashboard Klok 1280×720（FL7703）：功能与 `scooter_dashboard_klok_1280_720` 同类，固定用于 BK7259 + FL7703 板卡。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/zh_CN/v4.0.1/projects/scooter_dashboard_klok_fl7703_1280_720/index.html) |

## BEKEN 相关资源

- [BEKEN 官网](https://www.bekencorp.com/)
- [ARMINO 开发者论坛](https://armino.bekencorp.com/)
- [BEKEN 文档中心](https://docs.bekencorp.com/)
- 微信视频号: 博通集成电路

