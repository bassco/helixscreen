# Manual Barcode Scanner Selection

**Issue:** prestonbrown/helixscreen#659
**Date:** 2026-03-31

## Problem

USB barcode scanners that don't contain "barcode" or "scanner" in their device name (e.g., "TMS HIDKeyBoard") aren't prioritized by the auto-detection logic. They fall back to the generic USB HID keyboard pool, which means they may lose to a real USB keyboard if one is connected.

## Design

### UI: Device Picker in Spoolman Settings

Add a "Barcode Scanner" action row inside the existing Spoolman settings overlay. Current status shown as secondary text:
- "Auto-detect" (default, no manual selection)
- Device name (e.g., "TMS HIDKeyBoard") when manually configured

Tapping the row opens a modal listing all currently connected USB HID keyboard-capable devices. Each entry shows the device name and vendor:product ID. The list includes:
1. **"Auto-detect (default)"** at the top — clears any manual selection
2. Each discovered USB HID keyboard device — tapping selects it

A refresh button allows re-scanning without closing the modal. If no devices are found, show "No USB HID devices detected. Plug in your scanner and tap Refresh."

### Detection Logic Change

In `input_device_scanner.cpp` `find_hid_keyboard_devices()`:

1. Read configured vendor:product ID from settings
2. If set, scan sysfs for a device matching that vendor:product — if found, return it as the sole result (highest priority, unconditional)
3. If not set, or if the configured device is not currently connected, fall back to the existing name-based priority logic (named scanners first, then generic keyboards)

This means a manually configured scanner always wins, even over a device with "barcode" in its name. The fallback preserves full backward compatibility.

### Storage

Two config keys under `config->df()`:

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `scanner/usb_vendor_product` | string | `""` | `"vendor:product"` hex pair, empty = auto-detect |
| `scanner/usb_device_name` | string | `""` | Display name for settings UI (shown even when device unplugged) |

### Sysfs Data Source

Vendor and product IDs are read from:
- `/sys/class/input/eventN/device/id/vendor` — hex string (e.g., "1a2c")
- `/sys/class/input/eventN/device/id/product` — hex string (e.g., "4c5e")

These are already adjacent to the existing `id/bustype` reads in `input_device_scanner.cpp`.

### Device Enumeration API

A new function exposed from `input_device_scanner.h`:

```cpp
struct UsbHidDevice {
    std::string name;         // e.g., "TMS HIDKeyBoard"
    std::string vendor_id;    // e.g., "1a2c"
    std::string product_id;   // e.g., "4c5e"
    std::string event_path;   // e.g., "/dev/input/event5"
};

std::vector<UsbHidDevice> enumerate_usb_hid_devices();
```

This reuses the existing sysfs scanning and capability filtering from `find_hid_keyboard_devices()`, but returns all matching devices with their IDs instead of prioritizing one.

### Component Breakdown

1. **`input_device_scanner.cpp`** — Add `enumerate_usb_hid_devices()`, add vendor:product reading to sysfs scan, add config-based device matching to `find_hid_keyboard_devices()`
2. **`settings_manager.h/.cpp`** — Add `get_scanner_device_id()`, `set_scanner_device_id()`, `get_scanner_device_name()`, `set_scanner_device_name()`
3. **Spoolman overlay (`ui_settings_spoolman.cpp`)** — Add "Barcode Scanner" action row, wire callback to open picker modal
4. **Scanner picker modal** — New modal showing device list, handles selection/clear/refresh
5. **Spoolman overlay XML** — Add the action row element
6. **Scanner picker XML** — Modal layout with scrollable device list

### Edge Cases

- **Configured device not plugged in**: Falls back to auto-detect silently. Settings UI shows the configured name with "(not connected)" or similar.
- **Multiple identical scanners**: Both show in the list with the same vendor:product. Selecting either configures the same ID — both would match. This is acceptable; distinguishing truly identical devices would require serial numbers or physical port paths, which is overkill.
- **Device unplugged during scan**: Existing error recovery in `usb_scanner_monitor.cpp` handles this — it re-scans on read errors.
