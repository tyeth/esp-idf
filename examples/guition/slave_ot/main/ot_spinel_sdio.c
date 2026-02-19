/*
 * SPDX-FileCopyrightText: 2026 Tyeth Gundry
 * SPDX-License-Identifier: Apache-2.0
 *
 * OpenThread Spinel ↔ ESP-Hosted ETH_IF Bridge
 *
 * Overrides otPlatUartSend (via --wrap linker flag) to route Spinel HDLC
 * frames through the esp-hosted SDIO transport using the ESP_ETH_IF channel.
 *
 * Incoming ETH_IF packets are fed into otPlatUartReceived() which drives
 * the NCP HDLC decoder in the OpenThread stack.
 */

#include "ot_spinel_sdio.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_hosted_coprocessor.h"

/* OpenThread platform APIs we interact with.
 * The otPlatUart* functions are declared in a private OT header
 * (examples/platforms/utils/uart.h), not in the public API.
 * We declare them explicitly since we provide --wrap replacements. */
#include "openthread/error.h"

extern otError otPlatUartSend(const uint8_t *aBuf, uint16_t aBufLength);
extern otError otPlatUartEnable(void);
extern otError otPlatUartDisable(void);
extern otError otPlatUartFlush(void);
extern void otPlatUartSendDone(void);
extern void otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength);

static const char *TAG = "ot_spinel_sdio";

/*
 * -----------------------------------------------------------------------
 *  otPlatUartSend wrapper
 *
 *  The real otPlatUartSend() (in esp_openthread_uart.c) tries to write()
 *  to a VFS UART fd that doesn't exist (we use HOST_CONNECTION_MODE_NONE).
 *
 *  This replacement sends the Spinel HDLC bytes through the esp-hosted
 *  SDIO transport on the ESP_ETH_IF channel to the P4 host.
 * -----------------------------------------------------------------------
 */

/*
 * __wrap_otPlatUartSend — called by the NcpSend callback from otNcpHdlcInit.
 * The NCP HDLC encoder calls NcpSend(buf, len) → otPlatUartSend(buf, len).
 * We intercept it here and route it over SDIO ETH_IF.
 */
otError __wrap_otPlatUartSend(const uint8_t *aBuf, uint16_t aBufLength)
{
    esp_err_t ret = esp_hosted_coprocessor_eth_tx(aBuf, aBufLength);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH_IF TX failed: %s", esp_err_to_name(ret));
        return OT_ERROR_FAILED;
    }

    /* The NCP HDLC layer expects otPlatUartSendDone() to be called after
     * a successful send. */
    otPlatUartSendDone();

    return OT_ERROR_NONE;
}

/* No-op wrappers — UART hardware doesn't exist */
otError __wrap_otPlatUartEnable(void)
{
    ESP_LOGI(TAG, "otPlatUartEnable (SDIO bridge, no-op)");
    return OT_ERROR_NONE;
}

otError __wrap_otPlatUartDisable(void)
{
    return OT_ERROR_NONE;
}

otError __wrap_otPlatUartFlush(void)
{
    return OT_ERROR_NONE;
}

/*
 * -----------------------------------------------------------------------
 *  ESP-Hosted ETH_IF → OpenThread Spinel RX path
 *
 *  The weak esp_hosted_coprocessor_eth_rx() in esp_hosted_coprocessor.c
 *  is overridden here. When the host sends a Spinel HDLC frame over
 *  SDIO ETH_IF, it arrives here and we feed it into the NCP decoder.
 * -----------------------------------------------------------------------
 */

void esp_hosted_coprocessor_eth_rx(const uint8_t *payload, uint16_t len)
{
    ESP_LOGD(TAG, "ETH_IF RX %u bytes → otPlatUartReceived", len);

    /* otPlatUartReceived feeds data into the NCP HDLC decoder.
     * It must be called from a task context (same task as OT mainloop
     * ideally), but since the recv_task calls process_rx_pkt which calls
     * us, and we're on a FreeRTOS task, this should be safe.
     *
     * Note: The OT NCP HDLC decoder handles partial frames / buffering.
     */
    otPlatUartReceived(payload, len);
}

/*
 * -----------------------------------------------------------------------
 *  Init
 * -----------------------------------------------------------------------
 */

esp_err_t ot_spinel_sdio_init(void)
{
    ESP_LOGI(TAG, "OpenThread Spinel ↔ SDIO ETH_IF bridge initialized");
    ESP_LOGI(TAG, "  TX: otPlatUartSend → esp_hosted_coprocessor_eth_tx (ESP_ETH_IF)");
    ESP_LOGI(TAG, "  RX: esp_hosted_coprocessor_eth_rx → otPlatUartReceived");

    /* Nothing to init here — the wrappers are linked at build time
     * and the esp_hosted_coprocessor_eth_rx override is linked via
     * strong symbol. The SDIO transport is initialized by
     * esp_hosted_coprocessor_init(). OpenThread is started separately
     * by esp_openthread_start(). */

    return ESP_OK;
}
