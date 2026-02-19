# find_newest_firmware.cmake
# Finds the newest .bin in slave_fw_bin/ and copies it to a temp dir
# for LittleFS image creation.
#
# Required:
#   SOURCE_COMPONENT_DIR  — component source dir (contains slave_fw_bin/)
#   BINARY_COMPONENT_DIR  — component build dir  (temp_littlefs/ created here)
#   OTA_ACTION            — "prepare_littlefs"
# Optional:
#   COMPONENT_NAME        — label for log messages

if(NOT DEFINED COMPONENT_NAME)
    set(COMPONENT_NAME "OTA")
endif()

set(SOURCE_DIR "${SOURCE_COMPONENT_DIR}/slave_fw_bin")
set(TEMP_DIR   "${BINARY_COMPONENT_DIR}/temp_littlefs")

message(STATUS "${COMPONENT_NAME}: Searching for firmware in ${SOURCE_DIR}")

file(GLOB FIRMWARE_FILES "${SOURCE_DIR}/*.bin")
if(NOT FIRMWARE_FILES)
    message(FATAL_ERROR "${COMPONENT_NAME}: No .bin files in ${SOURCE_DIR}")
endif()

# Pick newest by timestamp
set(NEWEST_FILE "")
set(NEWEST_TS 0)
foreach(F ${FIRMWARE_FILES})
    file(TIMESTAMP "${F}" TS "%s")
    message(STATUS "${COMPONENT_NAME}: ${F} ts=${TS}")
    if(TS GREATER NEWEST_TS)
        set(NEWEST_FILE "${F}")
        set(NEWEST_TS "${TS}")
    endif()
endforeach()

get_filename_component(NEWEST_NAME "${NEWEST_FILE}" NAME)
message(STATUS "${COMPONENT_NAME}: Selected ${NEWEST_NAME}")

if(OTA_ACTION STREQUAL "prepare_littlefs")
    file(REMOVE_RECURSE "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
    file(COPY "${NEWEST_FILE}" DESTINATION "${TEMP_DIR}")
    message(STATUS "${COMPONENT_NAME}: Prepared ${NEWEST_NAME} for LittleFS")
else()
    message(FATAL_ERROR "${COMPONENT_NAME}: Unknown OTA_ACTION: ${OTA_ACTION}")
endif()
