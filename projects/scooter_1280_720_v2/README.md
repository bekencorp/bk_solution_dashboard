# scooter_1280_720_v2 Development Guide

- [中文](./README_CN.md)

## Overview

`scooter_1280_720_v2` is the V2 product project for the BK7259 two-wheeler smart dashboard,
targeting a 1280x720 landscape display. It uses a BK7259 (AP, main controller) + BK3515N
(Bluetooth secondary controller) dual-chip architecture and integrates **MIPI DSI + DPU + LVGL
dashboard display, boot AVI playback, dashcam recording, phone JPEG casting, BLE / Wi-Fi
provisioning, classic Bluetooth audio, contacts and dialing, local music, and an OTA UI**.

Compared with the base `scooter_1280_720` project, V2 adds a complete dashcam, contacts,
local music and OTA pages together with physical-key interaction.

## Directory Layout

```text
scooter_1280_720_v2/
├── ap/                         # Application processor (AP) app
│   ├── ap_main.c               # Board, display, Bluetooth and key initialization
│   ├── config/bk7259_ap/       # AP defconfig and GPIO configuration
│   ├── beken_generated/        # UI-designer generated pages, fonts and images
│   ├── dashcam/                # Preview, recording, storage, playback and assist view
│   ├── home/                   # Dashboard home page and Bluetooth status integration
│   ├── phone_book/             # Contacts, call history and dialing
│   ├── music_player/           # SD-card scan, playlist and playback control
│   ├── ota/                    # OTA progress UI
│   ├── CMakeLists.txt          # AP sources and component dependencies
│   └── Kconfig.projbuild       # Display, rotation and casting options
├── cp/                         # Co-processor (CP) app and configuration
├── partitions/bk7259/          # Partition table and RAM regions
├── Makefile / CMakeLists.txt   # Build entry
├── dbuild.sh / dbuild.ps1      # Docker build scripts
└── bk7259_bsp.ld               # Linker script
```

## Build

The target chip is `bk7259`. Build it through the Docker wrapper:

```bash
cd projects/scooter_1280_720_v2
./dbuild.sh make bk7259
```

On Windows PowerShell:

```powershell
cd projects/scooter_1280_720_v2
.\dbuild.ps1 make bk7259
```

- The wrapper uses `../..` (the repository root) as `SDK_DIR` by default. Override it with the
  `SDK_DIR` environment variable if necessary.
- Build artifacts are produced under the project `build/` directory. Flash the resulting firmware
  to BK7259.

## Key Configuration

Project options live mainly in `ap/config/bk7259_ap/defconfig` and `ap/Kconfig.projbuild`:

- `CONFIG_LCD_ER68576B_MIPI_720x1280=y`: ER68576B MIPI DSI panel.
- `CONFIG_SCOOTER_LVGL_HOR_RES=720`, `VER_RES=1280`: LVGL framebuffer resolution.
- `CONFIG_SCOOTER_UI_ROTATION=270`: rotation to a 1280x720 landscape display.
- `CONFIG_SCOOTER_UI_CANVAS_WIDTH=1280`, `HEIGHT=720`: UI-designer canvas size.
- `CONFIG_SCOOTER_CAST_JPEG_*`: casting source, destination and 90-degree rotation settings.
- `CONFIG_CSI_GC2053=y`, `CONFIG_CSI_CV2005=y`: dashcam camera support.
- `CONFIG_VIDEO_RECORDER=y`, `CONFIG_BK_ENCODER=y`: recording and encoding support.
- `CONFIG_BLUETOOTH_MULTI_CONTROLLER=y`: BK3515N secondary controller.
- `CONFIG_BK_BLE_PROVISIONING=y`: BLE network provisioning.
- `CONFIG_HFP_HF_DEMO=y`, `CONFIG_PBAP_CONTACTS=y`: hands-free calling and contact sync.
- `CONFIG_FATFS_SDCARD=y`, `CONFIG_SDCARD=y`: SD-card media and recording storage.

The BK3515N link uses UART1 by default. Reset is GPIO51, CTS / RTS are GPIO28 / GPIO29,
and the initial / negotiated baud rates are 115200 / 921600.

## Boot Flow

`main()` in `ap/ap_main.c` initializes the system in this order:

1. `bk_init()`: initialize the system;
2. `media_service_init()`: initialize media services;
3. `app_board_init()`: enable the LDO and initialize the frame buffer and optional touch panel;
4. `app_display_init()`: register the UI callback, initialize DPU / MIPI DSI / LCD, preload the
   home background while playing the boot AVI, start LVGL, register casting recovery hooks and
   initialize media devices;
5. `app_bt_init()`: create `ap_bt_startup_task` for MAC synchronization, BK3515N BSC setup,
   Bluetooth stack startup, media messaging, SDP, classic Bluetooth profiles and BLE provisioning;
