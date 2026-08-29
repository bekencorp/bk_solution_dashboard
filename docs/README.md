# Beken BK7258 Two-Wheeler Solution

- [中文](./README_CN.md)

## Overview

**BK7258 Two-Wheeler Solution** is an open-source turnkey two-wheeler smart dashboard solution released by Beken. The solution is based on the **BK7258** main control chip, relies on the Armino base SDK **BK_AVDK_SMP**, and adopts a BK7258 + BK3515NS dual-chip architecture. Targeting dashboard screens of various resolutions, it supports LVGL dashboard UI display, Wi-Fi image transmission display, phone navigation casting, navigation voice and prompt tone playback, classic Bluetooth audio, BLE, Wi-Fi provisioning, a second Bluetooth controller, as well as dash cam and AVI video playback.

## Documentation

- [BK7258 Two-Wheeler Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/index.html)
- [Armino SMP SDK (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html)

## Hardware

The hardware for this solution uses the two-wheeler solution development board. The board is built around the BK7258 main control chip and the BK3515NS Bluetooth secondary control chip, connects to displays of various resolutions such as 480x272, 800x480, and 1024x600 through an LCD adapter board, and integrates SD card storage, a UVC camera interface, and a reset button.

- [Development board hardware materials](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/hw-reference/index.html)
- [BK7258 Datasheet](https://docs.bekencorp.com/spec/BK7258/BK7258%C2%A0Datasheet.pdf)

Development kit purchase link: coming soon.

## Version Strategy

This solution uses a "maintenance branch + release tag" version management approach:

- `release/v3.1.1` is the ongoing maintenance branch, used for feature iteration and bug fixes in this version series. It always contains the latest code, but it may include changes that have not yet completed the full release testing.
- `release/v3.1.1.x` are official release tags, such as `release/v3.1.1.7` and `release/v3.1.1.8`. Each tag has gone through the complete testing and release process and can be used as a mass-production version.
- The BK7258 Two-Wheeler Solution and the Armino SMP SDK must use exactly the same version tag. For example, when the solution code uses `release/v3.1.1.8`, the SDK must also use `release/v3.1.1.8`.

We recommend that customers choose the tag with the highest version number in the `release/v3.1.1.x` series for which both the solution and the SDK have been released, for development and mass production. Only use the `release/v3.1.1` branch when you just want to try the latest features or participate in development.

## Get the Code

The BK7258 Two-Wheeler Solution and the Armino SMP SDK are released simultaneously to GitHub, Gitee, and GitLab. Choose the code source based on your network environment and access permissions.

BK7258 Two-Wheeler Solution:

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

The following commands use GitHub and the `release/v3.1.1.8` tag as an example. **When actually getting the code, choose the latest released tag and make sure the solution code and the SDK use exactly the same tag**. When using another code source, replace the repository URL accordingly.

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_avdk_smp.git

# BK7258 Two-Wheeler Solution
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_solution_dashboard.git
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

For detailed setup steps, see [Armino SMP Quick Start: Environment deployment and build](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Build the Project

Taking the `scooter_800_480` project as an example (located in `projects/scooter_800_480`), point `SDK_DIR` to the Armino SMP SDK and build locally:

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_800_480
make bk7258 SDK_DIR=~/armino/bk_avdk_smp        # or export SDK_DIR first, then run make bk7258
```

Docker build is also supported (use `./dbuild.sh` on Linux / macOS, and `.\dbuild.ps1` on Windows PowerShell):

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_800_480
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7258
```

After a successful build, the firmware file used for flashing is located at the following path (relative to the `bk_solution_dashboard/` repository root):

```text
projects/scooter_800_480/build/bk7258/scooter_800_480/package/all-app.bin
```

For other projects, replace the path in the commands with the corresponding `projects/<project_name>`. For detailed descriptions of the build commands, see [Two-Wheeler Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/index.html).

## Flash the Firmware

The BK7258 main control firmware can be flashed over the serial port by downloading and using the [BKFIL local flashing tool](https://dl.bekencorp.com/tools/flash/). When flashing, select the `all-app.bin` firmware file generated in the previous section.

In addition to the BK7258 main control firmware, the BK3515NS Bluetooth secondary controller requires flashing its own firmware separately (firmware directory: [https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller](https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller)). For the detailed steps, see [BK7258 Two-Wheeler Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/index.html).

For the detailed flashing process, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Reference Projects

| Project | Main features | Details |
| --- | --- | --- |
| [scooter_480_272](../projects/scooter_480_272/) | 480x272 dashboard project: Wi-Fi image transmission and phone navigation casting display, classic Bluetooth audio and HFP hands-free, BLE, Wi-Fi provisioning; adapted for the V1.0 LCD adapter board. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/scooter_480_272/index.html) |
| [scooter_480_272_v2](../projects/scooter_480_272_v2/) | Same features as `scooter_480_272`, adapted for the CAN-capable V1.1 LCD adapter board. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/scooter_480_272_v2/index.html) |
| [scooter_800_480](../projects/scooter_800_480/) | 800x480 dashboard project: Wi-Fi image transmission and phone navigation casting display, classic Bluetooth audio and HFP hands-free, BLE, Wi-Fi provisioning; adapted for the V1.0 LCD adapter board. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/scooter_800_480/index.html) |
| [scooter_800_480_v2](../projects/scooter_800_480_v2/) | Same features as `scooter_800_480`, adapted for the CAN-capable V1.1 LCD adapter board. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/scooter_800_480_v2/index.html) |
| [scooter_1024_600](../projects/scooter_1024_600/) | 1024x600 dashboard project: Wi-Fi image transmission and phone navigation casting display, classic Bluetooth audio and HFP hands-free, BLE, Wi-Fi provisioning. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/scooter_1024_600/index.html) |
| [scooter_uvc_example](../projects/scooter_uvc_example/) | UVC example project: validates UVC device access, preview display, and the decode-display pipeline; navigation casting is not yet supported. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/scooter_uvc_example/index.html) |

For the feature scope and selection notes of each project, see the [example projects](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/projects/index.html) page in the online documentation.

## Beken Resources

- [Beken official website](https://www.bekencorp.com/)
- [Armino developer forum](https://armino.bekencorp.com/)
- [Beken documentation center](https://docs.bekencorp.com/)
- Bilibili: Beken Product Solutions
