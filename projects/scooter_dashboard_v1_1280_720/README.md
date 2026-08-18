# Scooter Dashboard V1.0 User Guide

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
- Use the **USB-UART** interface for BK3515NS flashing. Switch the jumpers as listed above and
  disconnect the flashing connection when finished.
- Use the board's **USB** interface for MTP. After connecting to a PC, run `ap_cmd mtp start`
  to expose the SD Card as a media device.

## Feature Overview

This is the base demonstration project for Dashboard V1.0 hardware. It provides:

- A 1280x720 MIPI DSI + DPU + LVGL dashboard.
- Boot AVI playback.
- Dashboard speed animation and a blinking left turn signal.
- A pet GIF page loaded from the SD Card.
- Phone JPEG casting and dashboard recovery after disconnect.
- BLE / Wi-Fi provisioning.
- BK3515NS classic Bluetooth audio and HFP hands-free calls.
- USB MTP access to SD Card files.

## Pages and Keys

### Dashboard

After the boot animation, the device opens the 1280x720 dashboard. It automatically demonstrates:

- A speed gauge moving repeatedly between 0 and 100.
- A left turn signal alternating between green and gray every 500 ms.

### Pet GIF Page

Place `pet_1.gif` or `pet_2.gif` in the SD Card root directory. Short-press Up to select one
available GIF at random and open the pet page. Short-press Up again to return to the dashboard.

Dashboard V1.0 key GPIOs and current actions:

| Key | GPIO | Action | Function |
| --- | --- | --- | --- |
| Up | GPIO32 | Short press | Toggle between the dashboard and pet GIF page |
| Down | GPIO27 | Short press | Clear saved network provisioning information and reboot |
| Left | GPIO31 | Short / double / long press | Currently unassigned |
| Right | GPIO29 | Short / double / long press | Currently unassigned |
| Middle | GPIO30 | Short press | Clear saved network provisioning information without rebooting |

Other double-press, long-press and hold actions currently have no application callbacks.

## Phone Casting

The phone App discovers the device through SDP and establishes CTRL / VIDEO / AUDIO channels.
JPEG frames are sent to the device for decoding and display. LVGL is paused during casting.
After disconnect, the dashboard, speed animation and turn-signal animation are restored, and
SDP advertising restarts.

## Bluetooth and Network Provisioning

- BK7259 communicates with BK3515NS over UART1: RX / TX use GPIO0 / GPIO1, RTS / CTS use
  GPIO2 / GPIO3, and reset uses GPIO71.
- Initial and negotiated UART baud rates are 115200 and 921600.
- BLE boarding provides Wi-Fi provisioning.
- A2DP Sink / AVRCP provides phone audio playback and control.
- HFP HF provides hands-free calls.

## CLI Commands

Use these commands through the serial CLI:

| Command | Description |
| --- | --- |
| `ap_cmd dashboard np_erase [reboot]` | Clear provisioning data; add `reboot` to reboot immediately |
| `ap_cmd dashboard np_start_advertise` | Restart BLE provisioning advertising |

## SD Card MTP

The `mtp` CLI is registered during startup, but the USB MTP device is not started automatically.

1. Connect the board to a PC through the board's USB interface; no additional storage card is required.
2. Run `ap_cmd mtp start` through the serial CLI.
3. The PC enumerates the SD Card as an MTP media device. Browse or copy files under `/sd0`.
4. Run `ap_cmd mtp stop` when finished.

Supported MTP commands:

| Command | Description |
| --- | --- |
| `ap_cmd mtp start` | Mount the SD Card and start the MTP USB device |
| `ap_cmd mtp stop` | Stop the MTP USB device |
| `ap_cmd mtp status` | Show the MTP state |
| `ap_cmd mtp ls [path]` | List SD Card files through the serial CLI; the default path is `/sd0` |

> Stop MTP only after copying files on the PC is complete, otherwise the USB transfer is interrupted.

## Build

The target chip is `bk7259`. Build it through the Docker wrapper:

```bash
cd projects/scooter_dashboard_v1_1280_720
./dbuild.sh make bk7259
```

On Windows PowerShell:

```powershell
cd projects/scooter_dashboard_v1_1280_720
.\dbuild.ps1 make bk7259
```
