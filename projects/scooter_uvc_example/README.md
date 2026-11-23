# Scooter Project UVC example

* [中文](./README_CN.md)

## Quick Start

For source download, the build environment, and general flashing, see
[BK7258 Two-Wheeler Solution Quick Start](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/en/v3.1.1/get-started/index.html).

Build this project:

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_uvc_example
make bk7258 SDK_DIR=~/armino/bk_avdk_smp
```

This project's output:

```text
build/bk7258/scooter_uvc_example/package/all-app.bin
```

Flash that file with the BKFIL steps on the solution Quick Start page. BK3515NS companion flashing is on the same page.

## Notes

- Screen-casting navigation is not supported yet.

This project (`scooter_uvc_example`) is mainly used to test UVC functionality (device connection, preview pipeline, frame queues, and decode/display flow).
