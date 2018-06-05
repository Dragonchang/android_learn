
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

KLOGCAT_APPNAME := Klogcat
KLOGCAT_EXCLUDED := false

ifneq ($(HTC_GP_FLAG),True)
  ifneq ($(HTC_HEP_FLAG),True)
    KLOGCAT_EXCLUDED := true
    $(info $(KLOGCAT_APPNAME): Stock UI build!)
  endif
endif

$(info $(KLOGCAT_APPNAME): HTC_HEP_FLAG                  = $(HTC_HEP_FLAG))
$(info $(KLOGCAT_APPNAME): HTC_GP_FLAG                   = $(HTC_GP_FLAG))
$(info $(KLOGCAT_APPNAME): KLOGCAT_EXCLUDED              = $(KLOGCAT_EXCLUDED))

ifeq ($(KLOGCAT_EXCLUDED),true)
$(info $(KLOGCAT_APPNAME): Exclude $(KLOGCAT_APPNAME) from this build!)
else

klogcat_includes:= \
        $(LOCAL_PATH)/headers \

LOCAL_C_INCLUDES := $(klogcat_includes)

LOCAL_SRC_FILES := \
	klogcat.c \
	logkmsg.c \
	board.c \
	str.c \
	glist.c \
	fio.c \
	dir.c \
	process.c \
	poll.c \
	sem.c \
	client.c \
	server.c \
	$(NULL)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	$(NULL)

LOCAL_MODULE := klogcat

LOCAL_MODULE_TAGS := $(HTC_TAGS)

HTC_FORCE_INSTALL += $(LOCAL_MODULE)

include $(BUILD_EXECUTABLE)

$(call htc-force-install-package, $(HTC_FORCE_INSTALL))

endif # KLOGCAT_EXCLUDED
