博通集成 Armino Dashboard解决方案
=====================================

:link_to_translation:`en:[English]`

这是博通集成 Armino Dashboard解决方案的官方文档。

Armino Dashboard解决方案基于Armino SMP架构, 帮助用户开发应用;
代码下载及版本编译方法如下:

1. Armino SDK 代码下载
------------------------------------

您可从 gitlab 上下载 Armino SMP 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

2. Armino Dashboard解决方案 代码下载
------------------------------------

您可从 gitlab 上下载 Armino Dashboard解决方案 代码::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_dashboard.git -b release/v3.1.1


3. Armino Dashboard解决方案 版本编译
------------------------------------

版本编译方法如下::

    cd ~/armino/bk_solution_dashboard
    cd projects/scooter_1280_720
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7259 SDK_DIR=~/armino/bk_avdk_smp

或者可以通过export来指定SDK路径::

    cd ~/armino/bk_solution_dashboard
    cd projects/scooter_1280_720
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7259

使用docker编译方式如下

    linux或macos系统下在终端执行以下命令::

        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7259

    Windows下使用powershell执行以下命令::

        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        ./dbuild.ps1 make clean
        ./dbuild.ps1 make bk7259

.. note::

    如果SDK的代码未放在 ~/armino/bk_avdk_smp 目录下, 需要在 make 命令中指定 SDK_DIR 参数为SMP SDK实际路径。


4. 代码烧录
------------------------------------

4.1 母板
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

编译生成的烧录bin文件路径：``projects/scooter_1280_720/build/bk7259/scooter_1280_720/package/all-app.bin``

烧录流程参考 `烧录代码 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v3.0.1/get-started/index.html#id7>`_

4.2 3515
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1. 引出母板上H2的BT_P0/BT_P1/BT_RST/VBAT/GND，按以下方法接入串口小板
    - BT_P0: 串口小板rx, 注意，某些串口小板tx rx是反的
    - BT_P1: 串口小板tx，注意，某些串口小板tx rx是反的
    - BT_RST: 高
    - VBAT: 3.3v供电
    - GND: 地

2. 下载Beken_BT_Toolkit_V7.6.0.14 `https://dl.bekencorp.com/tools/Beken_Toolkit <https://dl.bekencorp.com/tools/Beken_Toolkit>`_ 或更新版本

3. 下载3515固件 `https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller <https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller>`_

- 对应关系:

    +------------------+----------------------+---------------+--------------------+
    | EVB版本          | 对应固件目录         | 3515使用串口  | bk7259 UART TX/RX  |
    +------------------+----------------------+---------------+--------------------+
    | V1.1 20250904    | EVB_v1.1_20250904    | P10 P11       | P55 P54            |
    +------------------+----------------------+---------------+--------------------+
    | V1.2 20251027    | EVB_v1.2_20251027    | P0 P1         | P55 P54            |
    +------------------+----------------------+---------------+--------------------+

4. 打开Beken_BT_Toolkit，chipset选择bk3296，点击login

5. 选择flash烧录，选择刚刚下载的3515固件。选择串口设置，更改对应的串口号

6. 同时按住母板的3515NS RESET/7259 RESET键，等待5秒，点击Beken_BT_Toolkit下载按钮

7. 松开3515NS RESET，此时可以看到正在下载，如果失败，从第6步重试

8. 烧录成功会提示Download successful，此时可以松开7259 RESET

9. 拔掉所有引出的线，按7259 RESET复位

.. note::

    尽量让母版不工作可以提高烧录成功率。

本文档基于Armino SMP架构, 帮助用户开发应用;

Armino SMP架构, 请参考 `Armino SMP架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v3.0.1/index.html>`_

应用处理器AP配置和使用, 请参考 `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/zh_CN/v3.0.1/index.html>`_

通信处理器CP配置和使用, 请参考 `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7259/zh_CN/v3.0.1/index.html>`_

.. toctree::
    :hidden:

    Armino SMP架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html>

    示例工程 <projects/index>

    H/W 参考手册 <hw-reference/index>

* :ref:`genindex`
