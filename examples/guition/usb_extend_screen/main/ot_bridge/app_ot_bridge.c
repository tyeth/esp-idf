/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OpenThread UART ↔ USB CDC ACM bridge
 *
 * Bridges data between a USB CDC ACM virtual serial port exposed to
 * the PC host and a hardware UART connected to an ESP32-C6 running
 * OpenThread RCP firmware.  This lets tools such as `ot-ctl`,
 * `ot-daemon`, or OpenThread Border Router talk to the C6 over USB.
 */

#include "sdkconfig.h"

#if defined(CONFIG_OT_BRIDGE_ENABLE) && CONFIG_OT_BRIDGE_ENABLE

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "tusb.h"
#include "app_ot_bridge.h"

static const char *TAG = "ot_bridge";

/* ------------------------------------------------------------------ */
/*  UART helpers                                                       */
/* ------------------------------------------------------------------ */

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
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_param_config(CONFIG_OT_BRIDGE_UART_PORT_NUM, &uart_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = uart_set_pin(CONFIG_OT_BRIDGE_UART_PORT_NUM,
                       CONFIG_OT_BRIDGE_UART_TX_PIN,
                       CONFIG_OT_BRIDGE_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    return ret;
}

/* ------------------------------------------------------------------ */
/*  Bridge task – runs forever                                         */
/* ------------------------------------------------------------------ */

#define CDC_ITF  0   /* CDC instance index inside TinyUSB */
#define BUF_SIZE 512

static void ot_bridge_task(void *arg)
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
            /* Only write when the host has the port open (DTR set). */
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
                    if (chunk > avail) {
                        chunk = avail;
                    }
                    tud_cdc_n_write(CDC_ITF, buf + written, chunk);
                    written += chunk;
                }
                tud_cdc_n_write_flush(CDC_ITF);
                ESP_LOGD(TAG, "UART→CDC %d bytes", len);
            }
        }

        /* Yield if nothing happened */
        if (len <= 0 && !tud_cdc_n_available(CDC_ITF)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ------------------------------------------------------------------ */
/*  TinyUSB CDC callbacks                                              */
/* ------------------------------------------------------------------ */

/* Called when the host sets DTR / RTS via SET_CONTROL_LINE_STATE.     */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    ESP_LOGI(TAG, "CDC line state itf=%u dtr=%d rts=%d", itf, dtr, rts);
}

/* Called when the host changes line coding (baud, parity, etc.).     */
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding)
{
    ESP_LOGI(TAG, "CDC line coding itf=%u baud=%"PRIu32" stop=%u parity=%u data=%u",
             itf,
             p_line_coding->bit_rate,
             p_line_coding->stop_bits,
             p_line_coding->parity,
             p_line_coding->data_bits);

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
}

/* ------------------------------------------------------------------ */
/*  Public init                                                        */
/* ------------------------------------------------------------------ */

esp_err_t app_ot_bridge_init(void)
{
    esp_err_t ret = ot_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
                        ot_bridge_task, "ot_bridge",
                        CONFIG_OT_BRIDGE_TASK_STACK_SIZE,
                        NULL,
                        CONFIG_OT_BRIDGE_TASK_PRIORITY,
                        NULL, tskNO_AFFINITY);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OT bridge task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OpenThread UART↔CDC bridge initialised");
    return ESP_OK;
}

#endif /* CONFIG_OT_BRIDGE_ENABLE */