6. `app_key_init()`: register physical-key actions.

The 1280x720 LVGL dashboard is shown after startup, and the background dashcam flow is started.

## Features

> Source paths below are relative to the solution root.

### Boot Animation and Dashboard

Before LVGL starts, `app_display_init()` calls `boot_avi_play()` to play the boot AVI from the
SD card. In parallel, `boot_bg_preload` decodes the home-page JPEG on a worker thread to avoid
synchronous decoding delays when the dashboard appears.

Main source files:

- `projects/scooter_1280_720_v2/ap/ap_main.c`: display initialization orchestration;
- `components/boot_avi_play/`: boot AVI playback and home background preload;
- `projects/scooter_1280_720_v2/ap/home/home_ui.c`: speed, lights, Bluetooth music and call state;
- `projects/scooter_1280_720_v2/ap/beken_generated/`: UI pages and assets.

### Dashcam

The dashcam provides background continuous segmented recording, an assist view, recording lists,
H.264 playback, storage management and an FTP server started after the network becomes ready.
Recordings are stored under `/sd0/dashcam`.

Main source files:

- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_app.c`: camera, recording and playback state machine;
- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_camera.c`: ISP / CSI camera management;
- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_recorder.c`: H.264 recording container;
- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_storage.c`: directory scanning, file naming,
  space accounting and old-file recycling;
- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_player.c`, `dashcam_video_v3.c`: decode,
  scaling and LVGL display;
- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_assitview.c`: turn assist view;
- `projects/scooter_1280_720_v2/ap/dashcam/dashcam_ui.c`: recording list and key interaction.

Defaults are defined in `dashcam/dashcam_config.h`: 960x540 preview, 1280x720 recording at
25 fps, 960x540 playback and 60-second segments. Development builds retain up to 1000 files.
Release builds can use free-space-based circular recording that removes the oldest files first.

### Phone Casting, Navigation and Provisioning

The phone App discovers the device through SDP and establishes CTRL / VIDEO / AUDIO channels.
JPEG frames are decoded and rotated by `jpeg_stream_pipeline` before display. On disconnect,
the casting path is stopped, LVGL is restored and SDP advertising restarts.

The BLE boarding channel also carries Wi-Fi provisioning, navigation control and chunked
navigation-file transfer:

- opcode 50: file transfer control;
- opcode 51: file data chunk;
- opcode 52: start / stop navigation casting;
- opcode 53: select BT / Wi-Fi navigation type.

The implementation is under `components/media/`, `components/network_provisioning/`,
`components/cast_pipeline/` and `components/display_ui/`.

### Bluetooth Music, Calls and Contacts

- A2DP Sink / AVRCP supports phone audio, volume and playback control.
- HFP HF provides SCO hands-free calls; call audio takes priority over background music.
- PBAP synchronizes phone contacts into a local cache.
- `ap/phone_book/phone_book_ui.c` displays contacts and call history and supports key-driven dialing.
- `ap/home/home_ui.c` reflects track metadata, playback state and incoming / outgoing calls on
  the dashboard.

### Local Music

`ap/music_player/music_player_ui.c` scans `/sd0/Music` for MP3, WAV, FLAC, M4A, AAC, OGG and
APE files and reads available metadata. The current player registers MP3, WAV and AAC decoders
and provides a playlist, previous / next, pause / resume, sequence-loop and single-loop controls.

### OTA UI

`ap/ota/ota_ui.c` manages OTA page entry, exit and progress animation. It currently provides the
upgrade entry and progress UI; the corresponding OTA service must be integrated for real upgrades.

## Physical Keys

Default key actions:

- Up: answer a call; double press to hang up.
- Left: erase network provisioning data and reboot.
- Right: erase network provisioning data.
- Middle: short, double and long presses are delegated to the active page for navigation, focus,
  confirmation or return.

## CLI Commands

The project exports the `dashboard` command through `.cli_cmdtabl`:

```text
dashboard np_erase [reboot]        # Erase saved BLE provisioning data; optionally reboot
dashboard np_start_advertise       # Restart BLE provisioning advertising
dashboard dashcam                  # Open the dashcam page
dashboard phone_book               # Open the contacts page
dashboard pet_toggle               # Simulate a middle-key short press
dashboard pet_double               # Simulate a middle-key double press
dashboard pet_enter                # Simulate a middle-key long press
dashboard dashcam_count            # Show recording count and SD-card free space
dashboard dashcam_trim [target]    # Delete oldest recordings down to the target count
dashboard dashcam_rec_start        # Start background recording
dashboard dashcam_rec_stop         # Stop background recording
dashboard dashcam_turn_left        # Start the turn assist view
dashboard dashcam_turn_off         # Stop the turn assist view
```
