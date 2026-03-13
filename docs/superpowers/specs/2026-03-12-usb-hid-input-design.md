# USB HID Input Support for DRM and Fbdev Backends

**Date:** 2026-03-12
**Status:** Draft

## Problem

USB HID mice and keyboards do not work on Pi (DRM) or embedded (fbdev) builds:

1. **DRM pointer:** `create_input_pointer()` finds the DSI touchscreen via libinput and returns immediately (line 362). Mouse detection at line 374 is unreachable when a touchscreen exists.
2. **Fbdev pointer:** `auto_detect_touch_device()` only checks `ABS_X/ABS_Y` (touchscreen). Mice use `REL_X/REL_Y` + `BTN_LEFT` — never detected.
3. **DRM keyboard:** Only tries `lv_libinput_find_dev(KEYBOARD)` with no evdev fallback.
4. **Fbdev keyboard:** Has sysfs scanning for `KEY_A` — should work but untested with USB HID.

## Requirements

- USB HID mouse works alongside DSI touchscreen (both active simultaneously)
- USB HID keyboard works on both DRM and fbdev backends
- Existing touchscreen behavior unchanged (calibration, gestures, rotation)
- Mouse cursor visible when mouse is active
- No new dependencies

## Design

### Key Insight: LVGL Supports Multiple Input Devices

Each `lv_evdev_create()` / `lv_libinput_create()` call registers an independent `lv_indev_t` that LVGL polls separately. We don't need to multiplex — just create all detected devices.

### New Shared Utility: `input_device_scanner.h` + `input_device_scanner.cpp`

Extract sysfs-based input device scanning into a shared utility to avoid duplicating the same logic in both backends. Free functions in a `helix::input` namespace.

```cpp
namespace helix::input {

struct ScannedDevice {
    std::string path;       // e.g., "/dev/input/event4"
    std::string name;       // e.g., "Logitech USB Mouse"
    int event_num;
};

// Check if a specific bit is set in a sysfs capability hex bitmask.
// Handles both 32-bit and 64-bit word widths (kernel prints unsigned long).
// The bitmask is space-separated hex words, rightmost = lowest bits.
bool check_capability_bit(const std::string& hex_bitmask, int bit);

// Scan /dev/input/event* for devices with REL_X + REL_Y + BTN_LEFT
// (standard mouse capabilities). Skips devices with ABS_X/ABS_Y (touchscreens).
// Returns first match, or nullopt.
std::optional<ScannedDevice> find_mouse_device();

// Scan /dev/input/event* for devices with KEY_A set in key capabilities.
// Returns first match, or nullopt.
std::optional<ScannedDevice> find_keyboard_device();

}  // namespace helix::input
```

#### Capability Bit Parsing (`check_capability_bit`)

The kernel prints sysfs capability bitmasks as space-separated hex words where each word represents an `unsigned long`. Word width is architecture-dependent:
- **aarch64 (Pi 5):** 64-bit words → 16 hex digits each
- **armhf (AD5M, SonicPad):** 32-bit words → 8 hex digits each

The `check_capability_bit()` function:
1. Split the hex string on spaces
2. Determine which word contains the target bit: `word_index = bit / bits_per_word` (counted from the right)
3. Check bit within that word: `bit_in_word = bit % bits_per_word`
4. **Architecture-independent approach:** Parse right-to-left, tracking cumulative bit offset, so it works regardless of word width

This replaces ad-hoc bit checks like the fbdev keyboard scanner's hardcoded `1UL << KEY_A` on the last word (which only works because KEY_A=30 fits in any word size).

#### Mouse Detection Logic

1. Scan `/dev/input/event*`
2. Check `capabilities/abs` — if `ABS_X` (bit 0) AND `ABS_Y` (bit 1) are set, **skip** (it's a touchscreen, not a mouse)
3. Check `capabilities/rel` — require `REL_X` (bit 0) AND `REL_Y` (bit 1)
4. Check `capabilities/key` — require `BTN_LEFT` (bit 272 = 0x110)
5. Return first accessible match

Touchpads are treated as mice — they report the same capabilities and work as basic pointers. Multi-finger gestures are not supported (non-goal).

#### Keyboard Detection Logic (extracted from fbdev)

1. Scan `/dev/input/event*`
2. Check `capabilities/key` — require `KEY_A` (bit 30)
3. Return first match

#### Combo Devices (e.g., Logitech K400 keyboard+trackpad)

Some USB devices present as a single evdev node with both keyboard and mouse capabilities. The same device path can be opened by both `find_mouse_device()` and `find_keyboard_device()` — `lv_evdev_create()` filters events by type (`LV_INDEV_TYPE_POINTER` vs `LV_INDEV_TYPE_KEYPAD`), so creating two indev devices on the same path is safe. No `exclude_path` parameter needed.

### DRM Backend Changes

**Header** (`display_backend_drm.h`):
- Add `lv_indev_t* mouse_ = nullptr;` member

**Pointer** (`create_input_pointer()`):
- After creating touch device (line 346-362), **remove the early return**
- Check `HELIX_MOUSE_DEVICE` env var override before auto-detection
- Use `helix::input::find_mouse_device()` + `lv_evdev_create()` for sysfs-based detection
- **Do NOT use `lv_libinput_find_dev(LV_LIBINPUT_CAPABILITY_POINTER)`** — libinput's POINTER capability is too broad and falsely matches HDMI CEC devices (e.g., Pi's `vc4-hdmi` devices report `REL_X`/`REL_Y` but are not mice). The sysfs scanner's `BTN_LEFT` (bit 272) check correctly distinguishes real mice.
- Return `pointer_` (the touch device) to DisplayManager as before — it needs it for calibration/scroll config

