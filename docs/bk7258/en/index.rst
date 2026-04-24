Beken Armino Dashboard Solution Development Document
=============================================================

:link_to_translation:`zh_CN:[中文]`

Armino Dashboard Solution
------------------------------------


This is the official documentation for Beken Armino Dashboard solution.

The Armino Dashboard solution is based on the Armino SMP architecture to help users develop applications.
The code download and version compilation methods are as follows:

1. Armino SDK Code Download
------------------------------------

You can download the Armino SMP code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

2. Armino Dashboard Solution Code Download
--------------------------------------------

You can download the Armino Dashboard solution code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_dashboard.git -b release/v3.1.1


3. Armino Dashboard Solution Version Compilation
----------------------------------------------------

The version compilation method is as follows::

    cd ~/armino/bk_solution_dashboard
    cd projects/scooter
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

Alternatively, you can specify the SDK path using the export command::

    cd ~/armino/bk_solution_dashboard
    cd projects/scooter
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7258

Compile using Docker as follows::

    On Linux or macOS systems, execute the following commands in the terminal::

        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7258

    On Windows, use PowerShell to execute the following commands::

        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        ./dbuild.ps1 make clean
        ./dbuild.ps1 make bk7258

.. note::

    If the SDK code is not placed in the ~/armino/bk_avdk_smp directory, you need to specify the SDK_DIR parameter in the make command as the actual path of the SMP SDK.

4. Code Burning
------------------------------------

4.1 Mother board
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Path of the generated burning bin file after compilation: ``projects/scooter/build/bk7258/scooter/package/all-app.bin``

For burning process, please refer to `Burning Code <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.0.1/get-started/index.html#burn-code>`_

4.2 3515
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1. Connect BT_P0/BT_P1/BT_RST/VBAT/GND on mother board, lead to uart board below:
    - BT_P0: uart board rx, be careful that some uart board tx/rx is confused.
    - BT_P1: uart board tx, be careful that some uart board tx/rx is confused.
    - BT_RST: high
    - VBAT: 3.3v power
    - GND: ground

2. Download Beken_BT_Toolkit_V7.6.0.14 `https://dl.bekencorp.com/tools/Beken_Toolkit <https://dl.bekencorp.com/tools/Beken_Toolkit>`_ or newest version

3. Download 3515 firmware `https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller <https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller>`_

- relationship:

    +------------------+----------------------+----------------+------------------+
    | EVB version      | relative dir         | 3515 used uart | bk7258 used uart |
    +------------------+----------------------+----------------+------------------+
    | V1.1 20250904    | EVB_v1.1_20250904    | P10 P11        | P30 P31          |
    +------------------+----------------------+----------------+------------------+
    | V1.2 20251027    | EVB_v1.2_20251027    | P0 P1          | P30 P31          |
    +------------------+----------------------+----------------+------------------+

4. Launch Beken_BT_Toolkit，set bk3296 in chipset，click login

5. Select flash download with 3515 firmware above. select uart setting，change uart id

6. Press 3515NS RESET/7258 RESET on mother board simultaneously，wait 5 seconds，click Beken_BT_Toolkit download button.

7. Relase 3515NS RESET，you will see download processing, if fail, try again from step 6

8. It will prompt Download successful，you cant release 7258 RESET now.

9. Disconnect all connected wires above, press 7258 RESET

.. note::

    It will increase download successfully rate when mother board doesn't work.

5. 3515NS Calibration
------------------------------------

1. The 3515NS must be calibrated by sending commands. Please refer to:

    - The **frequency offset** and **power** settings chapters in `BK3515NS Bluetooth Module Guide.docx <https://gitlab.bekencorp.com/controller/bk3515ns/-/blob/main/BK3515NS%20Bluetooth%20Module%20Guide.docx>`_
    - The **Classic procedure** section in `rf测试_hci交互.txt <https://gitlab.bekencorp.com/controller/bk3515ns/-/blob/main/rf%E6%B5%8B%E8%AF%95_hci%E4%BA%A4%E4%BA%92.txt>`_ for **non-signaling** testing

2. After calibration is complete, follow the **enter DUT mode** steps in ``rf测试_hci交互.txt``; **signaling** tests can then be performed.

3. Calibration values can be saved by **sending commands to the 3515 over fly wires**, or they can be stored on the **host (main) CPU**. In the latter case, enable the ``TCI_READ_FRQ_OFFSET_FROM_FLASH`` and ``TCI_WRITE_POWER_TO_FLASH`` related code in the ``bsc_init`` function in ``components/bluetooth_secondary_controller/bsc_main.c`` to perform the write.

.. note::

    If values are provisioned from the host, you can read them back via ``TCI_READ_FRQ_OFFSET_FROM_FLASH`` and ``TCI_READ_POWER_FROM_FLASH``, compare with the current values, and **avoid writing on every boot** when they already match.


本文档基于Armino SMP架构, 帮助用户开发应用;

Armino SMP架构, 请参考 `Armino SMP架构 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

应用处理器AP配置和使用, 请参考 `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.0.1/index.html>`_

通信处理器CP配置和使用, 请参考 `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

This document is based on the Armino SMP architecture to help users develop applications.

For the Armino SMP architecture, please refer to `Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Application Processor (AP) configuration and usage, please refer to `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Communication Processor (CP) configuration and usage, please refer to `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

.. toctree::
    :hidden:

    Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>

    Example Projects <projects/index>

    H/W Reference Manual <hw-reference/index>

* :ref:`genindex`
