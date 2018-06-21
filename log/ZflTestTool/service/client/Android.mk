LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	client.c \
	main.c \
	$(NULL)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/.. \
	$(HTC_LIB_PATH) \
	$(NULL)

LOCAL_CFLAGS := \
	-DBUILD_AND \
	-DFORCE_STDOUT \
	$(HTC_COMMON_CFLAGS) \
	$(NULL)

LOCAL_LDFLAGS := \
	$(HTC_COMMON_LDFLAGS) \
	$(NULL)

LOCAL_LDLIBS := \
	$(NULL)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	$(NULL)

LOCAL_MODULE := htcservice
include $(BUILD_EXECUTABLE)

