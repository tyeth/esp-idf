/*
 * SPDX-FileCopyrightText: 2026 Tyeth Gundry
 * SPDX-License-Identifier: Apache-2.0
 *
 * slave_ot — ESP-Hosted coprocessor with OpenThread Radio (RCP)
 *
 * This firmware runs on the ESP32-C6 coprocessor of the Guition JC1060P470
 * board. It initializes the esp-hosted SDIO transport layer (for OTA support
 * and the ETH_IF data channel), then starts OpenThread in Radio (NCP) mode.
 *
 * Spinel HDLC frames are tunnelled between the host (ESP32-P4) and the
 * OpenThread NCP via the ESP_ETH_IF channel over SDIO.
 *
 * The P4 host bridges these frames over USB CDC ACM to the PC, where
 * ot-daemon or ot-ctl can communicate with the 802.15.4 radio.
 */

#include <stdio.h>
#include <unistd.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_openthread.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"

#include "esp_hosted_coprocessor.h"
#include "ot_spinel_sdio.h"

static const char *TAG = "slave_ot";

/*
 * OpenThread configuration:
 *   - Radio: Native 802.15.4 (ESP32-C6 built-in)
 *   - Host:  NONE (we don't use a real UART/SPI/USB to the host;
 *            instead, otPlatUartSend is wrapped to use SDIO ETH_IF)
 *   - NVS:   for OpenThread persistent storage
 */

static const esp_openthread_config_t s_ot_config = {
    .netif_config = {0},  /* RCP doesn't need a network interface */
    .platform_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,
        },
        .port_config = {
            .storage_partition_name = "nvs",
            .netif_queue_size = 10,
            .task_queue_size = 10,
        },
    },
};

void app_main(void)
{
    /*
     * Cancel any pending OTA rollback FIRST, before anything that might crash.
     * If we were booted via OTA and the bootloader has rollback tracking
     * enabled, this marks our image as valid so the bootloader won't
     * revert to the previous firmware on the next reset.
     */
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "=== slave_ot: ESP-Hosted + OpenThread RCP ===");

    /* Initialize NVS — needed by both esp-hosted and OpenThread */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* eventfd needed by OpenThread mainloop (task queue + radio driver) */
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 2,
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    /*
     * Step 1: Initialize ESP-Hosted coprocessor (SDIO transport + RPC).
     *
     * This sets up:
     *  - SDIO slave interface to the P4 host
     *  - Protocomm RPC layer (handles OTA, config commands)
     *  - recv_task / send_task for packet processing
     *
     * We disable APP_MAIN in Kconfig so this doesn't provide app_main().
     * WiFi and BT are disabled in sdkconfig — only SDIO transport + RPC.
     */
    ESP_LOGI(TAG, "Initializing ESP-Hosted coprocessor (SDIO transport)...");
    ESP_ERROR_CHECK(esp_hosted_coprocessor_init());

    /*
     * Step 2: Initialize our Spinel ↔ SDIO bridge.
     *
     * This is mostly informational — the real work is done via:
     *  - __wrap_otPlatUartSend() → routes Spinel TX to ETH_IF
     *  - esp_hosted_coprocessor_eth_rx() → feeds Spinel RX from ETH_IF
     * These are linked in at build time.
     */
    ESP_LOGI(TAG, "Initializing OT Spinel ↔ SDIO bridge...");
    ESP_ERROR_CHECK(ot_spinel_sdio_init());

    /*
     * Step 3: Start OpenThread in Radio (RCP) mode.
     *
     * This creates the OT task which:
     *  - Initializes the OT platform (radio, alarms, etc.)
     *  - Calls otAppNcpInit() → otNcpHdlcInit(instance, NcpSend)
     *    where NcpSend() calls otPlatUartSend() (our wrapped version)
     *  - Runs the OT mainloop (select-based event loop)
     *
     * With HOST_CONNECTION_MODE_NONE, no real UART is initialized.
     * Our __wrap_otPlatUartSend sends through SDIO ETH_IF instead.
     */
    ESP_LOGI(TAG, "Starting OpenThread RCP...");
    ESP_ERROR_CHECK(esp_openthread_start(&s_ot_config));

    /* esp_openthread_start returns after the OT task is running.
     * The OT mainloop runs in its own task. We're done here. */
    ESP_LOGI(TAG, "slave_ot ready — OpenThread RCP via SDIO ETH_IF");

    /* Main task can sleep forever — all work happens in:
     *   - OT task (Spinel processing, radio events)
     *   - recv_task (SDIO RX from host → process_rx_pkt → eth_rx)
     *   - send_task (outgoing packets to host)
     */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
