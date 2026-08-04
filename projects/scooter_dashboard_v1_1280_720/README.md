# scooter_1280_720 Development Guide

* [中文](./README_CN.md)

## Overview

`scooter_1280_720` is the main product project of the two-wheeler solution (BK7259), targeting the
1280x720 display profile. It is built on a BK7259 (AP, main controller) + BK3515 (Bluetooth secondary
controller) dual-chip architecture. The project already implements baseline capabilities such as
**MIPI DSI + DPU + LVGL dashboard display, boot AVI playback, phone JPEG casting, BLE / Wi-Fi
provisioning, classic Bluetooth audio and multi-controller Bluetooth**, serving as the foundation for the
upcoming "front/rear dual-UVC dual-recording" product.


## Directory Layout

```
scooter_1280_720/
├── ap/                       # Application processor (AP) app
│   ├── ap_main.c             # Main entry: board/display/Bluetooth/CLI init orchestration
│   ├── boot_avi_play.c/.h    # Boot AVI playback
│   ├── config/bk7259_ap/     # AP defconfig and GPIO config
│   ├── beken_generated/      # UI-designer generated code (pages, fonts, images, pet GIF page, ...)
│   ├── assets/               # Image assets
│   └── Kconfig.projbuild     # Project options (display resolution, rotation, cast crop, ...)
├── cp/                       # Co-processor (CP) app and config
├── partitions/bk7259/        # Partition table and RAM regions
├── Makefile / CMakeLists.txt # Build entry
├── dbuild.sh / dbuild.ps1    # docker build scripts
└── bk7259_bsp.ld             # Linker script
```

## Build

Target chip is `bk7259`, built via the docker build script:

```bash
cd projects/scooter_1280_720
./dbuild.sh make bk7259        # Linux / macOS
# Windows PowerShell:
# .\dbuild.ps1 make bk7259
```

- The build script uses `../..` (repo root) as `SDK_DIR` by default; override it via the `SDK_DIR`
  environment variable if needed.
- Build artifacts are produced under the project `build/` directory; flash the resulting firmware to BK7259.

## Key Configuration

Options live mainly in `ap/config/bk7259_ap/defconfig` and `ap/Kconfig.projbuild`:

| Config | Default | Description |
| --- | --- | --- |
| `CONFIG_LCD_HX8394F_MIPI_720x1280` | y | MIPI DSI panel model (720x1280) |
| `CONFIG_SCOOTER_LVGL_HOR_RES` / `VER_RES` | 720 / 1280 | LVGL framebuffer resolution |
| `CONFIG_SCOOTER_UI_ROTATION` | 270 | UI rotation (rotated to landscape 1280x720) |
| `CONFIG_SCOOTER_UI_CANVAS_WIDTH` / `HEIGHT` | 1280 / 720 | UI designer canvas size |
| `CONFIG_BLUETOOTH_MULTI_CONTROLLER` | y | Enable multi-controller (BK3515 secondary) |
| `CONFIG_BK_BLE_PROVISIONING` | y | Enable BLE provisioning |
| `CONFIG_A2DP_SINK_DEMO` / `CONFIG_HFP_HF_DEMO` | y | Classic Bluetooth audio / hands-free |
| `CONFIG_MEDIA_RECEIVE_DEMO` | y | Phone casting / SDP discovery |
| `CONFIG_FATFS_SDCARD` / `CONFIG_SDCARD` | y | SD card storage (boot media, recording) |

BK3515 secondary-controller UART link pins (defaults, adjustable in defconfig):

| Config | Default |
| --- | --- |
| `CONFIG_BLUETOOTH_BSC_UART_ID` | 1 |
| `CONFIG_BLUETOOTH_BSC_RESET_PIN` | 51 |
| `CONFIG_BLUETOOTH_BSC_CTS_PIN` / `RTS_PIN` | 28 / 29 |
| `CONFIG_BLUETOOTH_BSC_INIT_BAUD` / `NEGO_BAUD` | 115200 / 921600 |

## Boot Flow

