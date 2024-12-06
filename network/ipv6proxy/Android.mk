LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	address.cpp \
	interface.cpp \
	log.cpp \
	main.cpp \
	namespace.cpp \
	packet.cpp \
	proxy.cpp \
	router.cpp \
	socket.cpp \


LOCAL_CPPFLAGS += -Werror
LOCAL_SHARED_LIBRARIES := libcutils liblog
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE := ipv6proxy2

LOCAL_MODULE_CLASS := EXECUTABLES

include $(BUILD_EXECUTABLE)
