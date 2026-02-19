## USB Extension Screen Example

Try with [LaunchPad](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://dl.espressif.com/AE/esp-iot-solution/config.toml)

The USB Extension Screen Example allows the P4 development board to be used as an additional screen for Windows. It supports the following features:

* Supports a screen refresh rate of 1024*600@60FPS
* Supports up to five touch points
* Supports audio input and output

## Required Hardware

* Development Board
  1. [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html#getting-started) development board
  2. A 1024*600 MIPI screen from the development kit
  3. A speaker

* Connections
  1. Connect the high-speed USB port on the development board to the PC

## Compilation and Flashing

This is currently based off of tag release/v5.5
Commit ref: 87912cd291d68f4319f13695718af6754879a83f

### P4 Device Side

Build the project and flash it to the board, then run the monitor tool to view serial output:

* Run `. ./export.sh` to set up the IDF environment
* Run `idf.py set-target esp32p4` to set the target chip
* If there are any errors in the previous step, run `pip install "idf-component-manager~=1.1.4"` to upgrade your component manager
* Run `idf.py -p PORT flash monitor` to build, flash, and monitor the project

(To exit the serial monitor, press `Ctrl-]`.)

Refer to the Getting Started Guide for all steps to configure and use ESP-IDF to build projects.

> Note: This example will fetch an AVI file online. Ensure that you are connected to the internet during the initial compilation.

### PC Side

For preparation, refer to the [windows_driver](./windows_driver/README.md)

![Demo](https://dl.espressif.com/AE/esp-iot-solution/p4_usb_extern_screen.gif)

## Other Issues

### Touch Screen Does Not Control the P4 Screen

* In the Control Panel, select `Tablet PC Settings`
* In the configuration section, select `Setup`
* Follow the prompts to choose the P4 extension screen

### Adjusting JPEG Image Quality

* Modify the `string_desc_arr` vendor interface string in the `usb_descriptor.c` file. Change `Ejpg4` to the desired image quality level; the higher the number, the better the quality, but it will use more memory for the same frame.

### Modify Screen Resolution

- Update the `usb_descriptor.c` file by changing the `string_desc_arr` vendor interface string. Modify `R1024x600` to the desired screen resolution.

**Note**: Currently, the driver does not support portrait mode screens. Please use hardware that is designed for landscape orientation.

---

## OpenThread RCP Bridge (Guition JC1060P470)

On boards with an on-board ESP32-C6 connected via SDIO (e.g. the Guition
JC1060P470), this firmware additionally provides:

- A **USB CDC ACM serial port** that bridges OpenThread Spinel frames to the C6
  over the SDIO `ESP_ETH_IF` channel.
- **Automatic OTA** of the C6 coprocessor firmware on first boot.

### How the OTA works

The P4 flash image includes a **LittleFS partition** (`storage`, 2 MB at
`0x610000`) containing `slave_ot.bin` — a custom esp-hosted coprocessor firmware
that adds OpenThread 802.15.4 RCP support.

On every boot the P4:

1. Connects to the C6 via esp-hosted SDIO.
2. Reads the C6's firmware version.
3. If the C6 reports version **1.250.x** → it is already running our OT RCP
   firmware → skip OTA, start the bridge immediately.
4. If the C6 reports **any other version** (e.g. stock `2.11.7`, blank flash,
   etc.) → OTA-update the C6 with the embedded `slave_ot.bin`, activate, and
   restart.

### Firmware version strategy: `1.250.0`

The slave_ot firmware deliberately uses version **1.250.0**:

| Property | Value | Reason |
|---|---|---|
| Major | **1** | Below mainline esp-hosted (≥ 2.x) |
| Minor | **250** | Recognisable as our custom OT RCP build |

**This means:** if you later flash a *different* P4 application that uses stock
esp-hosted (e.g. a normal Wi-Fi app), it will see `1.250.0` as outdated and
automatically OTA the C6 back to the latest mainline esp-hosted Wi-Fi coprocessor
firmware — with current TLS certificates, security patches, etc. The board is
always recoverable to normal Wi-Fi operation simply by reflashing the P4.

### Building with OT bridge support

The OT bridge is controlled by Kconfig options (under `Component config →
OT Bridge Configuration`):

- `CONFIG_OT_BRIDGE_ENABLE` — master enable
- `CONFIG_OT_BRIDGE_TRANSPORT_SDIO` — use SDIO ETH_IF (default for Guition
  boards)
- `CONFIG_OT_SLAVE_OTA_ON_BOOT` — enable automatic C6 OTA check at boot

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The flash command writes 4 partitions: bootloader, partition table, app, and the
LittleFS storage image. No separate C6 flashing step is required — the P4 handles
it via OTA on first boot.
