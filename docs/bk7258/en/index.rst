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

.. note::

    If the SDK code is not placed in the ~/armino/bk_avdk_smp directory, you need to specify the SDK_DIR parameter in the make command as the actual path of the SMP SDK.

4. Code Burning
------------------------------------

Path of the generated burning bin file after compilation: ``projects/scooter/build/bk7258/scooter/package/all-app.bin``

For burning process, please refer to `Burning Code <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.0.1/get-started/index.html#burn-code>`_

This document is based on the Armino SMP architecture to help users develop applications.

For the Armino SMP architecture, please refer to `Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Application Processor (AP) configuration and usage, please refer to `Armino AP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.0.1/index.html>`_

For Communication Processor (CP) configuration and usage, please refer to `Armino CP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/cp_doc/bk7258/zh_CN/v3.0.1/index.html>`_

.. toctree::
    :hidden:

    Armino SMP Architecture <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.0.1/index.html>

    Example Projects <projects/index>

* :ref:`genindex`



