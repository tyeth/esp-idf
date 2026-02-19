# Wrapper component for esp-hosted-mcu slave firmware.
# This re-exports the slave sources under the name "esp_hosted_slave"
# so it doesn't collide with our project's "main" component.

set(ESP_HOSTED_MCU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../common_components/esp_hosted_mcu_ot")
set(slave_main "${ESP_HOSTED_MCU_DIR}/slave/main")

if(IS_DIRECTORY "${slave_main}/common")
    set(common_dir "${slave_main}/common")
else()
    set(common_dir "${ESP_HOSTED_MCU_DIR}/common")
endif()

set(srcs
    "${slave_main}/slave_control.c"
    "${common_dir}/proto/esp_hosted_rpc.pb-c.c"
    "${common_dir}/utils/esp_hosted_cli.c"
    "${slave_main}/protocomm_pserial.c"
    "${slave_main}/esp_hosted_coprocessor.c"
    "${slave_main}/mempool.c"
    "${slave_main}/stats.c"
    "${slave_main}/mempool_ll.c"
    "${slave_main}/host_power_save.c"
    "${slave_main}/slave_light_sleep.c"
    "${slave_main}/nw_split_router.c"
    "${slave_main}/transport_gpio_pin_guard.c"
)

if(CONFIG_SOC_BT_SUPPORTED)
    list(APPEND srcs "${slave_main}/slave_bt.c")
    if(CONFIG_IDF_TARGET_ESP32)
        list(APPEND srcs "${slave_main}/slave_bt_uart_esp32.c")
    elseif(CONFIG_IDF_TARGET_ESP32C3 OR CONFIG_IDF_TARGET_ESP32S3)
        list(APPEND srcs "${slave_main}/slave_bt_uart_esp32c3_s3.c")
    else()
        list(APPEND srcs "${slave_main}/slave_bt_uart_esp32xx.c")
    endif()
endif()

if(CONFIG_ESP_SDIO_HOST_INTERFACE)
    list(APPEND srcs "${slave_main}/sdio_slave_api.c")
elseif(CONFIG_ESP_SPI_HOST_INTERFACE)
    list(APPEND srcs "${slave_main}/spi_slave_api.c")
elseif(CONFIG_ESP_SPI_HD_MODE)
    list(APPEND srcs "${slave_main}/spi_hd_slave_api.c")
else()
    list(APPEND srcs "${slave_main}/uart_slave_api.c")
endif()

if(CONFIG_ESP_HOSTED_ENABLE_PEER_DATA_TRANSFER)
    list(APPEND srcs "${slave_main}/example_peer_data_transfer.c")
endif()

if(CONFIG_ESP_HOSTED_COPROCESSOR_EXAMPLE_LIGHT_SLEEP)
    list(APPEND srcs "${slave_main}/example_light_sleep.c")
endif()

if(CONFIG_ESP_HOSTED_COPROCESSOR_EXAMPLE_MQTT)
    list(APPEND srcs "${slave_main}/example_mqtt_client.c")
endif()

if(CONFIG_ESP_HOSTED_COPROCESSOR_EXAMPLE_HTTP_CLIENT)
    list(APPEND srcs "${slave_main}/example_http_client.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "${slave_main}"
        "${common_dir}"
        "${common_dir}/log"
        "${common_dir}/proto"
        "${common_dir}/rpc"
        "${common_dir}/transport"
        "${common_dir}/utils"
    REQUIRES
        protocomm protobuf-c esp_wifi driver esp_driver_sdio esp_driver_spi
        esp_driver_uart esp_driver_gpio esp_event nvs_flash
        esp_timer esp_pm console esp_http_server esp_app_format
        app_update wpa_supplicant esp_driver_sdmmc sdmmc
)

# Add directory of protocomm_priv.h to include paths
idf_component_get_property(protocomm_dir protocomm COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${protocomm_dir}/src/common")

# Add linker options to wrap esp_wifi_init function (same as original)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_wifi_init")
