# Armino Dashboard Two-Wheeler Solution

- [中文](./README_CN.md)

## Overview

This repository contains the Armino SMP based Dashboard two-wheeler solution. The main product project is
[`projects/scooter_1280_720`](./projects/scooter_1280_720), targeting BK7259.

The solution uses a BK7259 (AP, main controller) + BK3515N (Bluetooth secondary controller) dual-chip
architecture for a 1280x720 landscape dashboard. It currently supports:

- MIPI DSI + DPU + LVGL dashboard display;
- boot AVI playback;
- phone JPEG casting;
- BLE / Wi-Fi provisioning;
- Wi-Fi image transmission and BLE image transmission;
- classic Bluetooth audio, navigation voice, and multi-controller Bluetooth;
- SD card storage foundation for later recording features.

## Directory Layout

```text
.
├── components/                  # Display, casting, media, Bluetooth, provisioning components
├── docs/                        # Sphinx documentation
├── projects/
│   └── scooter_1280_720/        # Main two-wheeler product project
├── README_CN.md
├── README.md
└── 两轮车方案_BK7259_需求设计说明书.docx
```

## Build

Enter the main project directory and build the BK7259 firmware with the docker build script:

```bash
cd projects/scooter_1280_720
./dbuild.sh make bk7259
```

Windows PowerShell:

```powershell
cd projects/scooter_1280_720
.\dbuild.ps1 make bk7259
```

The build script uses the repository root as `SDK_DIR` by default. If the Armino SMP SDK is elsewhere, set it with an environment variable:

```bash
export SDK_DIR=/path/to/bk_avdk_smp
./dbuild.sh make bk7259
```

Build artifacts are generated under the project `build/` directory. Flash the generated BK7259 firmware to the main board.

## Entry Points

Main documents:

- [Product project guide](./projects/scooter_1280_720/README.md)
- [English documentation entry](./docs/bk7259/en/index.rst)
- [Dashboard App user guide](./docs/bk7259/en/projects/dashboard_app/index.rst)
- [Hardware reference](./docs/bk7259/en/hw-reference/index.rst)

The Dashboard App supports both Wi-Fi image transmission and BLE image transmission. Select BK7259, the image transmission mode, and image parameters in the App configuration center, then follow the documentation to provision, search for devices, and start navigation casting.

## Documentation Build

To build the local HTML documentation:

```bash
cd docs
make doc
```

The output is generated under `docs/bk7259/zh_CN/_build/latest` and `docs/bk7259/en/_build/latest`.
