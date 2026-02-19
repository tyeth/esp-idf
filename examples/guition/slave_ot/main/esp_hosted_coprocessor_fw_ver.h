/*
 * Custom version override for slave_ot (Guition OT RCP firmware).
 *
 * Set deliberately high so that:
 *  - the P4 host OTA always considers this newer than stock esp-hosted
 *  - it is immediately recognisable as our custom build (250.x.x)
 *
 * The stock esp-hosted-mcu header defines 2.11.7; we shadow it here
 * because slave_ot/main/ appears earlier on the include path than the
 * esp_hosted_slave component's copy.
 */
#ifndef __ESP_HOSTED_COPROCESSOR_FW_VER_H__
#define __ESP_HOSTED_COPROCESSOR_FW_VER_H__

#define PROJECT_VERSION_MAJOR_1 1
#define PROJECT_VERSION_MINOR_1 250
#define PROJECT_VERSION_PATCH_1 0

#endif /* __ESP_HOSTED_COPROCESSOR_FW_VER_H__ */
