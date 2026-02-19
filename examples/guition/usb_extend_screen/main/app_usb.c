/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_private/usb_phy.h"
#include "usb_descriptors.h"
#include "device/usbd.h"
#include "app_usb.h"
#if defined(CONFIG_OT_BRIDGE_ENABLE) && CONFIG_OT_BRIDGE_ENABLE
#include "app_ot_bridge.h"
#endif
#if defined(CONFIG_OT_BRIDGE_TRANSPORT_SDIO)
#include "esp_hosted.h"
#endif
#if defined(CONFIG_OT_SLAVE_OTA_ON_BOOT)
#include "slave_ota_littlefs.h"
#include "esp_hosted_ota.h"
#endif

static const char *TAG = "app_usb";

//--------------------------------------------------------------------+
// USB PHY config
//--------------------------------------------------------------------+
static void usb_phy_init(void)
{
    usb_phy_handle_t phy_hdl;
    // Configure USB PHY
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .otg_mode = USB_OTG_MODE_DEVICE,
    };
    phy_conf.target = USB_PHY_TARGET_INT;
    usb_new_phy(&phy_conf, &phy_hdl);
}

static void tusb_device_task(void *arg)
{
    while (1) {
        tud_task();
    }
}

esp_err_t app_usb_init(void)
{
    esp_err_t ret = ESP_OK;

    usb_phy_init();
    bool usb_init = tusb_init();
    if (!usb_init) {
        ESP_LOGE(TAG, "USB Device Stack Init Fail");
        return ESP_FAIL;
    }

#if CFG_TUD_VENDOR
    ret = app_vendor_init();
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "app_vendor_init failed");
#endif

#if CFG_TUD_HID
    ret = app_hid_init();
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "app_hid_init failed");
#endif

#if CFG_TUD_AUDIO
    ret =  app_uac_init();
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "app_uac_init failed");
#endif

#if defined(CONFIG_OT_BRIDGE_ENABLE) && CONFIG_OT_BRIDGE_ENABLE

#if defined(CONFIG_OT_BRIDGE_TRANSPORT_SDIO)
    /* Initialise the esp-hosted SDIO link to the C6 slave */
    ESP_LOGI(TAG, "Initialising esp-hosted (SDIO) …");
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init failed");
    ESP_RETURN_ON_ERROR(esp_hosted_connect_to_slave(), TAG, "esp_hosted_connect_to_slave failed");
    ESP_LOGI(TAG, "esp-hosted connected to slave");

#if defined(CONFIG_OT_SLAVE_OTA_ON_BOOT)
    /* Check if the slave needs an OTA update from embedded LittleFS image */
    ESP_LOGI(TAG, "Checking slave firmware …");
    int ota_ret = slave_ota_littlefs_perform(
#if defined(CONFIG_OT_SLAVE_OTA_DELETE_AFTER_FLASH)
        true
#else
        false
#endif
    );
    if (ota_ret == ESP_HOSTED_SLAVE_OTA_COMPLETED) {
        ESP_LOGI(TAG, "Slave OTA succeeded — activating and restarting …");
        esp_hosted_slave_ota_activate();
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else if (ota_ret == ESP_HOSTED_SLAVE_OTA_NOT_REQUIRED) {
        ESP_LOGI(TAG, "Slave firmware is up to date");
    } else {
        ESP_LOGW(TAG, "Slave OTA check returned %d — continuing", ota_ret);
    }
#endif /* CONFIG_OT_SLAVE_OTA_ON_BOOT */
#endif /* CONFIG_OT_BRIDGE_TRANSPORT_SDIO */

    ret = app_ot_bridge_init();
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "app_ot_bridge_init failed");
#endif

    xTaskCreate(tusb_device_task, "tusb_device_task", 4096, NULL, CONFIG_USB_TASK_PRIORITY, NULL);
    return ret;
}

/************************************************** TinyUSB callbacks ***********************************************/
// Invoked when device is mounted
void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB Mount");
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB Un-Mount");
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    ESP_LOGI(TAG, "USB Suspend");
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    ESP_LOGI(TAG, "USB Resume");
}