**Keyboard** (`create_input_keyboard()`):
- After `lv_libinput_find_dev(KEYBOARD)` fails (line 423), add fallback: call `helix::input::find_keyboard_device()` and create via `lv_evdev_create(LV_INDEV_TYPE_KEYPAD, path)`

### Fbdev Backend Changes

**Header** (`display_backend_fbdev.h`):
- Add `lv_indev_t* mouse_ = nullptr;` member

**Pointer** (`create_input_pointer()`):
- At the end of the function (after touch setup, before return), check `HELIX_MOUSE_DEVICE` env var
- If no override, call `helix::input::find_mouse_device()`
- Skip the touch device path to avoid double-opening it as a mouse
- If found, create via `lv_evdev_create(LV_INDEV_TYPE_POINTER, path)` and store in `mouse_`

**Keyboard** (`create_input_keyboard()`):
- Replace inline sysfs scanning with `helix::input::find_keyboard_device()` call
- Keep env var override (`HELIX_KEYBOARD_DEVICE`) as-is
- Existing behavior preserved, code just moves to shared utility

### Mouse Cursor Visibility

LVGL 9.x does **not** show a mouse cursor by default. After creating the mouse `lv_indev_t`, set a cursor image:

```cpp
// Create a simple cursor (small crosshair or dot)
lv_obj_t* cursor = lv_image_create(lv_screen_active());
lv_image_set_src(cursor, LV_SYMBOL_CROSSHAIRS);  // or a custom image
lv_indev_set_cursor(mouse_, cursor);
```

Alternatively, use `LV_SYMBOL_GPS` (a dot) or a custom 16x16 cursor image from assets. The cursor object is owned by LVGL and cleaned up by `lv_deinit()`.

### DisplayManager Changes

None. `m_pointer` continues to track the touchscreen for calibration, scroll config, and sleep-aware wrapper. The mouse `lv_indev_t` is created and registered with LVGL automatically — it gets polled without DisplayManager needing to know about it.

### Cleanup

Both `mouse_` members are cleaned up by `lv_deinit()` (per CLAUDE.md rule: "Just `lv_deinit()` - handles everything"). No manual cleanup in backend destructors.

### Env Var Overrides

New environment variable for explicit mouse device override:
- `HELIX_MOUSE_DEVICE=/dev/input/eventX` — force a specific mouse device path

Keyboard override already exists: `HELIX_KEYBOARD_DEVICE`.

## Files Changed

| File | Change |
|------|--------|
| `include/input_device_scanner.h` | **NEW** — shared sysfs scanner declarations |
| `src/api/input_device_scanner.cpp` | **NEW** — implementation (same dir as backends) |
| `include/display_backend_drm.h` | Add `mouse_` member |
| `src/api/display_backend_drm.cpp` | Remove early return, add mouse + keyboard fallback |
| `include/display_backend_fbdev.h` | Add `mouse_` member |
| `src/api/display_backend_fbdev.cpp` | Add mouse detection, extract keyboard scanner |
| `tests/test_input_device_scanner.cpp` | **NEW** — unit tests for sysfs parsing |

## Testing

### Unit Tests (TDD — write before implementation)

**`check_capability_bit()` tests:**
- Bit 0 in single word `"1"` → true
- Bit 30 (`KEY_A`) in 32-bit word `"40000000"` → true
- Bit 30 in 64-bit word `"40000000"` → true
- Bit 272 (`BTN_LEFT`) in aarch64 format (16-hex-digit words) → true
- Bit 272 in armhf format (8-hex-digit words) → true
- Bit not set → false
- Empty string → false
- Multi-word bitmask with bit in non-last word → true

**`find_mouse_device()` tests (mock sysfs in `/tmp/`):**
- Mouse caps (`REL_X + REL_Y + BTN_LEFT`, no ABS) → detected
- Touchscreen caps (`ABS_X + ABS_Y`, no `REL_X`) → not detected as mouse
- Device with both ABS and REL (touchscreen with mouse emulation) → skipped (ABS takes priority)
- No accessible devices → returns nullopt

**`find_keyboard_device()` tests (mock sysfs in `/tmp/`):**
- Keyboard caps (`KEY_A` set) → detected
- Power button (`KEY_POWER=116` but no `KEY_A`) → not detected
- No accessible devices → returns nullopt

### Integration (on Pi hardware)
- Touchscreen-only: behavior unchanged
- Mouse-only: mouse works as pointer with visible cursor
- Touch + mouse: both work simultaneously
- USB keyboard: keyboard input works
- Combo keyboard+trackpad: both functions work
- No input devices: graceful failure with error message

## Non-Goals

- Hot-plug detection (devices must be connected at startup)
- Trackpad multi-finger gesture support (touchpads work as basic pointers)
- Multiple mice/keyboards (first detected device of each type is used)
