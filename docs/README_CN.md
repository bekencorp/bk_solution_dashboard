两轮车demo
-------------------

概述
-------------------

    此为两轮车解决方案，支持导航信息的投屏显示，语音导航/提示音的播放，支持1280X720的分辨率的行车记录仪功能。
    由两块芯片搭配使用，一块为BK7258，负责音视频数据的处理；一块为BK3515N，负责蓝牙音频的传输。

工程编译
-------------------

- 此工程依赖： ``bk_avdk_smp_main``。编译时需要下载 ``bk_avdk_smp_main`` 代码。
- 编译方法：在两轮车方案下： ``./projects/scooter``，下编译。
- 编译之前需要先修改Makefile文件（./projects/scooter/Makefile），将依赖的源码映射到bk_avdk_smp_main上，参考如下：

.. code-block:: makefile

    # 映射依赖的源码到bk_avdk_smp_main上
    SDK_DIR ?= $(abspath ../..)

    # change to
    SDK_DIR = /home/user.name/bk_avdk_smp_main

- 编译命令： ``make bk7258``
- 上面是BK7258的编译命令，编译完成后，会生成bin文件件，路径： ``./projects/scooter/build/bk7258/scooter/package/all-app.bin``。
- 烧录此固件到BK7258上。

开机演示
------------------------

    开机默认LCD屏幕上显示LVGL动画（分辨率：480X272）。


导航演示
------------------------

1、连接蓝牙设备（如手机）到BK3515N上。
2、发送命令，BK7258作为STA连接到手机热点，手机热点定义为： ``scooter``，密码为： ``12345678``。命令： ``ap_cmd test sta scooter 12345678``。
3、连接成功后，打开手机上的导航软件，配置好ip地址，即可开始导航。
4、LCD屏幕会自动从LVGL画面切到导航画面，导航语言会从BK7258的speaker中输出。

支持导航画面和LVGL画面切换显示：
- 发送命令： ``ap_cmd test switch lvgl``，即可从导航画面切换到LVGL画面。
- 发送命令： ``ap_cmd test switch camera``，即可从LVGL画面切换到导航画面。

行车记录仪
------------------------

行车记录仪是通过UVC采集实时画面，并将其编码压缩成H.264格式数据，存储在SD卡中，默认每十分钟为一段。

启动：
1、发送命令： ``ap_cmd test uvc open 1280X720``，即可打开UVC且输出分辨率为1280X720。
2、发送命令： ``ap_cmd test h264e open``，即可打开编码器，开始压缩数据。
3、发送命令： ``ap_cmd test storage open test.h264``，即可打开存储器，开始存储数据到xxx_test.h264中
4、上面xxx的范围是0-65535，默认每个文件存储10分钟的视频。

关闭：
1、发送命令： ``ap_cmd test storage close``，即可关闭存储器。
2、发送命令： ``ap_cmd test h264e close``，即可关闭编码器。
3、发送命令： ``ap_cmd test uvc close``，即可关闭UVC。
