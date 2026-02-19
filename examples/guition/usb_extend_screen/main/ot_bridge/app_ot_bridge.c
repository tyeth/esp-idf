/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OpenThread ↔ USB CDC ACM bridge
 *
 * Bridges data between a USB CDC ACM virtual serial port exposed to the
 * PC host and the ESP32-C6 coprocessor running OpenThread RCP firmware.
 *
 * Two transport back-ends:
 *   1. SDIO via esp-hosted ETH_IF  (default — zero extra wires)
 *   2. Direct UART                 (fallback / debugging)
 *
 * In SDIO mode the esp-hosted host driver carries Spinel frames inside
 * ETH_IF packets over the existing SDIO link.  A FreeRTOS queue is used
 * to pass received packets from the esp-hosted RX callback to the bridge
 * task which then forwards them to the CDC port.
 */

#include "sdkconfig.h"

#if defined(CONFIG_OT_BRIDGE_ENABLE) && CONFIG_OT_BRIDGE_ENABLE

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "tusb.h"
#include "app_ot_bridge.h"

#if defined(CONFIG_OT_BRIDGE_TRANSPORT_UART)
#include "driver/uart.h"
#elif defined(CONFIG_OT_BRIDGE_TRANSPORT_SDIO)
#include "esp_hosted.h"
#endif

static const char *TAG = "ot_bridge";

#define CDC_ITF  0   /* CDC instance index inside TinyUSB */
#define BUF_SIZE 512

/* ================================================================== */
/*  SDIO (esp-hosted ETH_IF) transport                                */
/* ================================================================== */
#if defined(CONFIG_OT_BRIDGE_TRANSPORT_SDIO)

/* Queue carries (malloc'd) rx_pkt_t from the esp-hosted callback     */
typedef struct {
    uint8_t *data;
    uint16_t len;
} rx_pkt_t;

static QueueHandle_t s_rx_queue;

/*
 * Callback registered with esp-hosted.  Called from the esp-hosted RX
 * task whenever an ETH_IF packet arrives from the slave.
 */
static esp_err_t eth_if_rx_cb(const uint8_t *data, uint16_t len)
{
    rx_pkt_t pkt;
    pkt.data = malloc(len);
    if (!pkt.data) {
        ESP_LOGW(TAG, "ETH_IF rx: alloc failed, dropping %u bytes", len);
        return ESP_ERR_NO_MEM;
    }
    memcpy(pkt.data, data, len);
    pkt.len = len;
    if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "ETH_IF rx: queue full, dropping %u bytes", len);
        free(pkt.data);
    }
    return ESP_OK;
}

static void ot_bridge_sdio_task(void *arg)
{
    uint8_t buf[BUF_SIZE];
    rx_pkt_t pkt;

    ESP_LOGI(TAG, "OT bridge task started (SDIO / esp-hosted ETH_IF)");

    while (1) {
        /* ---- USB CDC → SDIO ETH_IF (host → C6) ---- */
        if (tud_cdc_n_available(CDC_ITF)) {
            int rx = tud_cdc_n_read(CDC_ITF, buf, sizeof(buf));
            if (rx > 0) {
                esp_err_t ret = esp_hosted_eth_if_tx(buf, rx);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "esp_hosted_eth_if_tx failed: %s", esp_err_to_name(ret));
                }
                ESP_LOGD(TAG, "CDC→SDIO %d bytes", rx);
            }
        }

        /* ---- SDIO ETH_IF → USB CDC (C6 → host) ---- */
        while (xQueueReceive(s_rx_queue, &pkt, 0) == pdTRUE) {
            if (tud_cdc_n_connected(CDC_ITF)) {
                uint32_t written = 0;
                while (written < pkt.len) {
                    uint32_t avail = tud_cdc_n_write_available(CDC_ITF);
                    if (avail == 0) {
                        tud_cdc_n_write_flush(CDC_ITF);
                        vTaskDelay(pdMS_TO_TICKS(1));
                        continue;
                    }
                    uint32_t chunk = pkt.len - written;
                    if (chunk > avail) chunk = avail;
                    tud_cdc_n_write(CDC_ITF, pkt.data + written, chunk);
                    written += chunk;
                }
                tud_cdc_n_write_flush(CDC_ITF);
                ESP_LOGD(TAG, "SDIO→CDC %u bytes", pkt.len);
            }
            free(pkt.data);
        }

        /* Yield if nothing happened */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

#endif /* CONFIG_OT_BRIDGE_TRANSPORT_SDIO */

/* ================================================================== */
/*  UART transport (largely unchanged from the original)              */
/* ================================================================== */
#if defined(CONFIG_OT_BRIDGE_TRANSPORT_UART)

