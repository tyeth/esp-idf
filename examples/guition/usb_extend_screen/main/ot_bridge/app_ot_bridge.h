/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef CONFIG_OT_BRIDGE_ENABLE

/**
 * @brief Initialise the OpenThread UART ↔ USB CDC bridge.
 *
 * Sets up the UART connected to the ESP32-C6 coprocessor and starts
 * a FreeRTOS task that shuttles data between TinyUSB CDC ACM and the
 * UART.
 *
 * @return ESP_OK on success.
 */
esp_err_t app_ot_bridge_init(void);

#endif /* CONFIG_OT_BRIDGE_ENABLE */

#ifdef __cplusplus
}
#endif