`main()` in `ap/ap_main.c` initializes in this order:

1. `bk_init()`: system init;
2. `media_service_init()`: media service init;
3. `app_board_init()`: enable LDO, init frame buffer (and optional TP);
4. `app_display_init()`: init display hardware (DPU / MIPI DSI / LCD), play boot AVI, start LVGL, register cast recovery hooks;
5. `app_bt_init()`: create the dedicated `ap_bt_startup_task` thread, performing MAC sync, BK3515 BSC init,
   `bk_bluetooth_init()`, media messaging, SDP discovery, classic Bluetooth profiles, and BLE provisioning init;
6. `app_cli_init()`: init CLI commands and key service.

After boot, the screen defaults to the LVGL dashboard / home page (display resolution 1280x720).

## Feature Demos

> All "Source files" paths in the following sections are relative to the repository root.

### Boot animation

After boot, `boot_avi_play()` is invoked inside `app_display_init()` before LVGL starts, playing the
boot AVI from the SD card; the LVGL dashboard page is started afterwards.

Source files:

- `projects/scooter_1280_720/ap/ap_main.c` — `app_display_init()` runs
  `display_ui_init_display_hw() → boot_avi_play() → display_ui_start_lvgl()` in order;
- `projects/scooter_1280_720/ap/boot_avi_play.c` — boot animation main implementation;
- `components/jpeg_stream_pipeline/src/jpeg_stream_pipeline.c` — JPEG decode / scale / rotate pipeline.

Flow:

1. `sdcard_mount()` mounts the SD card and `f_stat()` looks up the fixed path `1:/boot.avi`
   (boot animation is skipped if absent);
2. The AVI is parsed via `AVI_open_input_file()` from `modules/avilib.h`, reading MJPEG frames one by one;
3. Frame fix-ups: MJPEG frames often lack DHT / QT1 which the hardware decoder (VCDEC) requires, so they
   are injected by `jpeg_has_dht()` + the built-in `s_mjpeg_dht` table and `jpeg_inject_qt1_if_missing()`;
4. When the AVI is portrait (`avi_w == dpu_h && avi_h == dpu_w`), the pipeline sets `rotate_degree = 90`
   for GPU rotation;
5. Decoded frames are flushed to the screen via `bk_display_flush()` in `boot_frame_display_cb()`. During
   playback the DPU is temporarily switched to `ARGB8888 + decompress`
   (`dpu_switch_to_argb8888_decompress()`) and restored afterwards (`dpu_restore_rgb565()`).

### Phone casting (JPEG)

The project provides a LAN JPEG casting loop: the phone App discovers the device over SDP, connects, and
pushes a JPEG video stream that is decoded and displayed locally.

Source files:

- `projects/scooter_1280_720/ap/ap_main.c` — `ap_bt_startup_task()` starts the media thread and SDP
  broadcast via `media_msg_init()` and `ntwk_sdp_start("media-tcp", ...)`; `app_display_init()` registers
  the cast display hooks via `display_ui_register_cast_hooks_once()`;
- `components/media/src/media_network_transfer.c` — CTRL / VIDEO / AUDIO network channels;
- `components/media/src/media_msg.c` — media event state machine `media_message_handle()`;
- `components/media/include/media_msg.h` — `MEDIA_EVT_*` event definitions;
- `components/media/src/media_devices.c` — `av_server_jpeg_decode_manager_turn_on/off()`;
- `components/cast_pipeline/cast_jpeg_pipeline.c` — cast JPEG decode/display pipeline;
- `components/display_ui/display_ui_cast_hooks.c` — display hooks that stop LVGL / switch DPU / restore LVGL during casting.

Flow:

1. The App discovers the device and sets up CTRL / VIDEO / AUDIO channels (`media_network_transfer.c`);
2. Once the control channel connects, `media_network_transfer.c` posts
   `MEDIA_EVT_REMOTE_DEVICE_CONNECTED`; the state machine in `media_msg.c` calls
   `av_server_jpeg_decode_manager_turn_on()` (`media_devices.c`) →
   `cast_jpeg_pipeline_turn_on()` (`cast_jpeg_pipeline.c`) to start the decode/display path;
