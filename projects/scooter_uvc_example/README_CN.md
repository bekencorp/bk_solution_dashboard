# Scooter项目 UVC示例工程

* [English](./README.md)

## 快速开始

取码、编译环境和通用烧录见
[BK7258 两轮车方案快速入门](https://docs.bekencorp.com/arminodoc/bk_dashboard/bk7258/zh_CN/v3.1.1/get-started/index.html)。

本工程编译：

```bash
cd ~/armino/bk_solution_dashboard/projects/scooter_uvc_example
make bk7258 SDK_DIR=~/armino/bk_avdk_smp
```

本工程产物：

```text
build/bk7258/scooter_uvc_example/package/all-app.bin
```

将该文件按方案快速入门中的 BKFIL 步骤烧录。BK3515NS 从控烧录见同一页。

## 注意事项

- 暂时不支持投屏导航。

本工程（`scooter_uvc_example`）主要用于测试 UVC 功能（设备接入、预览显示链路、帧队列与解码显示流程）。
