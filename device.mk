include vendor/redroid_ext/BoardConfig.mk

######################
# 通用配置
######################
PRODUCT_BROKEN_VERIFY_USES_LIBRARIES := true

PRODUCT_COPY_FILES += \
    vendor/redroid_ext/redroid.ext.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/redroid.ext.rc \

PRODUCT_PACKAGES += libstdc++.vendor


######################
# 显卡相关配置
######################
ifneq (,$(filter  mali-tDVx mali-G52 mali-G610, $(TARGET_BOARD_PLATFORM_GPU)))
BOARD_VENDOR_GPU_PLATFORM := bifrost
endif

ifneq (,$(filter  mali-t860 mali-t760, $(TARGET_BOARD_PLATFORM_GPU)))
BOARD_VENDOR_GPU_PLATFORM := midgard
endif

ifeq ($(strip $(TARGET_ARCH)), arm64)
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
endif

# define MPP_BUF_TYPE_DRM 1
# define MPP_BUF_TYPE_ION_LEGACY 2
# define MPP_BUF_TYPE_ION_404 3
# define MPP_BUF_TYPE_ION_419 4
# define MPP_BUF_TYPE_DMA_BUF 5
ifeq ($(TARGET_RK_GRALLOC_VERSION),4)
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.mpp_buf_type=1
# Gralloc HAL
PRODUCT_PACKAGES += \
    arm.graphics-V1-ndk_platform.so \
    android.hardware.graphics.allocator@4.0-impl-$(BOARD_VENDOR_GPU_PLATFORM) \
    android.hardware.graphics.mapper@4.0-impl-$(BOARD_VENDOR_GPU_PLATFORM) \
    android.hardware.graphics.allocator@4.0-service

DEVICE_MANIFEST_FILE += \
    device/rockchip/common/manifests/android.hardware.graphics.mapper@4.0.xml \
    device/rockchip/common/manifests/android.hardware.graphics.allocator@4.0.xml
else
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.mpp_buf_type=1
PRODUCT_PACKAGES += \
    gralloc.$(TARGET_BOARD_HARDWARE) \
    android.hardware.graphics.mapper@2.0-impl-2.1 \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service

DEVICE_MANIFEST_FILE += \
    device/rockchip/common/manifests/android.hardware.graphics.mapper@2.1.xml \
    device/rockchip/common/manifests/android.hardware.graphics.allocator@2.0.xml
endif

$(call inherit-product, device/rockchip/common/rootdir/rootdir.mk)
$(call inherit-product, device/rockchip/common/modules/mediacodec.mk)
$(call inherit-product, vendor/rockchip/common/device-vendor.mk)


######################
# wifi相关配置
######################
PRODUCT_SOONG_NAMESPACES += \
    device/generic/goldfish \

PRODUCT_PACKAGES += \
    iw_vendor \
    emulatorip2 \
    dhcpclient \
    dhcpserver2 \

PRODUCT_PACKAGES += \
    mac80211_create_radios \
    createns \
    execns \
    hostapd \
    hostapd_nohidl \
    ipv6proxy \
    wpa_supplicant \

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/wifi/init.redroid-net.sh:$(TARGET_COPY_OUT_VENDOR)/bin/init.redroid-net.sh \
    $(LOCAL_PATH)/wifi/init.wifi.sh:$(TARGET_COPY_OUT_VENDOR)/bin/init.wifi.sh \
    $(LOCAL_PATH)/wifi/init.wifi.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.wifi.rc \
    device/generic/goldfish/wifi/simulated_hostapd.conf:$(TARGET_COPY_OUT_VENDOR)/etc/simulated_hostapd.conf \
    device/generic/goldfish/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \


######################
# gms相关配置
######################
$(call inherit-product-if-exists, vendor/gapps/arm64/arm64-vendor.mk)


######################
# zygisk
######################
$(call inherit-product-if-exists, vendor/magisk/device.mk)