3. Video data is pushed via `cast_jpeg_pipeline_push_frame_buffer()` in `media_network_transfer.c`
   into `jpeg_stream_pipeline` for decode / rotation, then flushed to screen via `bk_display_flush()`;
4. On disconnect, `MEDIA_EVT_CAST_SESSION_TEARDOWN` is posted; `media_cast_lan_session_teardown()`
   calls `av_server_jpeg_decode_manager_turn_off()` → `cast_jpeg_pipeline_turn_off()` to tear down the
   cast pipeline, restore LVGL and restart the SDP broadcast.

### BLE provisioning & navigation control

Source files:

- `projects/scooter_1280_720/ap/ap_main.c` — `ap_bt_startup_task()` calls `bk_sl_np_init()` to start the provisioning service;
- `components/network_provisioning/network_provisioning.c` — provisioning and navigation control logic;
- `components/network_provisioning/network_provisioning.h` — opcode and data structure definitions;
- `components/bluetooth_dual/wifi_boarding/wifi_boarding_demo_service.c` — BLE boarding GATT service and message dispatch;
- `components/media/src/media_navigation_transfer.c` — navigation file chunk receive and upper-layer push.

Capabilities:

- BLE provisioning supports device discovery, STA / AP / P2P configuration and chunked Wi-Fi scan reporting;
- Navigation casting and file transfer are extended over the boarding channel; opcodes are defined in `network_provisioning.h`.

Opcode definitions:

| Opcode | Enum | Function |
| --- | --- | --- |
| 50 | `BOARDING_OP_TRANSFER_FILE_CONTROL` | File transfer control (start / stop / complete) |
| 51 | `BOARDING_OP_TRANSFER_FILE_DATA` | File chunk transfer by packet num |
| 52 | `BOARDING_OP_NAVIGATION_CONTROL` | Navigation casting start / stop |
| 53 | `BOARDING_OP_NAVIGATION_TYPE_CONTROL` | Navigation type selection (BT / Wi-Fi) |

- Opcode 52 is handled by `handle_navigation_control_msg()` in `network_provisioning.c`; START / STOP
  respectively call `av_server_jpeg_decode_manager_turn_on()` / `..._turn_off()`, reusing the same JPEG
  decode/display path as phone casting;
- Opcode 50/51 file transfer is handled by `media_navigation_transfer_begin()` / `..._push()` /
  `..._cancel()` (`media_navigation_transfer.c`).

### Classic Bluetooth audio

Source files:

- `projects/scooter_1280_720/ap/ap_main.c` — `ap_bt_app_init()` calls `a2dp_sink_demo_init(0, 1)` and
  `hfp_hf_demo_init(0)` for profile init;
- `components/bluetooth_dual/a2dp_sink/a2dp_sink_demo.c` — A2DP Sink / AVRCP implementation;
- `components/bluetooth_dual/hfp_hf/hfp_hf_demo.c` — HFP HF hands-free call implementation;
- `components/blue_audio_player_service/`, `components/blue_audio_recorder_service/` — player / recorder services.

Capabilities:

- A2DP Sink plays phone audio, AVRCP provides volume / playback control; on the audio-start event A2DP
  creates the player service per codec (SBC / AAC) and writes audio frames into `blue_audio_player_service`;
- HFP HF hands-free calls (SCO, CVSD / mSBC); uplink mic capture is sent to the phone via
  `blue_audio_recorder_service`, and call audio takes priority over A2DP background music.

### Pet GIF
`projects/scooter_1280_720/ap/beken_generated/pet_page.c` loads a pet GIF page from the SD card.

## CLI Commands

The project registers the `widgets` command (see `cli_widgets_cmd`):

```text
ap_cmd widgets np_erase [reboot]        # Erase saved BLE provisioning info (optionally reboot afterwards)
ap_cmd widgets np_start_advertise       # Restart BLE provisioning advertising
```
