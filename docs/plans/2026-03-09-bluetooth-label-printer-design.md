# Bluetooth Label Printer Support — Design Document

**Date**: 2026-03-09
**Status**: Approved
**Scope**: MAJOR — new plugin architecture, protocol refactor, UI changes

## Overview

Add Bluetooth as a third connection option for label printers (alongside USB and Network).
Supports three printer families over two Bluetooth transports:

- **Brother QL** — BT Classic SPP (RFCOMM), same ESC/P raster protocol as network
- **Phomemo** — BT Classic SPP (RFCOMM) or BLE GATT, ESC/POS-style raster protocol
- **Niimbot** — BLE GATT only, custom packet protocol with XOR checksum framing

Built as a runtime-loadable shared library plugin — zero memory footprint on devices
without Bluetooth hardware.

## Architecture

### Plugin Model

```
Main Binary                          libhelix-bluetooth.so
┌─────────────────────┐              ┌──────────────────────┐
│ BluetoothLoader     │──dlopen()──▶ │ bt_plugin.cpp        │
│   check HCI hw      │              │   sd-bus event loop   │
│   dlsym() ABI fns   │              │   BlueZ D-Bus calls   │
│                     │              │                      │
│ BrotherQLBTPrinter  │──rfcomm fd──▶│ bt_rfcomm.cpp        │
│ PhomemoBTPrinter    │──rfcomm/ble─▶│ bt_ble.cpp           │
│ NiimbotBTPrinter    │──ble handle─▶│                      │
│                     │              │ bt_discovery.cpp     │
│ bluetooth_plugin.h  │◀─shared ABI─▶│ bluetooth_plugin.h   │
└─────────────────────┘              └──────────────────────┘
```

The main binary never links libsystemd/libbluetooth for this feature.
All BlueZ/D-Bus interaction lives inside the plugin `.so`.

### Plugin C ABI (`bluetooth_plugin.h`)

```c
#define HELIX_BT_API_VERSION 1

struct helix_bt_plugin_info {
    int api_version;
    const char* name;
    bool has_classic;       // SPP/RFCOMM support
    bool has_ble;           // BLE GATT support
};

struct helix_bt_device {
    const char* mac;        // "AA:BB:CC:DD:EE:FF"
    const char* name;       // "Brother QL-820NWB"
    bool paired;
    bool is_ble;            // false = Classic, true = BLE
    const char* service_uuid; // SPP or Phomemo service UUID
};

typedef void (*helix_bt_discover_cb)(const helix_bt_device* dev, void* user_data);

// Required exports:
helix_bt_plugin_info* helix_bt_get_info();
helix_bt_context*     helix_bt_init();
void                  helix_bt_deinit(helix_bt_context*);

// Discovery
int  helix_bt_discover(helix_bt_context*, int timeout_ms, helix_bt_discover_cb, void*);
void helix_bt_stop_discovery(helix_bt_context*);

// Pairing
int  helix_bt_pair(helix_bt_context*, const char* mac);
int  helix_bt_is_paired(helix_bt_context*, const char* mac);

// SPP/RFCOMM — returns fd for direct read/write
int  helix_bt_connect_rfcomm(helix_bt_context*, const char* mac, int channel);

// BLE GATT — returns opaque handle
int  helix_bt_connect_ble(helix_bt_context*, const char* mac, const char* write_uuid);
int  helix_bt_ble_write(helix_bt_context*, int handle, const uint8_t* data, int len);

// Shared
void helix_bt_disconnect(helix_bt_context*, int handle);
const char* helix_bt_last_error(helix_bt_context*);
```

All functions return 0 on success, negative errno-style on failure.

### Loading Logic (`BluetoothLoader`)

1. Check `/sys/class/bluetooth/hci0` exists — no HW means skip entirely
2. `dlopen("libhelix-bluetooth.so")` from binary's directory
3. `dlsym()` all required function pointers into a table
4. Call `helix_bt_get_info()` → verify `api_version == HELIX_BT_API_VERSION`
5. Store in `BluetoothLoader` singleton
6. If any step fails → BT option hidden in UI, zero impact

## Protocol Extraction Refactor

Extract protocol byte generation from existing printer classes into pure functions
with no I/O dependency. All transports (USB, TCP, BT) share the same protocol code.

### `brother_ql_protocol.h`