static esp_err_t ot_uart_init(void)
{
    const uart_config_t uart_cfg = {
        .baud_rate  = CONFIG_OT_BRIDGE_UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret;
    ret = uart_driver_install(CONFIG_OT_BRIDGE_UART_PORT_NUM,
                              CONFIG_OT_BRIDGE_UART_RX_BUF_SIZE,
                              CONFIG_OT_BRIDGE_UART_TX_BUF_SIZE,
                              0, NULL, 0);
    if (ret != ESP_OK) return ret;

    ret = uart_param_config(CONFIG_OT_BRIDGE_UART_PORT_NUM, &uart_cfg);
    if (ret != ESP_OK) return ret;

    return uart_set_pin(CONFIG_OT_BRIDGE_UART_PORT_NUM,
                        CONFIG_OT_BRIDGE_UART_TX_PIN,
                        CONFIG_OT_BRIDGE_UART_RX_PIN,
                        UART_PIN_NO_CHANGE,
                        UART_PIN_NO_CHANGE);
}

static void ot_bridge_uart_task(void *arg)
{
    uint8_t buf[BUF_SIZE];

    ESP_LOGI(TAG, "OT bridge task started  UART%d @ %d baud  TX=GPIO%d  RX=GPIO%d",
             CONFIG_OT_BRIDGE_UART_PORT_NUM,
             CONFIG_OT_BRIDGE_UART_BAUD_RATE,
             CONFIG_OT_BRIDGE_UART_TX_PIN,
             CONFIG_OT_BRIDGE_UART_RX_PIN);

    while (1) {
        /* ---- USB CDC → UART (host → C6) ---- */
        if (tud_cdc_n_available(CDC_ITF)) {
            int rx = tud_cdc_n_read(CDC_ITF, buf, sizeof(buf));
            if (rx > 0) {
                uart_write_bytes(CONFIG_OT_BRIDGE_UART_PORT_NUM, buf, rx);
                ESP_LOGD(TAG, "CDC→UART %d bytes", rx);
            }
        }

        /* ---- UART → USB CDC (C6 → host) ---- */
        int len = uart_read_bytes(CONFIG_OT_BRIDGE_UART_PORT_NUM,
                                  buf, sizeof(buf),
                                  pdMS_TO_TICKS(5));
        if (len > 0) {
            if (tud_cdc_n_connected(CDC_ITF)) {
                uint32_t written = 0;
                while (written < (uint32_t)len) {
                    uint32_t avail = tud_cdc_n_write_available(CDC_ITF);
                    if (avail == 0) {
                        tud_cdc_n_write_flush(CDC_ITF);
                        vTaskDelay(pdMS_TO_TICKS(1));
                        continue;
                    }
                    uint32_t chunk = (uint32_t)(len) - written;
                    if (chunk > avail) chunk = avail;
                    tud_cdc_n_write(CDC_ITF, buf + written, chunk);
                    written += chunk;
                }
                tud_cdc_n_write_flush(CDC_ITF);
                ESP_LOGD(TAG, "UART→CDC %d bytes", len);
            }
        }

        if (len <= 0 && !tud_cdc_n_available(CDC_ITF)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

#endif /* CONFIG_OT_BRIDGE_TRANSPORT_UART */

/* ================================================================== */
/*  TinyUSB CDC callbacks                                             */
/* ================================================================== */

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    ESP_LOGI(TAG, "CDC line state itf=%u dtr=%d rts=%d", itf, dtr, rts);
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding)
{
    ESP_LOGI(TAG, "CDC line coding itf=%u baud=%"PRIu32" stop=%u parity=%u data=%u",
             itf,
             p_line_coding->bit_rate,
             p_line_coding->stop_bits,
             p_line_coding->parity,
             p_line_coding->data_bits);

#if defined(CONFIG_OT_BRIDGE_TRANSPORT_UART)
    /* Optionally re-configure UART to match host settings */
    uart_config_t uart_cfg = {
        .baud_rate  = (int)p_line_coding->bit_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    switch (p_line_coding->data_bits) {
    case 5: uart_cfg.data_bits = UART_DATA_5_BITS; break;
    case 6: uart_cfg.data_bits = UART_DATA_6_BITS; break;
    case 7: uart_cfg.data_bits = UART_DATA_7_BITS; break;
    default: uart_cfg.data_bits = UART_DATA_8_BITS; break;
    }
    switch (p_line_coding->parity) {
    case 1: uart_cfg.parity = UART_PARITY_ODD;  break;
    case 2: uart_cfg.parity = UART_PARITY_EVEN; break;
    default: uart_cfg.parity = UART_PARITY_DISABLE; break;
    }
    switch (p_line_coding->stop_bits) {
    case 2: uart_cfg.stop_bits = UART_STOP_BITS_2; break;
    default: uart_cfg.stop_bits = UART_STOP_BITS_1; break;
    }
    uart_param_config(CONFIG_OT_BRIDGE_UART_PORT_NUM, &uart_cfg);
#endif
}

/* ================================================================== */
/*  Public init                                                        */
/* ================================================================== */

esp_err_t app_ot_bridge_init(void)
{
    TaskFunction_t task_fn;

#if defined(CONFIG_OT_BRIDGE_TRANSPORT_SDIO)
    s_rx_queue = xQueueCreate(32, sizeof(rx_pkt_t));
    if (!s_rx_queue) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        return ESP_ERR_NO_MEM;
    }

    /* Register our ETH_IF RX callback with esp-hosted */
    esp_err_t ret = esp_hosted_register_eth_if_rx_handler(eth_if_rx_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ETH_IF RX handler: %s", esp_err_to_name(ret));
        return ret;
    }

    task_fn = ot_bridge_sdio_task;
    ESP_LOGI(TAG, "OT bridge: SDIO / esp-hosted ETH_IF transport");

#elif defined(CONFIG_OT_BRIDGE_TRANSPORT_UART)
    esp_err_t ret = ot_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    task_fn = ot_bridge_uart_task;
    ESP_LOGI(TAG, "OT bridge: UART transport");

#else
    #error "No OT bridge transport selected"
#endif

    BaseType_t ok = xTaskCreatePinnedToCore(
                        task_fn, "ot_bridge",
                        CONFIG_OT_BRIDGE_TASK_STACK_SIZE,
                        NULL,
                        CONFIG_OT_BRIDGE_TASK_PRIORITY,
                        NULL, tskNO_AFFINITY);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OT bridge task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OpenThread CDC bridge initialised");
    return ESP_OK;
}

#endif /* CONFIG_OT_BRIDGE_ENABLE */

