# QR Spool Scanner Design

## Context

Users with Spoolman-managed filament want to scan QR codes on spool labels to assign spools to filament slots. Today this requires either manual lookup in the Spoolman panel or an external tool like afc-spool-scan (USB-scanner-only, AFC-only). HelixScreen already has camera support, Spoolman integration, and multi-backend filament management — this feature connects them.

**Goal:** Scan a Spoolman QR code (camera or USB scanner) → look up spool → assign to the slot the user is already editing. Works with AFC, Happy Hare, ValgACE, Tool Changer, or no changer at all.

## QR Format

Spoolman QR codes encode spool IDs in two formats:
- `web+spoolman:s-<id>` (URL-like, for web/app integration)
- `SM:SPOOL=<id>` (simple text, for barcode scanners)

Both just contain a spool ID pointer — full metadata is fetched via `MoonrakerSpoolmanAPI::get_spool(id)`.

## Architecture

Two input sources feed a unified result:

```
Camera Path                    USB Scanner Path
─────────────                  ──────────────────
CameraStream (existing)        evdev /dev/input/eventN
    ↓                              ↓
JPEG frame → grayscale         UsbScannerMonitor
    ↓                          (keystroke accumulator)
quirc decode                       ↓
    ↓                          Pattern match prefix
Parse QR text                      ↓
    ↓                          Extract spool_id
    └──────────┬───────────────────┘
               ↓
        SpoolScanResult { spool_id }
               ↓
        MoonrakerSpoolmanAPI::get_spool(id)
               ↓
        SpoolInfo { vendor, material, color, weight, temps }
               ↓
        Confirm + assign to slot (slot already known from entry point)
```

### Key Design Decisions

- **quirc** library (MIT, ~3K LOC C, QR-only) vendored in `lib/quirc/`. No dependencies. Takes grayscale pixels, returns decoded QR strings.
- **Both input paths produce `SpoolScanResult`** — the UI doesn't care which source detected the QR code.
- **Slot is always known** before scanning starts — no slot picker needed.
- **Reuses existing infrastructure:** CameraStream, MoonrakerSpoolmanAPI, AmsBackend::set_slot_info(), SpoolInfo types.

## Entry Points

All entry points know which slot they're targeting:

1. **AMS slot context menu** — new `SCAN_QR` MenuAction in `AmsContextMenu`. Opens scanner overlay targeting that slot.

2. **AMS/slot edit modal** — dropdown split button on the spool assignment field. Default action: "Assign Spool" (existing picker). Secondary: "Scan QR Code". Opens scanner overlay; on success, populates the modal with the looked-up spool and returns to the edit modal for confirmation/save.

3. **Single-extruder spool card** — same edit modal pattern. The spool card on the filament panel opens the edit modal, which has the dropdown button. On no-changer printers, assignment calls `POST /server/spoolman/spool_id` to set the active spool.

## New Components

### 1. `QrDecoder` — QR code detection from image frames

**File:** `include/qr_decoder.h`, `src/system/qr_decoder.cpp`

Wraps quirc. Stateless utility:
```cpp
namespace helix {
struct QrDecodeResult {
    bool success;
    std::string text;        // Raw QR text
    int spool_id;            // Parsed Spoolman spool ID, or -1
};

class QrDecoder {
public:
    QrDecoder();
    ~QrDecoder();

    // Decode QR from grayscale image buffer. Thread-safe.
    QrDecodeResult decode(const uint8_t* gray_data, int width, int height);

    // Parse Spoolman spool ID from QR text.
    // Handles "web+spoolman:s-<id>", "SM:SPOOL=<id>", URL patterns.
    static int parse_spoolman_id(const std::string& text);
};
} // namespace helix
```

### 2. `UsbScannerMonitor` — USB barcode scanner input via evdev

**File:** `include/usb_scanner_monitor.h`, `src/system/usb_scanner_monitor.cpp`

Background thread monitoring `/dev/input/by-id/` for USB HID scanner devices. Accumulates keystrokes, detects Spoolman QR patterns, fires callback.

```cpp
namespace helix {
class UsbScannerMonitor {
public:
    using ScanCallback = std::function<void(int spool_id)>;

    void start(ScanCallback on_scan);
    void stop();
    bool is_running() const;

private:
    // Background thread reads evdev events
    // Accumulates HID keycodes → ASCII string
    // On Enter/newline: check for Spoolman prefix, parse ID, fire callback
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
};
} // namespace helix
```

**Device detection:** Scan `/dev/input/by-id/` for devices with "barcode" or "scanner" in name, or enumerate all keyboard-type HID devices. Grab the device exclusively (`EVIOCGRAB`) so keystrokes don't leak into LVGL's keyboard input.

**Always-on vs on-demand:** Start monitoring when scanner overlay is open. If a USB scanner event arrives while no overlay is open, show a toast notification with the spool info and offer to assign it (stretch goal — initially just active during overlay).

### 3. `QrScannerOverlay` — Fullscreen camera viewfinder overlay

