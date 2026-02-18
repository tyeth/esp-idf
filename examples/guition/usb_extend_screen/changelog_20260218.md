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

**Current state (UART bridge — works for boards with UART wired):**
```
┌──────────────┐  USB   ┌──────────────┐  UART  ┌──────────────┐
│  PC / Pi /   │◄─────►│  ESP32-P4    │◄─────►│  ESP32-C6    │
│  Ubuntu host │  CDC   │  (this fw)   │        │  (OT RCP)    │
│  ot-daemon   │  ACM   │  bridge task │        │              │
└──────────────┘        └──────────────┘        └──────────────┘
```

**Target state (SDIO tunnel — for Guition JC1060P470 and similar boards):**
```
┌──────────────┐  USB   ┌──────────────┐  SDIO   ┌──────────────┐
│  PC / Pi /   │◄─────►│  ESP32-P4    │◄──────►│  ESP32-C6    │
│  Ubuntu host │  CDC   │  CDC↔ETH_IF  │ ETH_IF  │  OT 802.15.4 │
│  ot-daemon   │  ACM   │  bridge task │ SER_IF  │  + OTA only   │
└──────────────┘        └──────────────┘         └──────────────┘
```
See "Chosen Approach" section below for full implementation plan.

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
| Enable | `y` (enabled) |
| UART port | 1 |
| Baud rate | 460800 |
| TX GPIO (P4→C6) | 24 |
| RX GPIO (P4←C6) | 25 |
| UART RX buffer | 2048 bytes |
| UART TX buffer | 2048 bytes |
| Task priority | 5 |
| Task stack | 4096 bytes |

---

## OpenThread over ESP-Hosted SDIO — Research & Options

### Problem

The Guition JC1060P470 board connects the ESP32-P4 to the ESP32-C6 **only via SDIO**
(for esp-hosted WiFi/BT). There are **no UART pins routed** between the two chips.
The CDC ACM bridge code above works correctly for the USB side, but there is no
physical UART path to carry Spinel (OpenThread RCP) data to/from the C6.

### Board Wiring (P4 ↔ C6)

| Signal | P4 GPIO | C6 GPIO | Notes |
|--------|---------|---------|-------|
| SDIO CMD | 19 | 18 | |
| SDIO CLK | 18 | 19 | |
| SDIO D0 | 14 | 20 | FIB variant |
| SDIO D1 | 15 | 21 | FIB variant |
| SDIO D2 | 16 | 22 | |
| SDIO D3 | 17 | 23 | |
| EN (reset) | 23 | — | P4 can reset C6 |
| WKUP | 6 | — | Wake signal |

**No UART TX/RX, no SPI, no other data lines.**

### ESP-Hosted Protocol Channels

The SDIO link multiplexes these interface types:

| `if_type` | Purpose | Status |
|-----------|---------|--------|
| `ESP_STA_IF` | WiFi Station | Active |
| `ESP_AP_IF` | WiFi SoftAP | Active |
| `ESP_SERIAL_IF` | Protobuf RPC | Active (also carries OTA) |
| `ESP_HCI_IF` | Bluetooth HCI | Active |
| `ESP_PRIV_IF` | Init/capability negotiation | Active |
| `ESP_TEST_IF` | Raw throughput debug | Debug only |
| `ESP_ETH_IF` | Ethernet | **Defined but unused** |

### ESP-Hosted OTA Support

The slave firmware supports OTA via protobuf RPC over SDIO:
- Commands: `Req_OTABegin` (272), `Req_OTAWrite` (273), `Req_OTAEnd` (274)
- Partition table has `ota_0` + `ota_1` slots (2× 1536K each)
- The C6 can be reflashed without physical access

### OpenThread RCP Transports (ESP-IDF)

| Mode | Supported | Notes |
|------|-----------|-------|
| UART | Yes | Default, 460800 baud |
| SPI | Yes | SPI slave mode |
| USB Serial JTAG | Yes | Native USB on C6 |
| **SDIO** | **No** | Not implemented |

### Chosen Approach: OT-only C6 slave with Spinel over `ESP_ETH_IF` + OTA retained

**Decision:** Use `ESP_ETH_IF` (already defined, value 7, currently unused) as the
transport channel for OpenThread Spinel frames over SDIO. The C6 runs OpenThread
only (no WiFi) but keeps the esp-hosted SDIO transport layer and protobuf RPC alive
so that OTA firmware updates still work. This means the C6 can be flashed back to
standard esp-hosted WiFi/BT at any time via OTA over SDIO.

#### Updated Architecture

