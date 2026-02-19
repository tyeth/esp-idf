/*
 * SPDX-FileCopyrightText: 2026 Espressif / Guition
 * SPDX-License-Identifier: Apache-2.0
 *
 * Slave OTA via LittleFS — header
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform slave OTA from a .bin stored in the LittleFS "storage" partition.
 *
 * Mounts LittleFS, finds the first .bin, validates the ESP image header,
 * then streams it to the slave via esp_hosted_slave_ota_begin/write/end.
 *
 * @param delete_after_use  Delete the .bin from LittleFS after successful OTA.
 * @return
 *   - ESP_HOSTED_SLAVE_OTA_COMPLETED   on success
 *   - ESP_HOSTED_SLAVE_OTA_NOT_REQUIRED if version is the same
 *   - ESP_HOSTED_SLAVE_OTA_FAILED      on error
 */
int slave_ota_littlefs_perform(bool delete_after_use);

#ifdef __cplusplus
}
#endif
