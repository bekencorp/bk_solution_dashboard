SDK_DIR = /home/shi.xiang/wps/DEBUG-SPACE/bk_solution_dashboard_dev_7259v2_bringup_25W4801/0624/avdk_smp

PROJECT_MAKE_FILE := $(SDK_DIR)/tools/build_tools/build_files/project_main.mk
ifeq ($(wildcard $(PROJECT_MAKE_FILE)),)
    $(error "$(PROJECT_MAKE_FILE) not exist, please check sdk directory.")
endif
include $(PROJECT_MAKE_FILE)
