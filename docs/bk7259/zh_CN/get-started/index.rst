快速入门
=================================

:link_to_translation:`en:[English]`

本节说明 BK7259 两轮车方案如何获取代码、安装编译环境、编译工程以及烧录固件，帮助您快速上手。

.. include:: ../_readme_body.rst

烧录从控
,,,,,,,,,,,,

除 BK7259 主控固件外，BK3515N 蓝牙从控还需单独烧录。
支持使用 BKFIL 烧录。

步骤 1：引出母板上 H2 的 BT_P0 / BT_P1 / BT_RST / VBAT / GND，按以下方法接入串口小板

   - BT_P0: 串口小板 rx，注意某些串口小板 tx / rx 是反的
   - BT_P1: 串口小板 tx，注意某些串口小板 tx / rx 是反的
   - BT_RST: 高
   - VBAT: 3.3V 供电
   - GND: 地

下载 `3515 固件 <https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller>`_

对应关系:

+------------------+----------------------+---------------+--------------------+
| EVB版本          | 对应固件目录         | 3515使用串口  | bk7259 UART TX/RX  |
+==================+======================+===============+====================+
| V1.1 20250904    | EVB_v1.1_20250904    | P10 P11       | P55 P54            |
+------------------+----------------------+---------------+--------------------+
| V1.2 20251027    | EVB_v1.2_20251027    | P0 P1         | P55 P54            |
+------------------+----------------------+---------------+--------------------+

步骤 2：打开 BKFIL

步骤 3：选择 flash 烧录，选择刚刚下载的 3515 固件。选择串口设置，更改对应的串口号

步骤 4：同时按住母板的 3515NS RESET / 7259 RESET 键，等待 5 秒，点击 BKFIL 下载按钮

步骤 5：松开 3515NS RESET，此时可以看到正在下载；如果失败，从步骤 4 重试

步骤 6：烧录成功会提示 Download successful，此时可以松开 7259 RESET

步骤 7：拔掉所有引出的线，按 7259 RESET 复位

也支持 `Beken_BT_Toolkit <https://dl.bekencorp.com/tools/Beken_Toolkit>`_ 烧录方式，步骤同上，更换工具即可。

.. note::

   尽量让母板不工作可以提高烧录成功率。

BK3515N 校准
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

步骤 1：3515N 需要发送命令校准，请参考

   - `BK3515NS Bluetooth Module Guide <https://dl.bekencorp.com/doc/smp_solution/bk_solution_dashboard/BK3515NS%20Bluetooth%20Module%20Guide/BK3515NS%20Bluetooth%20Module%20Guide.docx>`_ 的 **频偏设置** 及 **功率设置** 章节
   - `rf测试_hci交互 <https://dl.bekencorp.com/doc/smp_solution/bk_solution_dashboard/rf%E6%B5%8B%E8%AF%95_hci%E4%BA%A4%E4%BA%92/rf%E6%B5%8B%E8%AF%95_hci%E4%BA%A4%E4%BA%92.txt>`_ 的 **classic步骤** 章节进行非信令测试

步骤 2：校准完成后，参考该文件的进入 DUT 模式后，可以进行信令测试

步骤 3：校准值可以通过 3515 飞线发送命令保存，也可以保存在主控 CPU 端，然后开启 ``components/bluetooth_secondary_controller/bsc_main.c`` 的 ``bsc_init`` 函数中的 ``TCI_READ_FRQ_OFFSET_FROM_FLASH`` 和 ``TCI_WRITE_POWER_TO_FLASH`` 相关代码写入。

.. note::

   如果从主控端下发，可以通过 ``TCI_READ_FRQ_OFFSET_FROM_FLASH`` 和 ``TCI_READ_POWER_FROM_FLASH`` 读取，和现有的比较，以避免每次开机写入。

相关资源
--------------------------------

- 应用处理器 AP 配置和使用，请参考 `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/zh_CN/v4.0.1/index.html>`_
- 通信处理器 CP 配置和使用，请参考 `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7259/zh_CN/v4.0.1/index.html>`_