```
┌──────────────┐  USB    ┌────────────────────┐  SDIO   ┌──────────────────┐
│  PC / Pi /   │  CDC    │    ESP32-P4         │         │   ESP32-C6       │
│  Ubuntu host │  ACM    │                     │         │                  │
│              │◄──────►│ USB CDC ↔ SDIO      │◄──────►│ Spinel↔ETH_IF    │
│  ot-daemon   │ Spinel  │ bridge (ETH_IF)     │ ETH_IF  │ OT 802.15.4     │
│              │         │                     │         │ radio            │
│  OTA tool    │  CDC    │ OTA cmds →          │ SER_IF  │ OTA handlers     │
│  (optional)  │  or PC  │ ESP_SERIAL_IF       │◄──────►│ (esp_ota API)    │
└──────────────┘         └────────────────────┘         └──────────────────┘
```

#### What Exists (completed in this session)

| Component | Location | Status |
|-----------|----------|--------|
| USB CDC ACM in composite device | `main/usb_device/` (tusb_config, descriptors) | **Done** |
| CDC ↔ UART bridge task | `main/ot_bridge/app_ot_bridge.c` | **Done** (needs refactor — see below) |
| Kconfig for OT bridge | `main/ot_bridge/Kconfig.ot_bridge` | **Done** |
| Build system integration | `main/CMakeLists.txt`, `Kconfig.projbuild` | **Done** |
| Firmware builds and flashes | `idf.py build && flash` | **Verified** |

#### What Needs Building (future sessions)

##### Phase 1: C6 Slave Firmware — OpenThread + esp-hosted transport

**Location:** Create a new project, e.g. `examples/guition/slave_ot/` (fork of `slave/`)

**Key files to modify in the slave:**

1. **`main/app_main.c`** — `process_rx_pkt()`
   - Add `case ESP_ETH_IF:` handler that feeds received data into the OpenThread
     Spinel HDLC decoder
   - Current code ignores `ESP_ETH_IF` packets

2. **`main/slave_control.c`** — Keep ALL existing OTA handlers intact:
   - `req_ota_begin_handler()` (cmd 272)
   - `req_ota_write_handler()` (cmd 273)
   - `req_ota_end_handler()` (cmd 274)
   - These use `esp_ota_begin/write/end` and work over `ESP_SERIAL_IF`

3. **New file `main/ot_radio.c`** — OpenThread radio integration:
   - Init ESP-IDF OpenThread radio driver (`esp_openthread_radio_init()`)
   - Register Spinel frame TX callback that wraps frames in esp-hosted packet
     with `if_type = ESP_ETH_IF` and sends via `send_to_host()` 
   - Register Spinel frame RX handler called from `process_rx_pkt()` ETH_IF case
   - The C6 has a dedicated IEEE 802.15.4 radio — no conflict with keeping
     the SDIO slave transport running

4. **`sdkconfig.defaults.esp32c6`** — Add:
   ```
   CONFIG_OPENTHREAD_ENABLED=y
   CONFIG_OPENTHREAD_RADIO_NATIVE=y
   # Disable WiFi to save memory, OT-only mode
   CONFIG_ESP_WIFI_ENABLED=n
   CONFIG_BT_ENABLED=n
   ```

5. **Partition table** — Keep `ota_0`/`ota_1` partitions so OTA still works.
   The OT-only firmware will be smaller than WiFi+BT, so it fits easily.

6. **`adapter.h`** — No changes needed. `ESP_ETH_IF = 7` already exists.

##### Phase 2: P4 Host Side — SDIO ↔ USB CDC Bridge

**Location:** Modify `main/ot_bridge/` in the `usb_extend_screen` project

**Key changes:**

1. **Refactor `app_ot_bridge.c`** — Replace UART transport with SDIO/esp-hosted:
   - Instead of `uart_read_bytes()` / `uart_write_bytes()`, use the esp-hosted
     host driver API to send/receive `ESP_ETH_IF` frames
   - The bridge task becomes: USB CDC ↔ esp-hosted ETH_IF (not UART)
   - Keep the UART option behind a Kconfig toggle for boards that DO have
     UART wired (bodge wire scenario)

2. **Add esp-hosted host dependency** to `idf_component.yml`:
   ```yaml
   espressif/esp_hosted:
     version: ">=0.0.27"
   ```

3. **`Kconfig.ot_bridge`** — Add transport selection:
   ```
   choice OT_BRIDGE_TRANSPORT
       prompt "OpenThread bridge transport to C6"
       default OT_BRIDGE_TRANSPORT_SDIO
       
       config OT_BRIDGE_TRANSPORT_SDIO
           bool "SDIO (via esp-hosted ETH_IF channel)"
       config OT_BRIDGE_TRANSPORT_UART
           bool "UART (direct wired)"
   endchoice
   ```

4. **SDIO transport functions** (new file `main/ot_bridge/ot_sdio_transport.c`):
   - `ot_sdio_send(buf, len)` — Wrap in esp-hosted frame, `if_type=ESP_ETH_IF`, send
   - Register RX callback for `ESP_ETH_IF` frames from esp-hosted host driver
   - Feed received Spinel into `tud_cdc_n_write()`

##### Phase 3: OTA Tool (Optional)

