# Slave OT — ESP-Hosted coprocessor with OpenThread Radio
# Runs on ESP32-C6, connected to ESP32-P4 host via SDIO
cmake_minimum_required(VERSION 3.16)

# Custom version: high enough to always be "newer" than stock esp-hosted,
# and recognisable as our build.  Must match main/esp_hosted_coprocessor_fw_ver.h
set(PROJECT_VER "1.250.0")

# The esp-hosted slave code lives in components/esp_hosted_slave/
# (a wrapper component that includes sources from esp-hosted-mcu
#  under a unique name to avoid colliding with our project main).

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Match the esp-hosted slave's exclusion of lwip and esp_netif
list(APPEND EXCLUDE_COMPONENTS lwip esp_netif)

# Components needed by esp-hosted slave + openthread
set(COMPONENTS
    esp_timer esptool_py bootloader main nvs_flash esp_rom esp_wifi
    protocomm driver esp_driver_sdio esp_pm
    openthread ieee802154
    esp_hosted_slave
)

project(slave_ot)
idf_build_set_property(COMPILE_OPTIONS "-fdiagnostics-color=always" APPEND)

# Override the esp-hosted version macros so the running firmware
# reports 250.0.1 (matching PROJECT_VER in the app descriptor).
idf_build_set_property(COMPILE_OPTIONS
    "-DPROJECT_VERSION_MAJOR_1=1;-DPROJECT_VERSION_MINOR_1=250;-DPROJECT_VERSION_PATCH_1=0"
    APPEND)
