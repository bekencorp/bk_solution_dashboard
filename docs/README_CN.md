# 博通集成 BK7258 两轮车解决方案

- [English](./README.md)

## 概述

**BK7258 两轮车解决方案**是 BEKEN 发布并开源的两轮车智能仪表整机方案。该方案基于 **BK7258** 主控芯片，依赖 Armino 基础 SDK **BK_AVDK_SMP**，采用 BK7258 与 BK3515NS 双芯片架构，面向多种分辨率的仪表屏，支持 LVGL 仪表界面显示、Wi-Fi 图传显示、手机导航投屏、导航语音与提示音播放、经典蓝牙音频、BLE、Wi-Fi 配网、第二蓝牙控制器，以及行车记录仪、AVI 视频播放等能力。

## 文档

- [BK7258 两轮车方案在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/index.html)
- [Armino SMP SDK（BK AVDK SMP）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html)

## 硬件

本方案硬件使用两轮车方案开发板。该开发板采用 BK7258 主控芯片与 BK3515NS 蓝牙从控芯片，通过 LCD 转接板连接 480x272、800x480、1024x600 等多种分辨率的显示屏，并集成 SD 卡存储、UVC 摄像头接口，以及复位按键。

- [开发板硬件资料](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/hw-reference/index.html)
- [BK7258 Datasheet](https://docs.bekencorp.com/spec/BK7258/BK7258%C2%A0Datasheet.pdf)

开发套件购买链接：即将上架。

## 版本策略

本方案采用“维护分支 + 发布标签（Tag）”的版本管理方式：

- `release/v3.1.1` 为持续维护分支，用于该版本系列的功能迭代和问题修复，始终包含最新代码，但其中可能包含尚未完成完整发布测试的改动。
- `release/v3.1.1.x` 为正式发布标签，例如 `release/v3.1.1.7`、`release/v3.1.1.8`。每个标签均经过完整的测试与发布流程，可作为量产版本使用。
- BK7258 两轮车方案与 Armino SMP SDK 必须使用完全相同的版本标签。例如，方案代码使用 `release/v3.1.1.8` 时，SDK 也必须使用 `release/v3.1.1.8`。

建议客户选择 `release/v3.1.1.x` 系列中方案与 SDK 均已发布、版本号最大的标签进行开发和量产。仅需体验最新功能或参与开发时，才建议使用 `release/v3.1.1` 分支。

## 获取代码

BK7258 两轮车方案和 Armino SMP SDK 均同步发布至 GitHub、Gitee 和 GitLab，可根据网络环境及访问权限选择代码源。

BK7258 两轮车方案：

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

以下命令以 GitHub 和 `release/v3.1.1.8` 标签为例。**实际获取代码时，请选择最新发布的标签，并确保方案代码与 SDK 使用完全相同的标签**。使用其他代码源时，替换对应的仓库地址即可。

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_avdk_smp.git

# BK7258 两轮车方案
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_solution_dashboard.git
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

详细安装步骤请参阅 [Armino SMP 快速入门：环境部署及编译](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html)。

## 编译工程

以 `scooter_800_480` 工程为例（位于 `projects/scooter_800_480`），通过 `SDK_DIR` 指向 Armino SMP SDK 后本地编译：

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_800_480
make bk7258 SDK_DIR=~/armino/bk_avdk_smp        # 也可先 export SDK_DIR 再执行 make bk7258
```

也支持 Docker 编译（Linux / macOS 用 `./dbuild.sh`，Windows PowerShell 用 `.\dbuild.ps1`）：

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_800_480
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7258
```

编译成功后，用于烧录的固件文件位于以下路径（相对于 `bk_solution_dashboard/` 仓库根目录）：

```text
projects/scooter_800_480/build/bk7258/scooter_800_480/package/all-app.bin
```

其他工程请将命令中的路径替换为对应的 `projects/<工程名>`。编译命令的详细说明请参阅 [两轮车方案在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/index.html)。

## 烧录固件

BK7258 主控固件可下载并使用 [BKFIL 本地烧录工具](https://dl.bekencorp.com/tools/flash/) 通过串口烧录，烧录时请选择上一节编译生成的 `all-app.bin` 固件文件。

除 BK7258 主控固件外，BK3515NS 蓝牙从控还需单独烧录对应固件（固件目录：[https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller](https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller)），详细步骤请参阅 [BK7258 两轮车方案在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/index.html)。

详细烧录流程请参阅 [Armino SMP 快速入门](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html)。

## 参考工程

| 工程名                                                     | 主要功能                                                                           | 详细说明                                                                                                                           |
| ------------------------------------------------------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| [scooter_480_272](../projects/scooter_480_272/)         | 480x272 分辨率仪表工程：Wi-Fi 图传与手机导航投屏显示、经典蓝牙音频与 HFP 免提、BLE、Wi-Fi 配网；适配 V1.0 LCD 转接板。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/scooter_480_272/index.html)     |
| [scooter_480_272_v2](../projects/scooter_480_272_v2/)   | 功能同 `scooter_480_272`，适配支持 CAN 的 V1.1 LCD 转接板。                                 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/scooter_480_272_v2/index.html)  |
| [scooter_800_480](../projects/scooter_800_480/)         | 800x480 分辨率仪表工程：Wi-Fi 图传与手机导航投屏显示、经典蓝牙音频与 HFP 免提、BLE、Wi-Fi 配网；适配 V1.0 LCD 转接板。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/scooter_800_480/index.html)     |
| [scooter_800_480_v2](../projects/scooter_800_480_v2/)   | 功能同 `scooter_800_480`，适配支持 CAN 的 V1.1 LCD 转接板。                                 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/scooter_800_480_v2/index.html)  |
| [scooter_1024_600](../projects/scooter_1024_600/)       | 1024x600 分辨率仪表工程：Wi-Fi 图传与手机导航投屏显示、经典蓝牙音频与 HFP 免提、BLE、Wi-Fi 配网。                | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/scooter_1024_600/index.html)    |
| [scooter_uvc_example](../projects/scooter_uvc_example/) | UVC 示例工程：验证 UVC 设备接入、预览显示与解码显示链路，暂不支持导航投屏。                                     | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/scooter_uvc_example/index.html) |

各工程的功能范围与选型说明详见在线 [示例工程](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/projects/index.html) 页面。

## BEKEN 相关资源

- [BEKEN 官网](https://www.bekencorp.com/)
- [ARMINO 开发者论坛](https://armino.bekencorp.com/)
- [BEKEN 文档中心](https://docs.bekencorp.com/)
- 哔哩哔哩: 博通集成产品解决方案

