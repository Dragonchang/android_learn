LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := main.c server.c table.c uevent.c \
	pub/datatok.c \
	pub/db.c \
	gettime.c \
	proctrl.c \
	logmem.c \
	logfs.c \
	logbattery.c \
	logdevice.c \
	logradio.c \
	logevents.c \
	logkmsg.c \
	logmeminfo.c \
	logkey.c \
	dumpstate.c \
	dumplastkmsg.c \
	iotty.c \
	displaytest.c \
	btrouter.c \
	wimaxuarttool.c \
	logctl.c \
	thrputclnt.c \
	ghost.c \
	fsspeed.c \
	logtouch.c \
	logrpm.c \
	logprocrank.c \
	touchtest.c \
	logoom.c \
	cputest.c \
	lognet.c \
	logsniff.c \
	htc_if.c \
	client/client.c \
	lib/lib.c \
	lib/jniutil.c \
	lib/src/str.c \
	lib/src/glist.c \
	lib/src/conf.c \
	lib/src/attr_table_switch.c \
	lib/src/board.c \
	lib/src/process.c \
	lib/src/opendev.c \
	lib/src/mtd.c \
	lib/src/emmc.c \
	lib/src/emmc_ftm.c \
	lib/src/partition.c \
	lib/src/mb.c \
	lib/src/socket.c \
	lib/src/socket_comm.c \
	lib/src/time.c \
	lib/src/ttytool.c \
	lib/src/usb.c \
	lib/src/battery.c \
	lib/src/cpu.c \
	lib/src/uevent.c \
	lib/src/poll.c \
	lib/src/fio.c \
	lib/src/pclink.c \
	lib/src/hw_libs.c \
	lib/src/sem.c \
	lib/src/dir.c \
	$(NULL)

LOCAL_MODULE := zflserviced

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/lib/ \
	$(NULL)

LOCAL_CFLAGS := \
	-DBUILD_AND \
	$(NULL)

LOCAL_LDFLAGS := \
	$(NULL)

LOCAL_LDLIBS := \
	$(NULL)

#
# always build main.c to update the expiring date
#
$(info Touch service to update the expiring date ... $(shell touch $(LOCAL_PATH)/main.c))

#
# bt router
#
LOCAL_C_INCLUDES += system/bluetooth/bluez-libs/include system/bluetooth/bluez-clean-headers



LOCAL_SHARED_LIBRARIES := \
	libutils \
	libcutils \
	libselinux \
	liblog \
	$(NULL)

include $(BUILD_EXECUTABLE)

