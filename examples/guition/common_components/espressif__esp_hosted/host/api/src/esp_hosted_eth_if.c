/*
 * SPDX-FileCopyrightText: 2026 Espressif / Guition
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side ETH_IF send/receive support for esp-hosted.
 * Allows application code to exchange raw Ethernet-style frames
 * (e.g. Spinel/OT) with the slave over the existing SDIO/SPI transport.
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_hosted.h"
#include "esp_hosted_interface.h"
#include "transport_drv.h"

static const char *TAG = "esp_hosted_eth_if";

/* Global callback — set by esp_hosted_register_eth_if_rx_handler() */
static esp_hosted_eth_if_rx_cb_t s_eth_if_rx_cb = NULL;

esp_err_t esp_hosted_register_eth_if_rx_handler(esp_hosted_eth_if_rx_cb_t cb)
{
    s_eth_if_rx_cb = cb;
    ESP_LOGI(TAG, "ETH_IF RX handler %s", cb ? "registered" : "unregistered");
    return ESP_OK;
}

/* Called from the transport driver's RX dispatch when if_type == ESP_ETH_IF */
void esp_hosted_eth_if_rx_internal(const uint8_t *payload, uint16_t len)
{
    if (s_eth_if_rx_cb) {
        s_eth_if_rx_cb(payload, len);
    } else {
        ESP_LOGD(TAG, "ETH_IF pkt dropped (no handler), len=%u", len);
    }
}

static void eth_if_free_cb(void *ptr)
{
    if (ptr) free(ptr);
}

esp_err_t esp_hosted_eth_if_tx(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    /* esp_hosted_tx may hold the buffer beyond this call (zero-copy path)
     * so we must provide a copy that the transport can free.               */
    uint8_t *copy = malloc(len);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, data, len);

    int ret = esp_hosted_tx(ESP_ETH_IF, 0, copy, len,
                            H_BUFF_ZEROCOPY, copy, eth_if_free_cb, 0);
    if (ret) {
        ESP_LOGE(TAG, "esp_hosted_tx(ETH_IF) failed: %d", ret);
        free(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}
