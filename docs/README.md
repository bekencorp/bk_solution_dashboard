* [中文](./README_CN.md)

# Two-Wheeler (Scooter) Demo

## Overview

This is a two-wheeler (scooter) solution. It supports casting navigation information to the screen, playing voice navigation / prompt tones, and a 1280X720 dash-cam (driving recorder) feature.
It uses two chips working together: BK7258 handles audio/video data processing, and BK3515N handles Bluetooth audio transmission.

## Building the Project

- This project depends on `bk_avdk_smp_main`. You need to download the `bk_avdk_smp_main` source code before building.
- Build location: build under the scooter solution directory `./projects/scooter`.
- Before building, edit the Makefile (`./projects/scooter/Makefile`) to map the dependent source code to `bk_avdk_smp_main`, for example:

```makefile
# Map the dependent source to bk_avdk_smp_main
SDK_DIR ?= $(abspath ../..)

# change to
SDK_DIR = /home/user.name/bk_avdk_smp_main
```

- Build command: `make bk7258`
- The command above builds for BK7258. After building, a bin file is generated at `./projects/scooter/build/bk7258/scooter/package/all-app.bin`.
- Flash this firmware to BK7258.

## Boot Demo

On boot, the LCD screen shows an LVGL animation by default (resolution: 480X272).

## Navigation Demo

1. Connect a Bluetooth device (e.g. a phone) to BK3515N.
2. Send a command so that BK7258 connects as STA to the phone hotspot. The hotspot is named `scooter` with password `12345678`. Command: `ap_cmd test sta scooter 12345678`.
3. Once connected, open the navigation app on your phone, configure the IP address, and start navigation.
4. The LCD automatically switches from the LVGL screen to the navigation screen, and navigation voice is output from the BK7258 speaker.

Switching between the navigation screen and the LVGL screen is supported:

- Send `ap_cmd test switch lvgl` to switch from the navigation screen to the LVGL screen.
- Send `ap_cmd test switch camera` to switch from the LVGL screen to the navigation screen.

## Dash Cam (Driving Recorder)

The dash cam captures live frames via UVC, encodes and compresses them into H.264 data, and stores them on the SD card, split into 10-minute segments by default.

Start:

1. Send `ap_cmd test uvc open 1280X720` to open UVC with 1280X720 output resolution.
2. Send `ap_cmd test h264e open` to open the encoder and start compressing data.
3. Send `ap_cmd test storage open test.h264` to open the storage and start writing data to xxx_test.h264.
4. xxx ranges from 0-65535; each file stores 10 minutes of video by default.

Stop:

1. Send `ap_cmd test storage close` to close the storage.
2. Send `ap_cmd test h264e close` to close the encoder.
3. Send `ap_cmd test uvc close` to close UVC.
