# Scooter Dashboard V1.0 V2 User Guide

* [中文](./README_CN.md)

## Development Board Guide

This guide applies only to Dashboard V1.0 hardware.

> **Note**: The board requires a DC 12V power supply to boot normally.

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

This project targets Dashboard V1.0 hardware and provides home-page navigation, Dashcam loop recording and playback, a phone book, Assist View, Bluetooth call controls, network provisioning, and SD-card MTP export.

- Recording files are stored in `/sd0/dashcam`.
- After entering the Dashcam page, the active recording is stopped and finalized asynchronously before the SD-card recording list is loaded, preventing SD-card contention between recording and playback.
- Recording automatically resumes after leaving the Dashcam page.

## Pages

| Page | Purpose | How to enter |
| --- | --- | --- |
| Home | Default page for selecting Dashcam, OTA, or Phone Book | Shown after boot; double-press the middle key on a feature page to return |
| Dashcam | Browse completed SD-card recordings and play a selected clip; the list shows recording time and file size | Select Dashcam with the left/right key on Home, then short-press the middle key |
| OTA | Displays OTA upgrade status and progress | Select OTA with the left/right key on Home, then short-press the middle key |
| Phone Book | Browse contacts and recent calls, and place calls through Bluetooth HFP | Select Phone Book with the left/right key on Home, then short-press the middle key |
| Assist View | Full-screen live camera view; recording continues while active | Long-press the left or right key on Home; double-press the middle key to exit to Home |

## Keys

The Dashboard V1.0 key-to-GPIO mapping is:

| Key | GPIO | Action | Function |
| --- | --- | --- | --- |
| Up | GPIO32 | Short press | Select the previous item in the active Phone Book list |
| Down | GPIO27 | Short press | Select the next item in the active Phone Book list |
| Down | GPIO27 | Long press | Clear saved network provisioning information and reboot; Home only |
| Left | GPIO31 | Short press | Select the previous item; switch to Contacts in Phone Book |
| Left | GPIO31 | Long press | Open Assist View; Home only |
| Right | GPIO29 | Short press | Select the next item; switch to Recent Calls in Phone Book |
| Right | GPIO29 | Long press | Open Assist View; Home only |
| Middle | GPIO30 | Short press | Confirm the focused item: open a page, play a recording, dial a number, or perform a call action |
| Middle | GPIO30 | Double press | Return to Home from a feature page; exit Assist View to Home |

### Page Key Behavior

| Current page or state | Direction keys | Middle short press | Middle double press |
| --- | --- | --- | --- |
| Home | Left/right selects Dashcam, OTA, or Phone Book | Open the selected page | Stay on or return to Home |
| Dashcam | Left/right selects the previous or next recording, with wraparound | Play the selected recording | Return to Home |
| OTA | No in-page direction-key action | No action | Return to Home |
| Phone Book | Left/right switches Contacts/Recent Calls; up/down selects a list item | Dial the selected number | Return to Home |
| Incoming HFP call | Left/right selects Answer or Hang Up | Perform the selected action | Return to Home |
| Outgoing or active HFP call | The current call action receives focus automatically | Hang up | Return to Home |
| Assist View | No action | No action | Exit Assist View and return to Home |

After entering Dashcam, the page first shows a loading state while a worker stops and finalizes the active recording and scans completed recordings on the SD card. Recording resumes automatically after leaving the page.

### Dashcam Playback

1. Short-press the left or right key on Home to select Dashcam.
2. Short-press the middle key to enter Dashcam and wait for the recording list to load.
3. Short-press the left or right key to select the previous or next recording.
4. Short-press the middle key to play the selected recording.
5. Double-press the middle key to return to Home. Recording resumes after leaving Dashcam.

The first line of each recording-list item shows the recording time; the second line shows the file size in MB.

## CLI Commands

Use `ap_cmd dashboard <command>` through the serial CLI:

| Command | Description |
| --- | --- |
| `ap_cmd dashboard dashcam` | Enter the Dashcam page directly |
| `ap_cmd dashboard phone_book` | Enter the Phone Book page directly |
| `ap_cmd dashboard key_prev` | Simulate a short press of the left key to select the previous item |
| `ap_cmd dashboard key_next` | Simulate a short press of the right key to select the next item |
| `ap_cmd dashboard key_enter` | Simulate a short press of the middle key to confirm the focused item |
| `ap_cmd dashboard key_home` | Simulate a double press of the middle key to return to Home |
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
