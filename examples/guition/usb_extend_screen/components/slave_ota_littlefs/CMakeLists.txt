# Slave OTA via LittleFS component
# Embeds the slave_ot firmware into a LittleFS partition image at build time.

set(SRC_LIST "slave_ota_littlefs.c")

idf_component_register(
    SRCS ${SRC_LIST}
    INCLUDE_DIRS "."
    PRIV_REQUIRES littlefs espressif__esp_hosted bootloader_support esp_app_format
)

# ---------- LittleFS partition image ----------
# We use the find_newest_firmware.cmake script (shared with the upstream
# esp-hosted-mcu example) to locate the newest .bin in slave_fw_bin/ and
# copy it into a temp dir, then littlefs_create_partition_image packages
# that temp dir into a flashable LittleFS image bound to the "storage"
# partition.

set(TEMP_LITTLEFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/temp_littlefs")

add_custom_target(prepare_littlefs_temp
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_COMPONENT_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY_COMPONENT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DCOMPONENT_NAME=slave_ota_lfs
        -DOTA_ACTION=prepare_littlefs
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../common_ota_scripts/find_newest_firmware.cmake
    COMMENT "Preparing slave firmware for LittleFS image"
    VERBATIM
)

# Rebuild when the firmware .bin changes
file(GLOB _FW_FILES "${CMAKE_CURRENT_SOURCE_DIR}/slave_fw_bin/*.bin")
if(_FW_FILES)
    add_custom_target(fw_files_dep DEPENDS ${_FW_FILES})
    add_dependencies(prepare_littlefs_temp fw_files_dep)
endif()

littlefs_create_partition_image(storage ${TEMP_LITTLEFS_DIR}
    FLASH_IN_PROJECT DEPENDS prepare_littlefs_temp)
