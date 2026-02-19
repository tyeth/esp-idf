/*
 * SPDX-FileCopyrightText: 2026 Espressif / Guition
 * SPDX-License-Identifier: Apache-2.0
 *
 * Slave OTA via LittleFS
 *
 * Adapted from esp-hosted-mcu/examples/host_performs_slave_ota/components/ota_littlefs
 *
 * Reads the slave firmware binary from a LittleFS partition ("storage"),
 * validates the ESP image header, compares versions, then streams it to
 * the coprocessor via the esp-hosted OTA RPC path.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_hosted_ota.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "slave_ota_littlefs.h"

static const char *TAG = "slave_ota_lfs";

#define CHUNK_SIZE 1500

/* ------------------------------------------------------------------ */
/*  Parse ESP image header from file to get version + computed size    */
/* ------------------------------------------------------------------ */
static esp_err_t parse_image_header(const char *path,
                                    size_t *fw_size,
                                    char *ver_str, size_t ver_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return ESP_FAIL;
    }

    esp_image_header_t ih;
    if (fread(&ih, sizeof(ih), 1, f) != 1) {
        fclose(f);
        return ESP_FAIL;
    }
    if (ih.magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Bad magic 0x%02x (expected 0xE9)", ih.magic);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Image: magic=0x%02x segs=%u hash=%u",
             ih.magic, ih.segment_count, ih.hash_appended);

    size_t offset = sizeof(ih);
    size_t total  = sizeof(ih);

    for (int i = 0; i < ih.segment_count; i++) {
        esp_image_segment_header_t sh;
        if (fseek(f, offset, SEEK_SET) != 0 ||
            fread(&sh, sizeof(sh), 1, f) != 1) {
            fclose(f);
            return ESP_FAIL;
        }
        total  += sizeof(sh) + sh.data_len;
        offset += sizeof(sh) + sh.data_len;

        /* First segment contains the app_desc */
        if (i == 0) {
            esp_app_desc_t ad;
            size_t ad_off = sizeof(ih) + sizeof(sh);
            if (fseek(f, ad_off, SEEK_SET) == 0 &&
                fread(&ad, sizeof(ad), 1, f) == 1) {
                size_t copy_len = ver_len - 1;
                if (copy_len > sizeof(ad.version)) copy_len = sizeof(ad.version);
                memcpy(ver_str, ad.version, copy_len);
                ver_str[copy_len] = '\0';
                ESP_LOGI(TAG, "FW version='%s' project='%s'",
                         ad.version, ad.project_name);
            } else {
                strlcpy(ver_str, "unknown", ver_len);
            }
        }
    }

    /* Alignment padding + checksum + optional SHA256 */
    size_t pad = (16 - (total % 16)) % 16;
    total += pad + 1;
    if (ih.hash_appended) total += 32;

    *fw_size = total;
    ESP_LOGI(TAG, "Image size: %u bytes", (unsigned)total);
    fclose(f);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Find first .bin in /littlefs                                      */
/* ------------------------------------------------------------------ */
static esp_err_t find_firmware(char *out, size_t max_len)
{
    DIR *d = opendir("/littlefs");
    if (!d) {
        ESP_LOGE(TAG, "Cannot open /littlefs");
        return ESP_FAIL;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strstr(e->d_name, ".bin")) {
            int n = snprintf(out, max_len, "/littlefs/%s", e->d_name);
            if (n < 0 || (size_t)n >= max_len) {
                ESP_LOGW(TAG, "Path truncated for %s", e->d_name);
            }
            closedir(d);
            ESP_LOGI(TAG, "Found firmware: %s", out);
            return ESP_OK;
        }
    }
    closedir(d);
    ESP_LOGE(TAG, "No .bin found in /littlefs");
    return ESP_ERR_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
int slave_ota_littlefs_perform(bool delete_after_use)
{
    esp_err_t ret;
    char path[256];

    /* Mount LittleFS */
    esp_vfs_littlefs_conf_t lfs_cfg = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    ret = esp_vfs_littlefs_register(&lfs_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    /* Find .bin */
    ret = find_firmware(path, sizeof(path));
    if (ret != ESP_OK) {
        esp_vfs_littlefs_unregister("storage");
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    /* Validate header + get version */
    size_t fw_size;
    char new_ver[32];
    ret = parse_image_header(path, &fw_size, new_ver, sizeof(new_ver));
    if (ret != ESP_OK) {
        esp_vfs_littlefs_unregister("storage");
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    /* Check if slave is already running our custom OT RCP firmware (1.250.x).
     * If so, skip OTA.  Any other version (e.g. stock 2.11.7 or blank)
     * triggers an update so the C6 gets our build. */
    esp_hosted_coprocessor_fwver_t cur = {0};
    if (esp_hosted_get_coprocessor_fwversion(&cur) == ESP_OK) {
        ESP_LOGI(TAG, "Running slave FW %" PRIu32 ".%" PRIu32 ".%" PRIu32
                 ", embedded FW %s",
                 cur.major1, cur.minor1, cur.patch1, new_ver);
        if (cur.major1 == 1 && cur.minor1 == 250) {
            ESP_LOGI(TAG, "Slave already running custom OT RCP (1.250.x) — skipping OTA");
            esp_vfs_littlefs_unregister("storage");
            return ESP_HOSTED_SLAVE_OTA_NOT_REQUIRED;
        }
        ESP_LOGW(TAG, "Slave not running expected 1.250.x — will OTA");
    } else {
        ESP_LOGW(TAG, "Cannot read slave version — proceeding with OTA anyway");
    }

    /* Open and stream */
    FILE *f = fopen(path, "rb");
    if (!f) {
        esp_vfs_littlefs_unregister("storage");
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    ret = esp_hosted_slave_ota_begin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed: %s", esp_err_to_name(ret));
        fclose(f);
        esp_vfs_littlefs_unregister("storage");
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    uint8_t *chunk = malloc(CHUNK_SIZE);
    if (!chunk) {
        fclose(f);
        esp_vfs_littlefs_unregister("storage");
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    size_t total_written = 0;
    size_t n;
    while ((n = fread(chunk, 1, CHUNK_SIZE, f)) > 0) {
        ret = esp_hosted_slave_ota_write(chunk, n);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ota_write failed at %u bytes", (unsigned)total_written);
            free(chunk);
            fclose(f);
            esp_vfs_littlefs_unregister("storage");
            return ESP_HOSTED_SLAVE_OTA_FAILED;
        }
        total_written += n;
    }
    free(chunk);
    fclose(f);

    ESP_LOGI(TAG, "Wrote %u bytes to slave", (unsigned)total_written);

    ret = esp_hosted_slave_ota_end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_end failed: %s", esp_err_to_name(ret));
        esp_vfs_littlefs_unregister("storage");
        return ESP_HOSTED_SLAVE_OTA_FAILED;
    }

    ESP_LOGI(TAG, "Slave OTA completed successfully");

    if (delete_after_use) {
        if (unlink(path) == 0) {
            ESP_LOGI(TAG, "Deleted %s", path);
        }
    }

    esp_vfs_littlefs_unregister("storage");
    return ESP_HOSTED_SLAVE_OTA_COMPLETED;
}
