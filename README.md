# Beken BK7259 Two-Wheeler Solution

- [中文](./README_CN.md)

## Overview

**BK7259 Two-Wheeler Solution** is an open-source turnkey two-wheeler smart dashboard solution released by Beken. The solution is based on the **BK7259** main control chip, relies on the Armino base SDK **BK_AVDK_SMP**, and adopts a BK7259 + BK3515N dual-chip architecture. It supports MIPI DSI + DPU + LVGL dashboard display, boot AVI playback, phone JPEG casting, BLE / Wi-Fi provisioning, Wi-Fi image transmission and BLE image transmission, classic Bluetooth audio, navigation voice, and multi-controller Bluetooth, as well as SD card storage, and provides a complete end-to-end example project.

## Documentation

- [BK7259 Two-Wheeler Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/index.html)
- [Armino SMP SDK (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/index.html)

## Hardware

The hardware for this solution uses the BK7259 two-wheeler development board (EVB), which is built around the BK7259 main control chip and the BK3515N Bluetooth secondary control chip, and integrates a 1280x720 landscape MIPI DSI display, SD card storage, and buttons such as reset and provisioning reset.

- [BK7259 development board hardware materials](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/hw-reference/index.html)
- [BK7259 Datasheet](https://docs.bekencorp.com/spec/BK7259/BK7259_Datasheet.pdf)

Development kit purchase link: coming soon.

## Version Strategy

This solution uses a "maintenance branch + release tag" version management approach:

- `release/v4.0.1` is the ongoing maintenance branch, used for feature iteration and bug fixes in this version series. It always contains the latest code, but it may include changes that have not yet completed the full release testing.
- `release/v4.0.1.x` are official release tags, such as `release/v4.0.1.1` and `release/v4.0.1.2`. Each tag has gone through the complete testing and release process and can be used as a mass-production version.
- The BK7259 Two-Wheeler Solution and the Armino SMP SDK must use exactly the same version tag. For example, when the solution code uses `release/v4.0.1.6`, the SDK must also use `release/v4.0.1.6`.

We recommend that customers choose the tag with the highest version number in the `release/v4.0.1.x` series for development and mass production. Only use the `release/v4.0.1` branch when you just want to try the latest features or participate in development.

## Get the Code

The BK7259 Two-Wheeler Solution and the Armino SMP SDK are released simultaneously to GitHub, Gitee, and GitLab. Choose the code source based on your network environment and access permissions.

BK7259 Two-Wheeler Solution:

- GitHub: [https://github.com/bekencorp/bk_solution_dashboard](https://github.com/bekencorp/bk_solution_dashboard)
- Gitee: [https://gitee.com/bekencorp/bk_solution_dashboard](https://gitee.com/bekencorp/bk_solution_dashboard)
- GitLab: [https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_dashboard](https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_dashboard)

Armino SMP SDK:

- GitHub: [https://github.com/bekencorp/bk_avdk_smp](https://github.com/bekencorp/bk_avdk_smp)
- Gitee: [https://gitee.com/bekencorp/bk_avdk_smp](https://gitee.com/bekencorp/bk_avdk_smp)
- GitLab: [https://gitlab.bekencorp.com/armino/bk_avdk_smp](https://gitlab.bekencorp.com/armino/bk_avdk_smp)

GitHub and Gitee are publicly accessible. GitLab is available only to enterprise customers; enterprise customers who need access should contact their FAE or sales representative to apply for permission.

**Note for Windows users**: When using Git for Windows to get the code, we recommend disabling automatic line-ending conversion before cloning, to avoid scripts or source files being converted to CRLF and causing build failures. This is not required on Linux, macOS, or WSL.

```bash
git config --global core.autocrlf false
```

If the code has already been cloned, changing this setting will not automatically restore the files, so we recommend re-cloning after setting it.

The following commands use GitHub and the `release/v4.0.1.6` tag as an example. **When actually getting the code, choose the latest released tag and make sure the solution code and the SDK use exactly the same tag**. When using another code source, replace the repository URL accordingly.

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_avdk_smp.git

# BK7259 Two-Wheeler Solution
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_solution_dashboard.git
```

## Set Up the Build Environment

Armino SMP supports local build and Docker build. Choose one of the following methods to set up the build environment based on your development platform.

### Local Build on Linux

Enter the SDK directory and run the environment setup script:

```bash
# The setup script is located in the bk_avdk_smp repository
cd ~/armino/bk_avdk_smp
sudo bash tools/env_tools/setup/armino_env_setup.sh
```

### Local Build on Windows

Download and install [Armino Bash](https://dl.bekencorp.com/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.3.0.exe).

### Docker Build

The Docker build image is [`bekencorp/armino-idk`](https://hub.docker.com/r/bekencorp/armino-idk/tags). Choose an image tag of version `1.5` or higher; it supports Windows / Linux / macOS.

For detailed setup steps, see [Armino SMP Quick Start: Environment deployment and build](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Build the Project

Taking the `scooter_1280_720` project as an example (located in `projects/scooter_1280_720`), point `SDK_DIR` to the Armino SMP SDK and build locally:

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_1280_720
make bk7259 SDK_DIR=~/armino/bk_avdk_smp        # or export SDK_DIR first, then run make bk7259
```

Docker build is also supported (use `./dbuild.sh` on Linux / macOS, and `.\dbuild.ps1` on Windows PowerShell):

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_1280_720
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

After a successful build, the firmware file used for flashing is located at the following path (relative to the `bk_solution_dashboard/` repository root):

```text
projects/scooter_1280_720/build/bk7259/scooter_1280_720/package/all-app.bin
```

For detailed descriptions of the build commands, see [Two-Wheeler Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/index.html).

## Flash the Firmware

You can flash the BK7259 main control firmware using either of the following methods:

- Download and use the [BKFIL local flashing tool](https://dl.bekencorp.com/tools/bkfil/v4)
- Use the [BKFIL web flashing tool](https://connect.aclsemi.com/)

When flashing, select the `all-app.bin` firmware file generated in the previous section.

In addition to the BK7259 main control firmware, the BK3515N Bluetooth secondary controller requires flashing its own firmware separately. For the detailed steps, see the online documentation.

For the detailed flashing process, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Reference Projects

| Project | Main features | Details |
| --- | --- | --- |
| [scooter_1280_720](../projects/scooter_1280_720/) | 1280x720 main product entry: dashboard display, boot AVI, phone JPEG casting, BLE / Wi-Fi provisioning, classic Bluetooth audio, multi-controller Bluetooth, and SD card storage; foundation for the upcoming dual-UVC dual-recording product. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/projects/scooter_1280_720/index.html) |
| [scooter_1280_720_v2](../projects/scooter_1280_720_v2/) | V2 product project: adds dashcam recording, contacts/dialing, local music, OTA UI, and the corresponding physical-key interactions on top of scooter_1280_720. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/projects/scooter_1280_720_v2/index.html) |
| [scooter_dashboard_v1_1280_720](../projects/scooter_dashboard_v1_1280_720/) | Dashboard V1.0 hardware base demo: 1280×720 landscape dashboard, boot AVI, speed/turn-signal demo, pet GIF page, phone casting, provisioning, classic Bluetooth, and SD card MTP. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/projects/scooter_dashboard_v1_1280_720/index.html) |
| [scooter_dashboard_v1_1280_720_v2](../projects/scooter_dashboard_v1_1280_720_v2/) | Dashboard V1.0 1280×720 landscape: home navigation, dashcam loop recording/playback, local SD-card music (Music Player), phone book, Assist View, Bluetooth calls, provisioning, SD card MTP, and FTP. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/projects/scooter_dashboard_v1_1280_720_v2/index.html) |
| [scooter_dashboard_v1_1024_600_v2](../projects/scooter_dashboard_v1_1024_600_v2/) | Dashboard V1.0 1024×600: home navigation, dashcam loop recording/playback, local SD-card music (Music Player), phone book, Assist View, Bluetooth calls, provisioning, and SD card MTP; do not mix firmware or UI resources with the 1280×720 V2 project. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/projects/scooter_dashboard_v1_1024_600_v2/index.html) |
| [scooter_dashboard_klok_1280_720](../projects/scooter_dashboard_klok_1280_720/) | Dashboard Klok 1280×720: karaoke and MV playback demo with home/Klok main/song list/playback pages, SD-card A/V scan and play, H.264/AAC decode, full-screen Flexa display with key/touch/CLI control, and SD card MTP. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7259/en/v4.0.1/projects/scooter_dashboard_klok_1280_720/index.html) |

## Beken Resources

- [Beken official website](https://www.bekencorp.com/)
- [Armino developer forum](https://armino.bekencorp.com/)
- [Beken documentation center](https://docs.bekencorp.com/)
- WeChat Channels: Beken Corporation
