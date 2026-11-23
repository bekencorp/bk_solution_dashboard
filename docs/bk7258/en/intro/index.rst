BK7258 Two-Wheeler Solution Introduction
===========================================

:link_to_translation:`zh_CN:[中文]`

Overview
---------------------------------

The BK7258 Two-Wheeler Solution is a smart-dashboard reference design from Beken, built on the BK7258 SoC and the Armino SMP (BK AVDK SMP) v3.1.x architecture. It uses a BK7258 + BK3515NS dual-chip design and covers 480x272, 800x480, and 1024x600 panels through an LCD adapter board. It provides:

- LVGL dashboard UI display;
- Wi-Fi image transmission and phone navigation casting;
- Navigation voice and prompt-tone playback;
- Classic Bluetooth audio, BLE, Wi-Fi provisioning, and a second Bluetooth controller;
- Dashcam, AVI playback, UVC camera, and SD card storage.

.. note::

   For environment setup, source download, build, flashing, BK3515NS companion flashing, and calibration, see :doc:`../get-started/index`.
   For companion software download and usage, see :doc:`../software/index`.
   For each reference project's features and usage, see :doc:`../projects/index`.

Design Philosophy
---------------------------------

The BK7258 Two-Wheeler Solution splits work across a main SoC and a Bluetooth companion:

- **BK7258 main SoC**: dashboard UI, image transmission, provisioning, on-device A/V, and storage;
- **BK3515NS companion**: classic Bluetooth and related companion-side wireless features, working with the main SoC for the full Bluetooth experience;
- **Armino SMP**: SoC, RTOS, drivers, and network stacks; this repository implements the dashboard application and reference projects on top.

Core Features
---------------------------------

1. Dashboard display
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Multiple resolutions: 480x272 / 800x480 / 1024x600;
- LVGL dashboard UI.

2. Casting and image transmission
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Wi-Fi image transmission display;
- Phone navigation casting.

3. Wireless connectivity
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- BLE / Wi-Fi provisioning;
- Classic Bluetooth audio, navigation voice, and a second Bluetooth controller.

4. Storage and peripherals
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Display**: LCD adapter board for multiple panel sizes;
- **Input**: reset button;
- **Storage**: SD card;
- **Camera**: UVC;
- **Companion**: BK3515NS Bluetooth.

Development-board hardware materials are in :doc:`../hw-reference/index`.

System Architecture
---------------------------------

.. rubric:: Relationship between the BK7258 Two-Wheeler Solution and BK AVDK SMP

This repository and the underlying BK AVDK SMP (Armino SMP SDK v3.1.x) split the work as follows:

#. **Scope**: this is a scenario-specific solution on top of BK AVDK SMP, focused on dashboard UI, image transmission, provisioning, and Bluetooth audio; the SoC, RTOS, drivers, and network stacks live in BK AVDK SMP.
#. **Code boundary**: this repository (``bk_solution_dashboard``) provides solution and application code (``components/``, ``projects/``, mounted at the repo root); hardware drivers, RTOS, memory management, and Wi-Fi / BLE stacks live in SMP.

On BK7258, Armino SMP uses an AP (application processor) + CP (communication processor) split:

- **AP**: LVGL dashboard, image transmission, A/V, and application logic;
- **CP**: Wi-Fi, BLE, networking stacks, and low-power management.

Software stack (illustrative)::

    Application (LVGL dashboard / casting / audio)
            ↓
    Services (image tx / audio / provisioning / Dashcam)
            ↓
    OS (RTOS)
            ↓
    Hardware (BK7258 AP+CP + BK3515NS)

Typical Use Cases
---------------------------------

1. **Two-wheeler smart dashboard**: LVGL display and multi-resolution panels;
2. **Phone casting and image transmission**: navigation casting and Wi-Fi image tx;
3. **In-vehicle Bluetooth**: classic Bluetooth audio, HFP hands-free, and provisioning;
4. **UVC and dashcam**: camera preview, recording, and AVI playback.

Technical Highlights
---------------------------------

1. **End-to-end solution paired with SMP**: this repo focuses on dashboard application and UI; drivers / RTOS / stacks ship with BK AVDK SMP.
2. **Dual-chip reference**: BK7258 main SoC + BK3515NS companion covers display and companion-side Bluetooth.
3. **Multiple runnable reference projects**: 480x272 / 800x480 / 1024x600 dashboards, V2 CAN adapter boards, and a UVC example; see :doc:`../projects/index`.
