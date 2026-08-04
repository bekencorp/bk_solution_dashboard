# Scooter Dashboard V1.0 V2 User Guide

* [中文](./README_CN.md)

## Development Board Guide

This guide applies only to Dashboard V1.0 hardware.

### Jumper Configuration

Before powering on, verify that the following jumpers are connected:

| Purpose | Jumper connections |
| --- | --- |
| BK7259 flashing and serial logs | `P11 ↔ RXD`, `P10 ↔ TXD` |
| BK3515NS flashing | `BT_P0 ↔ RXD`, `BT_P1 ↔ TXD` |
| BK7259-to-BK3515NS serial communication | `P71 ↔ BT_RST`, `P0 ↔ BT_P0`, `P1 ↔ BT_P1` |

### Interface Usage

- Use the **USB-UART** interface for BK7259 flashing and serial logs.
- Use the **USB-UART** interface for BK3515NS flashing. Switch the related jumpers as listed above, and disconnect the flashing connection after flashing is complete.
- Use the board's **USB** interface for MTP. After connecting to a PC, run `ap_cmd mtp start` through the serial CLI to expose the SD card as a media device.

## Feature Overview

This project targets Dashboard V1.0 hardware and provides home-page navigation, Dashcam loop recording and playback, Assist View, Bluetooth call controls, network provisioning, and SD-card MTP export.

- Recording files are stored in `/sd0/dashcam`.
- Entering the Dashcam page stops and finalizes the active recording, preventing SD-card contention between recording and playback.
- Recording automatically resumes after leaving the Dashcam page.
- All mapped key events use 500 ms debounce/throttling; events triggered less than 500 ms apart are ignored.

## Pages

| Page | Purpose | How to enter |
| --- | --- | --- |
| Home | Default page for selecting Dashcam or OTA | Shown after boot; long-press the middle key in a feature page to return |
| Dashcam | Browse completed SD-card recordings and play a selected clip; the list shows recording time and file size | Select Dashcam on the Home page, then long-press the middle key |
| OTA | Displays OTA upgrade status and progress | Select OTA on the Home page, then long-press the middle key |
| Assist View | Full-screen live camera view; recording continues while active | Long-press the left or right key; short-press the middle key to exit to Home |

## Keys

The Dashboard V1.0 key-to-GPIO mapping is:

| Key | GPIO | Action | Function |
| --- | --- | --- | --- |
| Up | GPIO32 | Short press | Answer an incoming Bluetooth HFP call |
| Up | GPIO32 | Double press | Hang up a Bluetooth HFP call |
| Down | GPIO27 | Long press | Clear saved network provisioning information and reboot |
| Left | GPIO31 | Long press | Open Assist View |
| Right | GPIO29 | Long press | Open Assist View |
| Middle | GPIO30 | Short press | Switch the Dashcam/OTA selection on Home; select the next recording when the Dashcam list is focused; exit Assist View to Home |
| Middle | GPIO30 | Double press | Enter or exit Dashcam recording-list focus mode |
| Middle | GPIO30 | Long press | Open the currently selected page on Home; play the selected recording when the Dashcam list is focused |

### Middle-Key State Logic

The middle-key behavior depends on the active page and Dashcam list state:

| Current state | Short press | Double press | Long press |
| --- | --- | --- | --- |
| Home | Switch selection between Dashcam and OTA | No action | Enter the selected Dashcam or OTA page |
| Dashcam, recording list not focused | Return to Home | Enter recording-list focus mode | Return to Home |
| Dashcam, recording list focused | Select the next recording; wraps to the first item | Exit recording-list focus mode | Play the selected recording |
| OTA | Return to Home | No action | Return to Home |
| Assist View | Exit Assist View and return to Home | No action | No action |

Entering the Dashcam page stops and finalizes the active recording, then loads the completed recordings from the SD card.

### Dashcam Playback

1. Short-press the middle key to select Dashcam on the Home page.
2. Long-press the middle key to enter the Dashcam page. Recording stops after entering the page.
3. Double-press the middle key to focus the recording list.
4. Short-press the middle key to select the next recording.
5. Long-press the middle key to play the selected recording.
6. Double-press to leave list-focus mode. Long-press to return to Home. Recording resumes after leaving the Dashcam page.

The first line of each recording-list item shows the recording time; the second line shows the file size in MB.

## CLI Commands

Use `ap_cmd dashboard <command>` through the serial CLI:

| Command | Description |
| --- | --- |
| `ap_cmd dashboard dashcam` | Enter the Dashcam page directly |
| `ap_cmd dashboard dashcam_count` | Show the recording count and free SD-card space |
| `ap_cmd dashboard dashcam_trim [target]` | Delete the oldest recordings until no more than `target` recordings remain |
| `ap_cmd dashboard dashcam_rec_start` | Start recording manually |
| `ap_cmd dashboard dashcam_rec_stop` | Stop and finalize recording manually |
| `ap_cmd dashboard dashcam_turn_left` | Open Assist View |
| `ap_cmd dashboard dashcam_turn_off` | Close Assist View |
| `ap_cmd dashboard np_erase [reboot]` | Clear network provisioning information; add `reboot` to reboot immediately |
| `ap_cmd dashboard np_start_advertise` | Restart BLE provisioning advertising |

> Before running `ap_cmd dashboard dashcam_trim [target]`, stop recording with `ap_cmd dashboard dashcam_rec_stop` to avoid concurrent SD-card access while files are deleted.

## FTP Server

The FTP Server starts automatically after the device has connected to the network. Its FTP root directory is the SD-card mount point, `/sd0`. Recordings are stored in `/sd0/dashcam`.

Connect an FTP client to the device's current network IP address:

| Setting | Value |
| --- | --- |
| Protocol | FTP |
| Port | `21` |
| Username | `bk7259` |
| Password | `123456` |
| Recording directory | `/sd0/dashcam` |

Use directory browsing or file download to retrieve recordings. FTP uses clear-text authentication, so use it only on a trusted LAN.

## SD Card MTP

The `mtp` CLI is registered during startup, but the USB MTP device is not started automatically.

1. Insert a formatted SD card and connect the board to a PC through USB.
2. Run `ap_cmd mtp start` in the serial CLI.
3. The PC enumerates the board as an MTP media device. Browse or copy files under `/sd0`, including recordings in `/sd0/dashcam`.
4. Run `ap_cmd mtp stop` when finished.

Supported MTP commands:

| Command | Description |
| --- | --- |
| `ap_cmd mtp start` | Mount the SD card and start the MTP USB device |
| `ap_cmd mtp stop` | Stop the MTP USB device |
| `ap_cmd mtp status` | Show the MTP state |
| `ap_cmd mtp ls [path]` | List SD-card files through the serial CLI; the default path is `/sd0` |

> Stop MTP only after copying files on the PC is complete, otherwise the USB transfer is interrupted.
