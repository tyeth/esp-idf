// SPDX-License-Identifier: Apache-2.0
/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
//

#ifndef __NETWORK_ADAPTER_PRIV__H
#define __NETWORK_ADAPTER_PRIV__H

#include "esp_hosted_transport.h"
#include "esp_hosted_header.h"
#include "esp_hosted_interface.h"
#include "esp_hosted_transport_init.h"

esp_err_t esp_hosted_coprocessor_init(void);

/**
 * @brief Callback for ESP_ETH_IF packets received from host.
 *
 * Weak default drops the packet. Override to handle ETH_IF data
 * (e.g. OpenThread Spinel frames tunnelled over SDIO).
 *
 * @param payload  Raw payload (after esp_payload_header)
 * @param len      Length of payload in bytes
 */
void esp_hosted_coprocessor_eth_rx(const uint8_t *payload, uint16_t len);

/**
 * @brief Send a packet to the host via ESP_ETH_IF channel.
 *
 * Wraps the data in an esp-hosted frame with if_type=ESP_ETH_IF and
 * queues it for transmission to the host over the SDIO transport.
 *
 * @param data  Data to send
 * @param len   Length of data in bytes
 * @return ESP_OK on success
 */
esp_err_t esp_hosted_coprocessor_eth_tx(const uint8_t *data, uint16_t len);
#endif
