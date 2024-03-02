LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(call all-makefiles-under,$(LOCAL_PATH))

include device/generic/goldfish/wifi/Android.mk
