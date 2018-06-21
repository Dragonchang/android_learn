LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	lib.c \
	jniutil.c \
	src/str.c \
	src/glist.c \
	src/conf.c \
	src/attr_table_switch.c \
	src/board.c \
	src/process.c \
	src/opendev.c \
	src/mtd.c \
	src/emmc.c \
	src/emmc_ftm.c \
	src/partition.c \
	src/mb.c \
	src/socket.c \
	src/socket_comm.c \
	src/time.c \
	src/ttytool.c \
	src/usb.c \
	src/battery.c \
	src/cpu.c \
	src/uevent.c \
	src/poll.c \
	src/fio.c \
	src/pclink.c \
	src/hw_libs.c \
	src/sem.c \
	src/dir.c \
	$(NULL)

LOCAL_SHARED_LIBRARIES := \
	libnativehelper \
	libcutils \
	libutils \
	libhardware \
	libhardware_legacy \
	libdl \
	liblog \
	$(NULL)


LOCAL_STATIC_LIBRARIES := \
	$(NULL)

LOCAL_C_INCLUDES += \
	include/nativehelper \
	include/hardware \
	$(LOCAL_PATH) \
	$(NULL)


LOCAL_CFLAGS := \
	$(NULL)

LOCAL_LDFLAGS := \
	$(NULL)

LOCAL_LDLIBS := \
	$(NULL)

LOCAL_MODULE := libhtc_tools

HTC_FORCE_INSTALL += $(LOCAL_MODULE)

LOCAL_PRELINK_MODULE := false

include $(BUILD_SHARED_LIBRARY)
