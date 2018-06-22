LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	client.c \
	main.c \
	$(NULL)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/.. \
	$(NULL)

LOCAL_CFLAGS := \
	-DBUILD_AND \
	-DFORCE_STDOUT \
	$(NULL)

LOCAL_LDFLAGS := \
	$(NULL)

LOCAL_LDLIBS := \
	$(NULL)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	$(NULL)

LOCAL_MODULE := zflservice
include $(BUILD_EXECUTABLE)

