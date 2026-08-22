# Scooter Dashboard Klok 1280x720 User Guide

* [中文](./README_CN.md)

## Development Board Guide

This project is fixed to the BK7259 + FL7703 panel board. The ER68576B legacy
panel uses the independent `scooter_dashboard_klok_1280_720` project; the two
projects do not switch hardware through profile macros.
There is no verified schematic for the FL7703 hardware; its pin assignments
come only from working-firmware analysis and hardware test results.

> **Note**: The board requires a DC 12V power supply to boot normally.

### Jumpers and Interfaces

- Use the **USB-UART** interface for BK7259 flashing and serial logs.
  Before power-on, verify that `P11 ↔ RXD` and `P10 ↔ TXD` are connected.
- Media files are stored on the onboard SD-NAND `/sd0` partition.
- Use the board's **USB** interface for MTP. Connect it to a PC and run
  `ap_cmd mtp start` to expose `/sd0` as a media device.

## Feature Overview

This project demonstrates karaoke and MV playback with:

- A 1280x720 LVGL interface containing Home, Klok, Song List, and MV pages.
- SD Card MP4 / AVI media scanning and playback.
- Hardware H.264 video decoding and AAC audio decoding.
- Full-screen Flexa direct display with PP-OSD playback controls.
- 576x304 RGB565 video previews on the Klok and Song List pages.
- Touch, five-key, and serial CLI controls.
- USB MTP access to media files under `/sd0`.

After boot, the device opens the `home` page. Tap the left card to enter
the Klok page.

## Page Navigation

| Page | How to enter | Back target |
| --- | --- | --- |
| `home` | Default after boot, or `klok page home` | - |
| `klok_main` | Tap the left Home card, or `klok page main` | `home` |
| `song_list` | Open the song/artist page, or `klok page list` | `klok_main` |
| `mv_play` | Tap a song or preview, or run `klok play [file]` | `song_list` |

## Direct and Preview Video Modes

The project enables `KLOK_VIDEO_FLEXA_DIRECT_MODE=1` by default. The
player switches between two video output modes according to the current
page. A switch rebuilds only the video decoder; the container parser,
audio decoder, selected audio track, and audio device remain active.

### Flexa Direct Mode

The full-screen `mv_play` page uses Flexa direct output:

1. The H.264 decoder produces NV12 and applies a 90-degree rotation.
2. Flexa produces compressed ARGB8888 and submits it directly to the DPU.
3. OSD Snapshot is disabled, so LVGL controls are not fused into video frames.
4. Flexa owns the VG-Lite HSRAM while direct output is active. The LVGL
   GPU instance and normal refresh path are suspended to prevent
   concurrent GPU/DPU access.

This path avoids passing each full video frame through LVGL, reducing one
LVGL image update and full-frame copy during full-screen playback. Because
LVGL refresh is suspended, use the five keys or serial CLI as the primary
playback and return controls while direct output is active.

### RGB565 Preview Mode

The `klok_main` and `song_list` pages use preview output:

1. The H.264 frame decoder produces unrotated 1280x720 RGB565 frames.
2. The display worker uses `lv_async_call` to hand frames to the LVGL thread.
3. LVGL scales the video into a fixed 576x304 preview area.
4. LVGL resumes ownership of VG-Lite and the DPU and renders the full page.

### Mode-Switch Sequence

- Song List to `mv_play`: quiesce and drain the current video decoder,
  suspend LVGL refresh, enter Flexa direct mode, then create the direct
  decoder and resume playback.
- `mv_play` to Song List: leave direct mode, restore the LVGL GPU and
  refresh path, then create the RGB565 frame decoder for preview.
- Switching the same file preserves the audio decoder and playback
  position. Entering full-screen playback with a different file starts
  the new file.
- Repeated return requests are blocked during a switch to prevent two
  video decoders from being destroyed or created concurrently.

To disable the direct path, set `KLOK_VIDEO_FLEXA_DIRECT_MODE` to `0` in
`ap/CMakeLists.txt`. The project then uses the fixed RGB565 frame-decoder
and PP-OSD path without runtime output-mode switching.

## Preparing Media Files

1. Copy media files to the `/sd0` root directory. H.264 video with AAC
   audio in an MP4 container is recommended; AVI is also supported.
2. The default demo file is `/sd0/klok_demo.mp4`. Running
   `ap_cmd klok play` without a file name plays this file.
3. The song list scans up to 64 media files.
4. Relative CLI file names automatically receive the `/sd0/` prefix:

```text
ap_cmd klok play demo.mp4
```

This is equivalent to `/sd0/demo.mp4`. An absolute path can also be used:

```text
ap_cmd klok play /sd0/demo.mp4
```

> **Format note**: The song list recognizes `.mp4`, `.avi`, `.mkv`, and
> `.mov` extensions, but the player currently registers only MP4 and AVI
> container parsers. Use `.mp4` or `.avi` files.

## Five-Key Controls

All five keys are active-low, and only short-press callbacks are
registered. Keys work only after playback has started successfully.
They are ignored before playback, after stop, or before UI initialization.

