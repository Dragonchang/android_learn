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
	$(NULL)

LOCAL_MODULE := htcserviced

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../lib/ \
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
LOCAL_C_INCLUDES += system/bluetooth/bluez-libs/include system/bluetooth/bluez-clean-headers $(HTC_INCLUDE_PATH)

ifneq ($(HTC_FORCE_BT_OFF),true)
  ifeq ($(BOARD_HAVE_BLUETOOTH_TI),true)
    LOCAL_CFLAGS += -DBT_TI
  endif
endif

LOCAL_SHARED_LIBRARIES := \
	libutils \
	libcutils \
	libselinux \
	liblog \
	libhtc_tools \
	$(NULL)

include $(BUILD_EXECUTABLE)