Create a simple host-side utility (Python or C) that sends OTA commands over the
esp-hosted RPC channel to flash the C6 back to normal esp-hosted WiFi/BT or update
the OT firmware. This can run over:
- The USB CDC port (if we expose `ESP_SERIAL_IF` as a second CDC endpoint), or
- A dedicated OTA script that talks directly to the esp-hosted host driver on the P4

**Alternative:** The P4 firmware itself could embed the OTA binary and flash the C6
on first boot or via a command.

#### File Map (for future sessions)

```
examples/guition/
├── usb_extend_screen/          ← P4 firmware (THIS project)
│   ├── main/
│   │   ├── ot_bridge/
│   │   │   ├── Kconfig.ot_bridge        ✅ Done (add transport choice)
│   │   │   ├── tusb_config_cdc.h        ✅ Done
│   │   │   ├── app_ot_bridge.h          ✅ Done
│   │   │   ├── app_ot_bridge.c          ✅ Done (refactor UART→SDIO)
│   │   │   ├── ot_sdio_transport.c      ❌ TODO — SDIO send/recv via ETH_IF
│   │   │   └── ot_sdio_transport.h      ❌ TODO
│   │   ├── usb_device/
│   │   │   ├── tusb_config.h            ✅ Done (includes CDC config)
│   │   │   ├── usb_descriptors.h        ✅ Done (CDC interfaces + endpoints)
│   │   │   └── usb_descriptors.c        ✅ Done (CDC descriptor in composite)
│   │   ├── app_usb.c                    ✅ Done (calls app_ot_bridge_init)
│   │   ├── CMakeLists.txt               ✅ Done (includes ot_bridge dir)
│   │   └── Kconfig.projbuild            ✅ Done (sources ot_bridge Kconfig)
│   └── changelog_20260218.md            ✅ This file
│
├── slave/                      ← Original esp-hosted C6 slave (WiFi/BT)
│   └── (untouched — keep as reference / OTA restore target)
│
└── slave_ot/                   ❌ TODO — Fork of slave/ for OT mode
    ├── main/
    │   ├── app_main.c                   ❌ TODO — Add ESP_ETH_IF → Spinel
    │   ├── slave_control.c              (keep OTA handlers as-is)
    │   ├── ot_radio.c                   ❌ TODO — OT radio + Spinel framing
    │   └── ot_radio.h                   ❌ TODO
    ├── sdkconfig.defaults.esp32c6       ❌ TODO — OT enabled, WiFi disabled
    └── partitions.esp32c6.csv           (keep OTA partitions)
```

#### Key Technical Details for Implementation

**esp-hosted packet format** (for `ESP_ETH_IF` frames):
```c
struct esp_payload_header {
    uint8_t  if_type;       // ESP_ETH_IF = 7
    uint8_t  if_num;        // 0
    uint8_t  flags;
    uint8_t  packet_type;   // DATA_PACKET = 2
    uint16_t len;           // Spinel frame length
    uint16_t offset;        // Payload offset
    uint16_t checksum;      // Optional
    uint16_t seq_num;
    uint8_t  throttle_cmd;
    // ... followed by raw Spinel HDLC frame
};
```

**Spinel over HDLC:** OpenThread RCP uses Spinel protocol encoded with HDLC-lite
framing (flag bytes `0x7E`, byte-stuffing). The raw HDLC stream is what flows
through the `ESP_ETH_IF` channel — no additional framing needed.

**C6 SDIO slave pins (fixed, cannot change):**
CLK=19, CMD=18, D0=20, D1=21, D2=22, D3=23

**C6 free GPIOs (available for 802.15.4 radio — auto-assigned internally):**
0–17 are free. The 802.15.4 radio is internal to the C6, no external pins needed.

**OTA partition layout on C6:**
```
otadata,  data, ota,     0xd000,   0x2000,
ota_0,    app,  ota_0,   0x10000,  0x180000,   (1536KB)
ota_1,    app,  ota_1,   0x190000, 0x180000,   (1536KB)
```

#### Dependencies

- ESP-IDF v5.5+ (current: v5.5.2)
- esp-hosted v0.0.27+ (current slave firmware version)
- TinyUSB (via `leeebo/tinyusb_src` managed component)
- ESP-IDF OpenThread component (`components/openthread/`)

#### Testing Plan

1. Build and flash `slave_ot` to C6 (via CH340 serial first time, then OTA)
2. Build and flash `usb_extend_screen` to P4 (with SDIO transport enabled)
3. Plug USB into Pi/Ubuntu host
4. Verify `/dev/ttyACM0` appears
5. Run: `sudo ot-daemon -I wpan0 'spinel+hdlc+forkpty:///dev/ttyACM0'`
6. Run: `sudo ot-ctl state` → should show "disabled" (OT radio ready)
7. Test OTA: flash original `slave/` WiFi firmware back via OTA over SDIO
8. Verify C6 returns to normal esp-hosted WiFi mode after OTA + reboot