```cpp
namespace helix::label {
std::vector<uint8_t> brother_ql_build_raster(
    const LabelBitmap& bitmap, const LabelSize& size);
}
```

Extracts from `BrotherQLPrinter::print_label()`:
- Invalidation (200 bytes of 0x00)
- Initialize (ESC @)
- Raster mode, media info, auto-cut settings
- Per-row raster encoding with horizontal flip
- Print command (0x1A)

### `phomemo_protocol.h`

```cpp
namespace helix::label {
std::vector<uint8_t> phomemo_build_raster(
    const LabelBitmap& bitmap, const LabelSize& size);
}
```

Extracts from `PhomemoPrinter::print()`:
- Speed, density, media type commands
- GS v 0 raster block
- Finalize and feed-to-gap commands

### After Extraction

| Class | Before | After |
|-------|--------|-------|
| `BrotherQLPrinter` | Builds raster + writes TCP | Calls `brother_ql_build_raster()` + writes TCP |
| `PhomemoPrinter` | Builds raster + writes USB | Calls `phomemo_build_raster()` + writes USB |
| `BrotherQLBluetoothPrinter` | — | Calls `brother_ql_build_raster()` + writes RFCOMM fd |
| `PhomemoBluetoothPrinter` | — | Calls `phomemo_build_raster()` + writes RFCOMM/BLE |
| `NiimbotBluetoothPrinter` | — | Calls `niimbot_build_print_job()` + writes BLE GATT |

## Bluetooth Printer Backends

### `BrotherQLBluetoothPrinter` (implements `ILabelPrinter`)

- Uses `helix_bt_connect_rfcomm(mac, channel)` → gets fd
- Writes `brother_ql_build_raster()` output to fd (same as TCP)
- Async on detached thread, callback via `queue_update()`
- SPP channel typically 1 (default)

### `PhomemoBluetoothPrinter` (implements `ILabelPrinter`)

- Uses `helix_bt_connect_ble(mac, "0000ff02-0000-1000-8000-00805f9b34fb")`
- Writes `phomemo_build_raster()` output via `helix_bt_ble_write()`
- BLE write handles chunking to MTU internally in plugin
- Async on detached thread, callback via `queue_update()`

### `NiimbotBluetoothPrinter` (implements `ILabelPrinter`)

- Uses `helix_bt_connect_ble(mac, "e7810a71-73ae-499d-8c15-faa9aef0c3f2")` (Transparent UART)
- `niimbot_build_print_job()` generates complete packet sequence from bitmap
- Packet format: `[0x55 0x55 CMD LEN DATA... XOR_CHECKSUM 0xAA 0xAA]`
- Sends packets sequentially: 10ms delay for image rows, 100ms for commands
- Supports B21 (384px/48mm printhead) and D11/D110 (96px/12mm printhead)
- Model auto-detected from BLE device name for correct printhead width
- Row compression: blank rows → `PrintEmptyRow`, identical rows → repeat count
- Async on detached thread, callback via `queue_update()`

## Discovery & Pairing

### Discovery Flow

1. UI triggers scan → `BluetoothLoader::discover(timeout_ms, callback)`
2. Plugin: `org.bluez.Adapter1.StartDiscovery()` → watches `InterfacesAdded` D-Bus signals
3. Filters to devices advertising SPP UUID (`00001101-...`) or Phomemo BLE service UUIDs
4. Callback per device with MAC, name, paired status, transport type
5. `label_printer_score()` (existing) scores by name heuristics
6. Results marshalled to UI thread via `queue_update()`

### Pairing

- Most label printers use "Just Works" (no PIN) — `Device1.Pair()` succeeds immediately
- When user selects an unpaired device: `modal_show_confirmation()` "Pair with {name}?"
- On confirm → plugin calls `org.bluez.Device1.Pair()`, spinner, success/fail toast
- PIN entry modal deferred until we encounter a printer that needs it
- BlueZ remembers pairings system-wide — no HelixScreen persistence needed

## UI Changes

### Settings Overlay

- Printer type selector: **Network / USB / Bluetooth** (3-way)
- Bluetooth option only visible when `BluetoothLoader::is_available()` returns true
- New Bluetooth section (shown when BT type selected):
  - "Scan" button → populates dropdown with discovered BT printers
  - Dropdown format: `"Brother QL-820NWB (AA:BB:CC:DD:EE:FF)"`
  - Spinner during scan
- Label size + preset dropdowns unchanged (shared across all transport types)

### Pairing Modal

