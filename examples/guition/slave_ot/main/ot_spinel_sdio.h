/*
 * SPDX-FileCopyrightText: 2026 Tyeth Gundry
 * SPDX-License-Identifier: Apache-2.0
 *
 * OpenThread Spinel ↔ ESP-Hosted ETH_IF Bridge
 *
 * This module bridges OpenThread Spinel HDLC frames between:
 *   - The ESP32-C6's native 802.15.4 radio (via OpenThread NCP)
 *   - The ESP-Hosted SDIO transport (ETH_IF channel) to the ESP32-P4 host
 *
 * The P4 host then forwards these frames over USB CDC ACM to the PC,
 * where ot-daemon/ot-ctl can communicate with the C6 radio.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the OpenThread Spinel ↔ SDIO bridge.
 *
 * This sets up:
 *   1. OpenThread platform with native 802.15.4 radio
 *   2. NCP HDLC layer with custom send callback (→ ETH_IF)
 *   3. Registers to receive ETH_IF packets from host (→ Spinel decoder)
 *
 * @return ESP_OK on success
 */
esp_err_t ot_spinel_sdio_init(void);

#ifdef __cplusplus
}
#endif
