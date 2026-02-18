# Changelog — 2026-02-18

## Feature: OpenThread UART ↔ USB CDC ACM Bridge (ESP32-C6 Coprocessor)

Adds an **optional USB CDC ACM serial port** to the existing USB composite device.
When enabled, the ESP32-P4 bridges data between the new virtual serial port (visible
to the PC host as `/dev/ttyACM*` or a COM port) and a hardware UART connected to an
ESP32-C6 running OpenThread RCP firmware.

This allows tools like `ot-ctl`, `ot-daemon`, or OpenThread Border Router on a
Raspberry Pi / Ubuntu / Windows host to communicate with the C6 over the same USB
cable that drives the display.

### USB Composite Device (when all interfaces enabled)

| Interface | Class | Purpose |
|---|---|---|
| 0 | Vendor | Display frame data (JPEG/RGB) from host → P4 LCD |
| 1 | HID | Multi-touch screen reports P4 → host |
| 2–3 | CDC ACM | **NEW** — Virtual serial port for OpenThread (C6 via UART) |
| 4–6 | Audio | UAC speaker + microphone |

### Architecture

```
┌──────────────┐  USB   ┌──────────────┐  UART  ┌──────────────┐
│  PC / Pi /   │◄─────►│  ESP32-P4    │◄─────►│  ESP32-C6    │
│  Ubuntu host │  CDC   │  (this fw)   │        │  (OT RCP)    │
│  ot-daemon   │  ACM   │  bridge task │        │              │
└──────────────┘        └──────────────┘        └──────────────┘
```

---

## Files Changed (all within `main/`)

### New: `main/ot_bridge/`

| File | Description |
|---|---|
| `Kconfig.ot_bridge` | Menuconfig options — enable/disable bridge, UART port number, TX/RX GPIO pins, baud rate (default 460800), buffer sizes, task priority and stack size. |
| `tusb_config_cdc.h` | Conditionally defines `CFG_TUD_CDC=1` and CDC buffer sizes based on `CONFIG_OT_BRIDGE_ENABLE`. When disabled, `CFG_TUD_CDC=0` and no CDC code is compiled. |
| `app_ot_bridge.h` | Public header — declares `app_ot_bridge_init()`. |
| `app_ot_bridge.c` | Full bridge implementation: UART driver init, FreeRTOS task that shuttles data bidirectionally between `tud_cdc_n_read/write()` and `uart_read/write_bytes()`. Implements TinyUSB CDC callbacks (`tud_cdc_line_state_cb`, `tud_cdc_line_coding_cb`) to log and optionally mirror host line coding changes to the UART. |

### Modified: `main/usb_device/tusb_config.h`

- Added `#include "tusb_config_cdc.h"` so TinyUSB sees the `CFG_TUD_CDC` define.

### Modified: `main/usb_device/usb_descriptors.h`

- **Interface enum**: Added `ITF_NUM_CDC` and `ITF_NUM_CDC_DATA` (conditional on `CFG_TUD_CDC`).
- **Endpoint enum**: Added `EPNUM_CDC_NOTIF` and `EPNUM_CDC_DATA`.
- **New string index enum** (`STRIDX_*`): Replaces hardcoded indices so that the string descriptor array positions auto-adjust regardless of which interfaces are compiled in.

### Modified: `main/usb_device/usb_descriptors.c`

- `CONFIG_TOTAL_LEN` now includes `TUD_CDC_DESC_LEN * CFG_TUD_CDC`.
- Configuration descriptor array: added `TUD_CDC_DESCRIPTOR(...)` block (guarded by `#if CFG_TUD_CDC`).
- String descriptor array: added `"OpenThread CDC"` entry.
- All interface descriptors now reference `STRIDX_*` enums instead of magic numbers.

### Modified: `main/app_usb.c`

- Includes `app_ot_bridge.h` when `CONFIG_OT_BRIDGE_ENABLE` is set.
- Calls `app_ot_bridge_init()` during USB init, after audio init and before starting the TinyUSB device task.

### Modified: `main/CMakeLists.txt`

- Added `"ot_bridge"` to both `SRC_DIRS` and `INCLUDE_DIRS`.

### Modified: `main/Kconfig.projbuild`

- Added `orsource "./ot_bridge/Kconfig.ot_bridge"` alongside the existing UAC Kconfig include.

---

## How to Use

### Build & Flash

```bash
# Enable the OT bridge in menuconfig
idf.py menuconfig
# → Example Configuration → OpenThread UART Bridge (C6 Coprocessor) → Enable
# → Set UART TX/RX GPIO pins to match your C6 wiring
# → Adjust baud rate if needed (default 460800)

idf.py build
idf.py -p /dev/ttyUSBx flash
```

### ESP32-C6 Side

Flash the OpenThread RCP firmware from ESP-IDF:

```bash
cd $IDF_PATH/examples/openthread/ot_rcp
idf.py -p /dev/ttyUSBx set-target esp32c6 build flash
```

Ensure the C6 RCP UART baud rate matches the bridge config.

### Host Side (Raspberry Pi / Ubuntu)

After plugging in the USB cable, the host will see:
- The display device (existing vendor interface)
- A new `/dev/ttyACM0` (or similar) serial port

Use OpenThread tools:

```bash
# Install ot-daemon / wpantund / ot-ctl
sudo ot-daemon -I wpan0 -v 'spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=460800'

# Or use ot-ctl directly
sudo ot-ctl
```

### Kconfig Defaults

| Option | Default |
|---|---|
| Enable | `n` (disabled) |
| UART port | 1 |
| Baud rate | 460800 |
| TX GPIO (P4→C6) | 24 |
| RX GPIO (P4←C6) | 25 |
| UART RX buffer | 2048 bytes |
| UART TX buffer | 2048 bytes |
| Task priority | 5 |
| Task stack | 4096 bytes |
