# slave_ot — OpenThread RCP over SDIO (ESP32-C6)

Custom ESP-Hosted coprocessor firmware that adds an **OpenThread 802.15.4 Radio
Co-Processor (RCP)** to the ESP32-C6, communicating with the ESP32-P4 host via
the existing SDIO transport using the `ESP_ETH_IF` interface type.

## Firmware version: `1.250.0`

This firmware deliberately reports version **1.250.0** — chosen so that:

- **`1.x` (major = 1)** is below all mainline esp-hosted releases (≥ 2.x). If a
  different application is later flashed to the P4 that uses stock esp-hosted, it
  will see `1.250.0` as outdated and automatically OTA-update the C6 back to a
  normal Wi-Fi coprocessor firmware. This means the board is always recoverable to
  standard Wi-Fi operation just by reflashing the P4.

- **`250` (minor)** is instantly recognisable as our custom OT RCP build — not a
  real esp-hosted release.

The version is set in two places:

| Where | Mechanism |
|---|---|
| `CMakeLists.txt` | `set(PROJECT_VER "1.250.0")` — sets `esp_app_desc_t.version` in the binary |
| `CMakeLists.txt` | `-DPROJECT_VERSION_MAJOR_1=1 -DPROJECT_VERSION_MINOR_1=250 -DPROJECT_VERSION_PATCH_1=0` — overrides the esp-hosted RPC version response |
| `main/esp_hosted_coprocessor_fw_ver.h` | Local header (not used at build time due to `-D` flags, kept for reference) |

## How it works

```
┌──────────────┐  USB CDC  ┌──────────────┐  SDIO    ┌──────────────┐
│  PC / Pi     │◄────────►│  ESP32-P4    │◄───────►│  ESP32-C6    │
│  ot-daemon   │  serial   │  CDC↔ETH_IF  │ ETH_IF   │  OT 802.15.4 │
└──────────────┘           │  bridge task │          │  RCP (this)  │
                           └──────────────┘          └──────────────┘
```

1. The P4 host forwards OpenThread Spinel frames between USB CDC and esp-hosted
   `ESP_ETH_IF`.
2. This C6 firmware receives ETH_IF packets via the esp-hosted slave driver,
   feeds them into the OpenThread RCP stack, and sends responses back.
3. `otPlatUart*` functions are wrapped (`--wrap`) to redirect Spinel I/O through
   the SDIO ETH_IF channel instead of a physical UART.

## Building

```bash
cd slave_ot
idf.py set-target esp32c6
idf.py build
```

Binary output: `build/slave_ot.bin` (~1.1 MB)

## Flashing

### Direct flash (via USB-JTAG on the C6)

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

### OTA from the P4

The `usb_extend_screen` P4 firmware embeds `slave_ot.bin` in a LittleFS partition.
On boot it checks the C6's version — if it is **not** `1.250.x`, it automatically
OTA-updates the C6 with the embedded binary. No manual C6 flashing is needed after
the initial P4 flash.

## Dependencies

- ESP-IDF ≥ 5.3
- Modified esp-hosted-mcu (`common_components/espressif__esp_hosted`)
- OpenThread + IEEE 802.15.4 components (from ESP-IDF)
