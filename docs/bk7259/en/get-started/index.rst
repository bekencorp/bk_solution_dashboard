Quick Start
=================================

:link_to_translation:`zh_CN:[中文]`

This section explains how to get the BK7259 Two-Wheeler Solution code, set up the build environment, build a project, and flash the firmware, so you can get started quickly.

.. include:: ../_readme_body.rst

Flash the companion
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

In addition to the BK7259 main firmware, the BK3515N Bluetooth companion must be flashed separately.
BKFIL is supported.

Step 1: Lead out BT_P0 / BT_P1 / BT_RST / VBAT / GND from connector H2 on the mother board, and connect them to a UART board as follows:

   - BT_P0: UART board rx (some UART boards have tx / rx swapped)
   - BT_P1: UART board tx (some UART boards have tx / rx swapped)
   - BT_RST: high
   - VBAT: 3.3 V
   - GND: ground

Download the `3515 firmware <https://dl.bekencorp.com/armino_bin/smp_solution/bk_solution_dashboard/3515n_controller>`_

Mapping:

+------------------+----------------------+----------------+-----------------------+
| EVB version      | firmware directory   | 3515 UART      | BK7259 UART TX/RX     |
+==================+======================+================+=======================+
| V1.1 20250904    | EVB_v1.1_20250904    | P10 P11        | P55 P54               |
+------------------+----------------------+----------------+-----------------------+
| V1.2 20251027    | EVB_v1.2_20251027    | P0 P1          | P55 P54               |
+------------------+----------------------+----------------+-----------------------+

Step 2: Launch BKFIL

Step 3: Select flash download with the 3515 firmware above. Open UART settings and select the correct port

Step 4: Press 3515NS RESET and 7259 RESET on the mother board at the same time, wait 5 seconds, then click the BKFIL download button

Step 5: Release 3515NS RESET. You should see the download progress; if it fails, retry from Step 4

Step 6: When it prompts Download successful, you can release 7259 RESET

Step 7: Disconnect the wires above and press 7259 RESET

`Beken_BT_Toolkit <https://dl.bekencorp.com/tools/Beken_Toolkit>`_ is also supported; follow the same steps and only change the tool.

.. note::

   The download success rate is higher when the mother board is not running.

BK3515N calibration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Step 1: The 3515N must be calibrated by sending commands. Please refer to:

   - The **frequency offset** and **power** settings chapters in `BK3515NS Bluetooth Module Guide <https://dl.bekencorp.com/doc/smp_solution/bk_solution_dashboard/BK3515NS%20Bluetooth%20Module%20Guide/BK3515NS%20Bluetooth%20Module%20Guide.docx>`_
   - The **Classic procedure** section in `rf测试_hci交互 <https://dl.bekencorp.com/doc/smp_solution/bk_solution_dashboard/rf%E6%B5%8B%E8%AF%95_hci%E4%BA%A4%E4%BA%92/rf%E6%B5%8B%E8%AF%95_hci%E4%BA%A4%E4%BA%92.txt>`_ for non-signaling testing

Step 2: After calibration is complete, follow the **enter DUT mode** steps in that file; signaling tests can then be performed.

Step 3: Calibration values can be saved by sending commands to the 3515 over fly wires, or they can be stored on the host CPU. In the latter case, enable the ``TCI_READ_FRQ_OFFSET_FROM_FLASH`` and ``TCI_WRITE_POWER_TO_FLASH`` related code in the ``bsc_init`` function in ``components/bluetooth_secondary_controller/bsc_main.c`` to perform the write.

.. note::

   If values are provisioned from the host, you can read them back via ``TCI_READ_FRQ_OFFSET_FROM_FLASH`` and ``TCI_READ_POWER_FROM_FLASH``, compare with the current values, and avoid writing on every boot when they already match.

Related resources
--------------------------------

- For Application Processor (AP) configuration and usage, see `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/en/v4.0.1/index.html>`_
- For Communication Processor (CP) configuration and usage, see `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7259/en/v4.0.1/index.html>`_
