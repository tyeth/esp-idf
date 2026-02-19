/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ESP_HOSTED_H__
#define __ESP_HOSTED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_hosted_os_abstraction.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_host_fw_ver.h"
#include "esp_hosted_misc.h"
#include "esp_hosted_ota.h"
#include "esp_hosted_event.h"
#include "esp_hosted_cp_gpio.h"

typedef struct esp_hosted_transport_config esp_hosted_config_t;

/* --------- Hosted Minimal APIs --------- */
int esp_hosted_init(void);
int esp_hosted_deinit(void);

int esp_hosted_connect_to_slave(void);
int esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *ver_info);

/* --------- ETH_IF (custom data) callback --------- */
/**
 * Callback type for receiving ETH_IF packets from the slave.
 * @param data   pointer to payload (caller owns; must be copied if needed)
 * @param len    payload length in bytes
 * @return ESP_OK
 */
typedef esp_err_t (*esp_hosted_eth_if_rx_cb_t)(const uint8_t *data, uint16_t len);

/**
 * Register a callback that will be invoked for every ESP_ETH_IF
 * packet received from the slave.  Pass NULL to unregister.
 */
esp_err_t esp_hosted_register_eth_if_rx_handler(esp_hosted_eth_if_rx_cb_t cb);

/**
 * Send data to the slave over ESP_ETH_IF interface.
 * @param data   payload to send
 * @param len    payload length
 * @return ESP_OK on success
 */
esp_err_t esp_hosted_eth_if_tx(const uint8_t *data, uint16_t len);

/* --------- Exhaustive API list --------- */
/*
 * 1. All Wi-Fi supported APIs
 *    File: host/api/src/esp_wifi_weak.c
 *
 * 2. Communication Bus APIs (Set and get transport config)
 *    File : host/api/include/esp_hosted_transport_config.h
 *
 * 3. Co-Processor OTA API
 *    File : host/api/include/esp_hosted_ota.h
 */

#ifdef __cplusplus
}
#endif

#endif /* __ESP_HOSTED_H__ */
