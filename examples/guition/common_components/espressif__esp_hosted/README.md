# esp_hosted_mcu_ot — Modified esp-hosted-mcu

Local copy of [espressif/esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu)
with custom patches for OpenThread over SDIO ETH_IF.

## Modifications

The following files have been patched from the upstream `main` branch:

### `slave/main/esp_hosted_coprocessor.c`
- Added **weak** `esp_hosted_coprocessor_eth_rx()` callback (lines 66–70)
- Added `ESP_ETH_IF` case in `process_rx_pkt()` that calls the callback (lines 727–728)
- Added `esp_hosted_coprocessor_eth_tx()` function for sending ETH_IF packets from
  the slave to the host (lines 850+)

### `slave/main/esp_hosted_coprocessor.h`
- Added declarations for `esp_hosted_coprocessor_eth_rx()` and
  `esp_hosted_coprocessor_eth_tx()`

## Purpose

Enables esp-hosted to carry OpenThread Spinel frames over the existing SDIO
transport using `ESP_ETH_IF` as the interface type, without modifying the core
esp-hosted transport or control path.

- **slave_ot** (ESP32-C6): Overrides the weak `eth_rx` to feed Spinel frames
  into the OT stack, calls `eth_tx` to send responses back.
- **usb_extend_screen** (ESP32-P4): Uses the host-side esp-hosted driver to
  send/receive ETH_IF frames, bridging them to USB CDC for `ot-daemon`.

## Directories

| Directory | Used by |
|-----------|---------|
| `common/` | Both host and slave projects |
| `slave/`  | `slave_ot` (C6 firmware) |
| `host/`   | `usb_extend_screen` (P4 firmware) |
