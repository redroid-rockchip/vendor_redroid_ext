######################
# 显卡相关配置
######################
TARGET_BOARD_PLATFORM := rk3588
TARGET_BOARD_PLATFORM_GPU := mali-G610
TARGET_BOARD_HARDWARE := rk30board
TARGET_RK_GRALLOC_VERSION := 4
BOARD_USE_DRM := true

PRODUCT_HAVE_RKVPU := true

# ALLOW_MISSING_DEPENDENCIES := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
BUILD_BROKEN_DUP_RULES := true

include device/rockchip/common/gralloc.device.mk


######################
# wifi相关配置
######################
BOARD_WLAN_DEVICE           := emulator
BOARD_HOSTAPD_DRIVER        := NL80211
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB   := lib_driver_cmd_simulated
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_simulated
WPA_SUPPLICANT_VERSION      := VER_0_8_X
WIFI_DRIVER_FW_PATH_PARAM   := "/dev/null"
WIFI_DRIVER_FW_PATH_STA     := "/dev/null"
WIFI_DRIVER_FW_PATH_AP      := "/dev/null"