**File:** `include/ui_qr_scanner_overlay.h`, `src/ui/ui_qr_scanner_overlay.cpp`
**XML:** `ui_xml/qr_scanner_overlay.xml`

Fullscreen overlay with camera feed, targeting rectangle, and status text.

```cpp
namespace helix::ui {
class QrScannerOverlay {
public:
    using ResultCallback = std::function<void(const SpoolInfo& spool)>;
    using CancelCallback = std::function<void()>;

    // Show overlay targeting a specific slot
    static void show(int slot_index, ResultCallback on_result, CancelCallback on_cancel);

    // Show for active spool (no slot, single-extruder)
    static void show_for_active_spool(ResultCallback on_result, CancelCallback on_cancel);

private:
    // Camera feed → QrDecoder on background thread
    // On decode success: lookup spool via MoonrakerSpoolmanAPI
    // On lookup success: fire ResultCallback with SpoolInfo
    // USB scanner monitor also active simultaneously
};
} // namespace helix::ui
```

**Camera scanning flow:**
1. Start `CameraStream` with existing webcam URL
2. On each frame callback (background thread): convert to grayscale, run `QrDecoder::decode()`
3. Throttle to ~5 fps for QR detection (skip frames if decoder is busy)
4. On successful decode: call `QrDecoder::parse_spoolman_id()`
5. If valid: stop camera, call `MoonrakerSpoolmanAPI::get_spool()`, fire callback via `ui_queue_update()`
6. Play success sound via `SoundManager`, brief green flash on targeting rectangle

**USB scanner simultaneous:** `UsbScannerMonitor` runs alongside camera. Whichever source detects first wins.

**No camera available:** If no webcam configured, overlay shows "No camera available — use USB barcode scanner" with the USB scanner still listening. Skip the camera viewfinder entirely.

## Integration with Existing Components

### AmsContextMenu changes
- Add `SCAN_QR` to `MenuAction` enum
- Add menu item with QR icon (MDI `qrcode-scan`)
- Only show when Spoolman is enabled (`SettingsManager::is_spoolman_enabled()`)
- On action: launch `QrScannerOverlay::show(slot_index, ...)`, on result call `AmsBackend::set_slot_info()`

### AmsEditModal changes
- Replace single "Assign Spool" / "Change Spool" button with dropdown split button
- Primary action: existing picker (unchanged)
- Secondary action: "Scan QR Code" → launch overlay
- On scan result: populate modal fields from `SpoolInfo`, switch to form view showing the spool
- Only show dropdown when Spoolman is enabled

### Backend assignment (existing, no changes needed)
- `AmsBackend::set_slot_info()` already handles all backends:
  - AFC: `SET_SPOOL_ID LANE={name} SPOOL_ID={id}`
  - Happy Hare: `MMU_GATE_MAP GATE={idx} SPOOLID={id}`
  - ValgACE/ToolChanger: internal slot update
  - No changer: `POST /server/spoolman/spool_id` for active spool

## New Dependencies

- **quirc** — vendored in `lib/quirc/`, compiled as part of build. MIT license. ~3K LOC C.
- **linux/input.h** — for evdev (already available on all Linux targets). Only used on Linux builds.

## Build Integration

- Add `lib/quirc/` sources to Makefile
- Gate USB scanner behind `#ifdef __linux__` (evdev is Linux-only)
- Gate camera scanning behind `HELIX_HAS_CAMERA`
- quirc itself is platform-independent (works on all targets)

## Error Handling

| Scenario | Behavior |
|----------|----------|
| QR not a Spoolman code | Show toast: "Not a Spoolman QR code" + keep scanning |
| Spool ID not found in Spoolman | Show toast: "Spool #X not found" + keep scanning |
| No camera + no USB scanner | Show message in overlay, cancel button available |
| Camera fails to start | Fall back to USB-scanner-only mode with message |
| Spoolman not configured | Hide QR scan options entirely from UI |

## Verification Plan

1. **Unit tests for QrDecoder:**
   - Parse `web+spoolman:s-42` → spool_id=42
   - Parse `SM:SPOOL=123` → spool_id=123
   - Parse URL `https://spoolman.local/view/spool/7` → spool_id=7
   - Reject non-Spoolman QR text → spool_id=-1
   - Decode a known QR code image (generate test fixture with LVGL's lv_qrcode)

2. **Unit tests for UsbScannerMonitor:**
   - Keystroke accumulation → string assembly
   - Pattern detection on Enter key
   - Device enumeration (mock /dev/input/)

3. **Integration test:**
   - Mock Spoolman API returning SpoolInfo for a known spool ID
   - Verify the full flow: scan result → API lookup → SpoolInfo populated

4. **Manual test on Pi:**
   - Camera viewfinder renders correctly
   - QR code on spool label detected within ~2 seconds
   - Spool info populates edit modal
   - Assignment persists to backend (check via Moonraker API)
   - USB barcode scanner detected and functional
