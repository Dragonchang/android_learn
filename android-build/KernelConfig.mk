#----------------------------------------------------------------------
# Define kernel defconfig file
#----------------------------------------------------------------------

ifeq ($(LOCAL_PATH),)
    LOCAL_PATH := $(shell pwd)/device/htc/$(TARGET_PRODUCT)
endif

ifeq ($(PRODUCT_OUT),)
    PRODUCT_OUT := out/target/product/$(TARGET_PRODUCT)
    $(shell mkdir -p $(PRODUCT_OUT))
endif

TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL:= msm-4.9
REMOVE_LIST := vendor/htc/proprietary/PowerPerf/common/remove_list/list_4_9.cfg
DEFCONFIG_PREFIX := sdm845

ifeq ($(KERNEL_DEFCONFIG),)
    ifeq ($(KERNEL_PERFORMANCE_BUILD), true)
                KERNEL_DEFCONFIG := htcperf_defconfig
                ifeq ($(TARGET_BUILD_VARIANT), user)
                    $(shell python device/htc/common/gen_Perf_conf.py -a $(TARGET_KERNEL_ARCH) -k $(TARGET_KERNEL) -c $(DEFCONFIG_PREFIX) -p $(KERNEL_DEFCONFIG) -o $(PRODUCT_OUT)/kern-perf_defconfig -l $(REMOVE_LIST))
                else
                    $(shell python device/htc/common/gen_Perf_conf.py -a $(TARGET_KERNEL_ARCH) -k $(TARGET_KERNEL) -c $(DEFCONFIG_PREFIX) -p $(KERNEL_DEFCONFIG) -o $(PRODUCT_OUT)/kern-perf_defconfig -l $(REMOVE_LIST))
                endif
    else
        # For HEP/SHEP ROM
        ifneq ($(BCMS_SENSE_VERSION), None)
            ifneq ($(HTC_DEBUG_FLAG), DEBUG)
                KERNEL_DEFCONFIG := htcperf_defconfig
                ifeq ($(TARGET_BUILD_VARIANT), user)
                    $(shell python device/htc/common/gen_Perf_conf.py -a $(TARGET_KERNEL_ARCH) -k $(TARGET_KERNEL) -c $(DEFCONFIG_PREFIX) -p $(KERNEL_DEFCONFIG) -o $(PRODUCT_OUT)/kern-perf_defconfig -l $(REMOVE_LIST))
                else
                    $(shell echo "python device/htc/common/gen_Perf_conf.py -a $(TARGET_KERNEL_ARCH) -k $(TARGET_KERNEL) -c $(DEFCONFIG_PREFIX) -p $(KERNEL_DEFCONFIG) -o $(PRODUCT_OUT)/kern-perf_defconfig -l $(REMOVE_LIST)" > command)
                    $(shell python device/htc/common/gen_Perf_conf.py -a $(TARGET_KERNEL_ARCH) -k $(TARGET_KERNEL) -c $(DEFCONFIG_PREFIX) -p $(KERNEL_DEFCONFIG) -o $(PRODUCT_OUT)/kern-perf_defconfig -l $(REMOVE_LIST))
                endif
            else
                KERNEL_DEFCONFIG := $(DEFCONFIG_PREFIX)_defconfig
        endif
        # For GEP ROM
        else
            ifeq ($(TARGET_BUILD_VARIANT), user)
                KERNEL_DEFCONFIG := htcperf_defconfig
                $(shell python device/htc/common/gen_Perf_conf.py -a $(TARGET_KERNEL_ARCH) -k $(TARGET_KERNEL) -c $(DEFCONFIG_PREFIX) -p $(KERNEL_DEFCONFIG) -o $(PRODUCT_OUT)/kern-perf_defconfig -l $(REMOVE_LIST))
            else
                KERNEL_DEFCONFIG := $(DEFCONFIG_PREFIX)_defconfig
           endif
        endif
    endif
endif
