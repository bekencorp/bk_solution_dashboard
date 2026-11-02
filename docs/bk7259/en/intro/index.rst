BK7259 Two-Wheeler Solution Introduction
===========================================

:link_to_translation:`zh_CN:[中文]`

Overview
---------------------------------

The BK7259 Two-Wheeler Solution is a smart-dashboard reference design from Beken, built on the BK7259 SoC and the Armino SMP (BK AVDK SMP) v4.0.x architecture. It uses a BK7259 + BK3515N dual-chip design, primarily for 1280x720 landscape dashboards, and also includes a 1024x600 example project. It provides:

- MIPI DSI + DPU + LVGL dashboard display and boot AVI playback;
- Phone JPEG casting, plus Wi-Fi / BLE image transmission;
- BLE / Wi-Fi provisioning;
- Classic Bluetooth audio, navigation voice, and multi-controller Bluetooth;
- SD card storage and complete end-to-end example projects.

.. note::

   For environment setup, source download, build, flashing, and BK3515N companion flashing, see :doc:`../get-started/index`.
   For companion software download and usage, see :doc:`../software/index`.
   For each example project's features and usage, see :doc:`../projects/index`.

Design Philosophy
---------------------------------

The BK7259 Two-Wheeler Solution splits work across a main SoC and a Bluetooth companion:

- **BK7259 main SoC**: dashboard UI, image transmission, provisioning, on-device A/V, and storage;
- **BK3515N companion**: classic Bluetooth and related companion-side wireless features, working with the main SoC for the full Bluetooth experience;
- **Armino SMP**: SoC, RTOS, drivers, and network stacks; this repository implements the dashboard application and example projects on top.

Core Features
---------------------------------

1. Dashboard display
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- 1280x720 landscape MIPI DSI + DPU;
- LVGL dashboard UI and boot AVI playback.

2. Casting and image transmission
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Phone JPEG casting;
- Wi-Fi and BLE image transmission.

3. Wireless connectivity
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- BLE / Wi-Fi provisioning;
- Classic Bluetooth audio, navigation voice, and multi-controller Bluetooth.

4. Storage and peripherals
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Display**: 1280x720 landscape MIPI DSI;
- **Input**: reset (RESET) and provisioning-reset (K1) buttons;
- **Storage**: SD card;
- **Companion**: BK3515N Bluetooth.

Development-board hardware materials are in :doc:`../hw-reference/index`.

System Architecture
---------------------------------

.. rubric:: Relationship between the BK7259 Two-Wheeler Solution and BK AVDK SMP

This repository and the underlying BK AVDK SMP (Armino SMP SDK v4.0.x) split the work as follows:

#. **Scope**: this is a scenario-specific solution on top of BK AVDK SMP, focused on dashboard UI, image transmission, provisioning, and Bluetooth audio; the SoC, RTOS, drivers, and network stacks live in BK AVDK SMP.
#. **Code boundary**: this repository (``bk_solution_dashboard``) provides solution and application code (``components/``, ``projects/``, mounted at the repo root); hardware drivers, RTOS, memory management, and Wi-Fi / BLE stacks live in SMP.

On BK7259, Armino SMP uses an AP (application processor) + CP (communication processor) split:

- **AP**: LVGL dashboard, image transmission, A/V, and application logic;
- **CP**: Wi-Fi, BLE, networking stacks, and low-power management.

Software stack (illustrative)::

    Application (LVGL dashboard / casting / music / phone)
            ↓
    Services (image tx / audio / provisioning / Dashcam)
            ↓
    OS (RTOS)
            ↓
    Hardware (BK7259 AP+CP + BK3515N)

Typical Use Cases
---------------------------------

1. **Two-wheeler smart dashboard**: LVGL display, boot animation, and physical-key interaction;
2. **Dashcam record and playback**: loop recording and local playback;
3. **Phone casting and image transmission**: JPEG casting and Wi-Fi / BLE image tx;
4. **In-vehicle entertainment**: local music, karaoke / MV, Bluetooth calls and contacts.

Technical Highlights
---------------------------------

1. **End-to-end solution paired with SMP**: this repo focuses on dashboard application and UI; drivers / RTOS / stacks ship with BK AVDK SMP.
2. **Dual-chip reference**: BK7259 main SoC + BK3515N companion covers display and companion-side Bluetooth.
3. **Multiple runnable example projects**: 1280x720 / 1024x600 dashboards, dashcam, casting/provisioning, and Klok karaoke variants; see :doc:`../projects/index`.
