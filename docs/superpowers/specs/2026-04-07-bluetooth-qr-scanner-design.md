# Bluetooth QR Scanner Support (HID Mode)

## Summary

Add Bluetooth discovery and pairing for HID-mode QR/barcode scanners to the existing scanner picker modal. BT scanners appear alongside USB devices in a unified list. Once paired, they work identically to USB scanners — no new monitor class or protocol handling needed.

## Motivation

HelixScreen already supports USB HID QR scanners and has a mature Bluetooth stack for label printers. Most consumer BT barcode scanners (Tera, Netum, Symcode, Inateck) present as HID keyboards. The only missing piece is a UI flow to discover and pair them.

## Design

### Approach: Extend the Scanner Picker Modal

The existing scanner picker modal (`scanner_picker_modal.xml` / `ui_modal_scanner_picker.cpp`) gets a "Scan for Bluetooth" button. BT HID devices merge into the same device list as USB devices, distinguished by a Bluetooth icon badge.

No new classes. We extend existing pieces and reuse the BluetoothLoader infrastructure from label printer support.

### Components Modified

#### 1. Scanner Picker Modal (`ui_modal_scanner_picker.cpp`)

- Add "Scan for Bluetooth" button next to "Refresh" (hidden when `BluetoothLoader::is_available()` is false)
- Add spinner (reuse `bt_scanning` pattern from label printer settings)
- On BT scan: run discovery on background thread, merge results into device list
- BT devices rendered with Bluetooth icon badge and connection state label: "(Paired)", "(Connected)", or no suffix for unpaired
- Tapping an unpaired BT device → `modal_show_confirmation()` ("Pair with [name]?") → pair + trust via `BluetoothLoader` → on success, auto-select as active scanner
- Tapping an already-paired BT device → select it directly (same as USB)

#### 2. Scanner Picker Modal XML (`scanner_picker_modal.xml`)

- Add "Scan for Bluetooth" button with `icon="bluetooth"` and `callback="on_scanner_bt_scan"`
- Add spinner bound to a scanning state subject
- Device list items: add optional Bluetooth icon badge (conditionally shown based on device bus type)

#### 3. BT Discovery Filter

Reuse `BluetoothLoader::discover()` with scanner-specific filtering:

- **Primary filter**: HID UUID `0x1124` (classic Bluetooth HID) and `0x1812` (HID-over-GATT)
- **Name fallback**: devices with "barcode", "scanner", "symcode", "tera", "netum", "inateck" in the name (case-insensitive)
- **Previously paired**: include devices with a saved `scanner/bt_address` even if not currently discoverable

This parallels the label printer discovery which filters by SPP/Phomemo UUIDs + known printer brand names.

#### 4. BT Pairing

Identical to label printer flow:
- `BluetoothLoader::pair(bt_ctx, mac)` on background thread
- Set device as trusted (auto-reconnect on power-on)
- On success: toast "Paired", auto-select device, persist to settings
- On failure: toast "Pairing failed"

#### 5. Settings Persistence

Existing keys continue to work for device selection:
- `scanner/usb_vendor_product` — vendor:product hex string (works for both USB and BT HID)
- `scanner/usb_device_name` — display name

New key:
- `scanner/bt_address` — MAC address of paired BT scanner (for reconnection / discovery filtering)

The `usb_vendor_product` key name is a misnomer for BT devices but works correctly since `find_hid_keyboard_devices()` matches by vendor:product regardless of bus type. Renaming the key would be a breaking change for no functional benefit.

### What Doesn't Change

- **`UsbScannerMonitor`** — Already works with BT HID devices. `find_hid_keyboard_devices()` accepts `BUS_BLUETOOTH (0x05)`. Once a BT scanner is paired+connected and appears as `/dev/input/eventN`, the monitor reads it identically to USB.
- **`QrScannerOverlay`** — No changes. Starts/stops the monitor as before.
- **`QrDecoder` / Spoolman pattern matching** — Transport-agnostic.
- **`find_hid_keyboard_devices()`** — Already handles BT. No changes needed.
- **`enumerate_usb_hid_devices()`** — Already enumerates BT HID devices. No changes needed.
- **Label printer BT code** — Untouched. We reuse `BluetoothLoader` but don't modify printer discovery/pairing.

### Data Flow

```
User opens scanner picker modal
  → USB devices listed via enumerate_usb_hid_devices() (includes any connected BT HID)
  → User taps "Scan for Bluetooth"
  → BluetoothLoader.discover() on bg thread (HID UUID + name filter, ~15s)
  → Results merged into device list with BT badge
  → User taps unpaired BT device
  → modal_show_confirmation("Pair with [name]?")
  → User confirms
  → BluetoothLoader.pair() + trust on bg thread
  → On success: auto-select device, persist MAC + vendor:product to settings
  → Toast "Paired — scanner will connect automatically"
  → BT scanner appears as /dev/input/eventN
  → UsbScannerMonitor reads it via existing evdev path
```

### Edge Cases

| Scenario | Behavior |
|----------|----------|
| BT hardware not available | "Scan for Bluetooth" button hidden |
| Scanner paired but not connected | Toast: "Paired — scanner will connect automatically when powered on" |
| Scanner disconnects mid-use | `poll()` in UsbScannerMonitor returns error on fd, monitor logs warning. No crash. Resumes on reconnect. |
| Scanner already paired (e.g., via bluetoothctl) | Appears in device list with "(Paired)" badge, selectable without re-pairing |
| Discovery finds no HID devices | List unchanged (only USB devices shown). No error — BT section just has no results. |
| User pairs scanner then unpairs externally | Next scan won't show it as paired. Saved `bt_address` becomes stale but harmless — device just won't appear in list. |

### UI String Changes

Rename "USB" references in scanner-related UI to be transport-agnostic where appropriate:
- "No USB HID devices detected" → "No devices detected"
- "Plug in your scanner and tap Refresh" → "Plug in a USB scanner and tap Refresh, or scan for Bluetooth devices"

### Testing

- Unit tests for discovery filter logic (HID UUID matching, name fallback)
- Existing `UsbScannerMonitor` tests cover the evdev path for both USB and BT devices
- Manual testing with a BT barcode scanner (HID mode) on a device with BlueZ