| Key | GPIO | Function |
| --- | --- | --- |
| Down | GPIO27 | Return from full-screen playback to Song List and switch from Flexa direct to RGB565 preview |
| Middle | GPIO30 | Toggle pause/resume while preserving the current file and position |
| Up | GPIO32 | Toggle between track 0 (vocals) and track 1 (accompaniment) |
| Left | GPIO31 | Reopen the current file so audio and video restart together |
| Right | GPIO29 | Play the next cached `/sd0` file, wrapping to the first file at the end |

### Key-Handling Details

- Key callbacks enqueue actions into a depth-5 control queue. A dedicated
  worker executes them serially instead of blocking the key callback.
- Down first checks playback state, an existing pending return, and an
  active output-mode switch. If any check fails, the press is ignored and
  a diagnostic message is printed.
- LVGL refresh is suspended in direct mode, so the key worker performs the
  Down-key return while holding the display lock instead of relying on an
  LVGL asynchronous callback.
- Up succeeds only when the media file contains the selected track. The
  project assigns track 0 to vocals and track 1 to accompaniment.
- Right uses the `/sd0` media cache created during playback. It does
  nothing if the media list is empty.
- Rapid repeated presses may be dropped if the control queue is full or
  an output-mode switch is in progress.
- Double-press, long-press, and hold actions have no application callbacks.

## CLI Commands

### Klok Page and Playback Controls

| Command | Description |
| --- | --- |
| `ap_cmd klok page <home\|main\|list\|play> [file]` | Switch pages; a file may be specified for the playback page |
| `ap_cmd klok back` | Navigate back through `mv_play → song_list → klok_main → home` |
| `ap_cmd klok play [file]` | Open the playback page and play a file; omit the file to use the default |
| `ap_cmd klok full` | Open the full-screen playback page |
| `ap_cmd klok next` / `prev` | Play the next or previous file |
| `ap_cmd klok replay` | Replay the current file from the beginning |
| `ap_cmd klok stop` | Stop playback |
| `ap_cmd klok pause` / `resume` | Pause or resume playback |
| `ap_cmd klok playpause` | Toggle between paused and playing states |
| `ap_cmd klok audio <0\|1>` | Select a track: `0` for vocals, `1` for accompaniment |
| `ap_cmd klok vocal` / `accompany` | Select the vocal or accompaniment track |
| `ap_cmd klok mute` | Toggle mute |
| `ap_cmd klok volup` / `voldown` | Increase or decrease volume |

### Low-Level Player Debugging

| Command | Description |
| --- | --- |
| `ap_cmd video_play_engine start <file>` | Play a file using its absolute path |
| `ap_cmd video_play_engine stop` | Stop the player |
| `ap_cmd video_play_engine pause` / `resume` | Pause or resume the player |
| `ap_cmd video_play_engine seek <ms>` | Seek to a position in milliseconds |
| `ap_cmd video_play_engine vol_up <n>` | Increase volume by `n` |
| `ap_cmd video_play_engine vol_down <n>` | Decrease volume by `n` |
| `ap_cmd video_play_engine mute <on\|off>` | Set mute state |
| `ap_cmd video_play_engine audio_track <index>` | Select an audio track |

## SD Card MTP

The `mtp` CLI is registered during startup, but the USB MTP device is not
started automatically.

1. Connect the board to a PC through the board's USB interface.
2. Run `ap_cmd mtp start` through the serial CLI.
3. After the PC enumerates the MTP device, browse or copy files under `/sd0`.
4. Run `ap_cmd mtp stop` after file transfers are complete.

| Command | Description |
| --- | --- |
| `ap_cmd mtp start` | Mount the SD Card and start the MTP USB device |
| `ap_cmd mtp stop` | Stop the MTP USB device |
| `ap_cmd mtp status` | Show the MTP state |
| `ap_cmd mtp ls [path]` | List a directory through serial; the default path is `/sd0` |

> Stop MTP only after PC file transfers are complete, otherwise the USB
> transfer is interrupted.

## Key Configuration

- The MIPI panel is fixed to FL7703. Its physical resolution is 720x1280;
  LVGL uses a rotated 1280x720 logical resolution.
- Touch uses the project-local GT911 driver on RST=P40, INT=P41, SDA=P1,
  SCL=P0, with IRQ plus polling fallback.
- The SD card uses SDIO0 on P2/P3/P4 in 20 MHz 1-bit mode.
- `KLOK_VIDEO_FLEXA_DIRECT_MODE=1` enables switching between full-screen
  Flexa output and RGB565 preview output.

## Known Limitations

- The default playback command fails if `/sd0/klok_demo.mp4` is missing.
- Only MP4 and AVI container parsers are enabled. MKV / MOV files shown in
  the list are not guaranteed to play.
- Repeated taps or key presses may be ignored briefly during video output
  mode switching.
- Some ranking and favorite cards are UI placeholders without backend logic.
- Replay reopens the current file instead of performing `seek(0)`.

## Build

On Linux:

```bash
cd projects/scooter_dashboard_klok_fl7703_1280_720
./dbuild.sh make bk7259
```

On Windows PowerShell:

```powershell
cd projects/scooter_dashboard_klok_fl7703_1280_720
.\dbuild.ps1 make bk7259
```
