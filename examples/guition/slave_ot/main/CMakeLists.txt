idf_component_register(
    SRCS
        "app_main.c"
        "ot_spinel_sdio.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES openthread ieee802154 nvs_flash esp_event esp_hosted_slave
)

# Wrap otPlatUart* functions so we can redirect Spinel I/O to SDIO ETH_IF
# instead of a real UART (which doesn't exist on this board).
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=otPlatUartSend"
    "-Wl,--wrap=otPlatUartEnable"
    "-Wl,--wrap=otPlatUartDisable"
    "-Wl,--wrap=otPlatUartFlush"
)