- `modal_show_confirmation()` with title "Pair Bluetooth Printer"
- Severity: info
- On confirm: pair, show result toast
- On cancel: deselect device in dropdown

### Config Persistence (`settings.json`)

```json
"label_printer": {
    "type": "bluetooth",
    "bt_mac": "AA:BB:CC:DD:EE:FF",
    "bt_transport": "spp",
    ...existing fields unchanged...
}
```

`bt_transport` is auto-detected from the device's advertised services during discovery
("spp" for Classic, "ble" for BLE GATT). User doesn't choose.

## Build System

### File Layout

```
src/bluetooth/                      # Plugin sources (separate compilation)
├── bt_plugin.cpp                   # C ABI exports, context, sd-bus event loop
├── bt_discovery.cpp                # BlueZ adapter/device discovery
├── bt_rfcomm.cpp                   # RFCOMM socket connect
├── bt_ble.cpp                      # BLE GATT connect/write via D-Bus
└── bt_error.cpp                    # Error string management

include/bluetooth_plugin.h          # C ABI header (shared)
include/bluetooth_loader.h          # dlopen wrapper singleton
include/brother_ql_protocol.h       # Extracted protocol (shared)
include/phomemo_protocol.h          # Extracted protocol (shared)
include/niimbot_protocol.h          # Niimbot BLE protocol (packet builder + print job)
include/niimbot_bt_printer.h        # Niimbot BLE printer backend
include/bt_discovery_utils.h        # Brand detection table (Brother/Niimbot/Phomemo)
include/bt_print_utils.h            # Shared RFCOMM send helper
```

### Makefile

```makefile
bluetooth-plugin: build/lib/libhelix-bluetooth.so

BT_CXXFLAGS := $(shell pkg-config --cflags libsystemd) -fPIC
BT_LDFLAGS  := $(shell pkg-config --libs libsystemd) -lbluetooth -shared -fPIC
```

Main binary: **no new link dependencies**. `-ldl` already linked.

### Cross-Compilation

Docker container needs `libbluetooth-dev:arm64` and `libsystemd-dev:arm64`
only when building the bluetooth plugin target.

### Deployment

- Pi / BTT Pi images: include `libhelix-bluetooth.so` next to binary
- AD5M images: omit entirely (no BT hardware)

## Thread Model

### Plugin Thread

- Single background thread owned by plugin, started on `helix_bt_init()`
- Runs `sd_bus_process()` / `sd_bus_wait()` event loop
- Handles D-Bus signals (discovery), method calls (pair, GATT write)
- Stopped on `helix_bt_deinit()`

### Caller Responsibility

- Plugin knows nothing about LVGL or `queue_update()`
- Callers (`BrotherQLBluetoothPrinter`, `PhomemoBluetoothPrinter`) run on detached
  threads (same as existing USB/TCP printers) and marshal results to UI via `queue_update()`

## Error Handling

- All plugin C ABI functions return `int` (0 = success, negative = error)
- `helix_bt_last_error()` returns human-readable string
- Connect timeout: 10s
- Discovery timeout: configurable (default 15s)
- BLE write timeout: 5s per chunk
- Errors surfaced as toasts in UI (same pattern as existing printer errors)

## Testing

### Protocol Tests (unit, no hardware)

- Given known `LabelBitmap` + `LabelSize`, assert `brother_ql_build_raster()` output matches expected bytes
- Same for `phomemo_build_raster()`
- Niimbot: `test_niimbot_protocol.cpp` — packet framing, checksum, print job sequence, blank row compression, label sizes (9 tests, 59 assertions)
- Validates the extraction didn't change protocol behavior

### Plugin ABI Tests (unit, mock sd-bus)

- Verify `helix_bt_get_info()` returns correct version
- Verify loader rejects mismatched API versions
- Verify loader handles missing `.so` gracefully

### Integration Tests (manual, requires BT hardware)

- Discover Brother QL-820NWB via BT Classic
- Discover Phomemo M110 via BLE
- Pair, print test label, verify output

## Zero-Footprint Guarantee

When Bluetooth is unavailable (no HCI adapter or no `.so` file):

- No `dlopen()` attempt (HCI check first)
- No threads started
- No D-Bus connections
- No memory allocated
- UI shows 2-way selector (Network / USB) — BT option never rendered
- Settings JSON `type` field rejects `"bluetooth"` value, falls back to `"network"`
