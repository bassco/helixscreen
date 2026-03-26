# AD5M Memory Diet — Design Spec

*2026-03-26*

## Problem

Telemetry shows average RSS on AD5M increased from ~11.5MB to ~15MB since v0.98.12 (3/23). The AD5M has 128MB total RAM, and every MB counts. The increase comes from binary bloat (static linking means code = RSS) and new runtime allocations (canvases, sound themes). This spec covers immediate optimizations and identifies areas for further investigation.

## Target Device

- **FlashForge Adventurer 5M**: Cortex-A7, armv7l, 128MB RAM
- **Display**: 800x480, ARGB8888 (32bpp)
- **Build**: Fully static (`-static`), `-Os`, LTO, `--gc-sections`
- **Sound**: PWM buzzer only (no ALSA, no tracker)
- **No**: Bluetooth, label printers, LED strips, CFS filament system

## Optimizations

### 1. Compile-Time Feature Guards

Add `#define` flags to gate features unnecessary on AD5M. All platforms default to enabled. AD5M (and AD5X where applicable) explicitly disables them in `mk/cross.mk` via `TARGET_CFLAGS`.

| Guard | Disables | Files Affected |
|-------|----------|----------------|
| `HELIX_HAS_LABEL_PRINTER` | Brother QL, Brother PT, Phomemo, Niimbot, MakeID, IPP protocol, PWG raster, sheet label layout, label printer settings UI, label bitmap/renderer | ~15 source files |
| `HELIX_HAS_LED` | LED controller, LED widget, LED controls overlay | ~5 source files |
| `HELIX_HAS_CFS` | CFS AMS backend (K2-only) | ~2 source files |
| `HELIX_HAS_BLUETOOTH` | Bluetooth discovery helper, BT transport utilities | ~3 source files |

**Implementation approach:**

- Wrap entire source files with `#if HELIX_HAS_X` / `#endif` (not per-function guards)
- Gate registration calls: `lv_xml_register_*()`, `panel_widget_registry` entries, event callback registrations
- Gate `#include` directives in files that reference guarded headers
- In `Makefile`: default all guards to 1; in `mk/cross.mk` AD5M section, add `-DHELIX_HAS_LABEL_PRINTER=0 -DHELIX_HAS_LED=0 -DHELIX_HAS_CFS=0 -DHELIX_HAS_BLUETOOTH=0` to `TARGET_CFLAGS`
- Menu items / settings entries for gated features: use `#if` to omit XML registration or hide dynamically via a runtime capability check

**Estimated savings:** 200-500KB binary reduction (static link means this is RSS)

### 2. Gradient Canvas: 256² → 128² on Constrained Devices

**File:** `src/ui/ui_gradient_canvas.cpp`

Change `GRADIENT_BUFFER_SIZE` from a compile-time constant to a runtime value:

```cpp
static int32_t get_gradient_buffer_size() {
    static int32_t size = MemoryInfo::get().is_constrained_device() ? 128 : 256;
    return size;
}
```

ARGB8888 at 256×256 = 256KB. At 128×128 = 64KB. **Saves 192KB.**

Visual quality: 128px gradient is still smooth on an 800px-wide display — gradient bands would only be visible below ~64px.

### 3. HSV Picker: 200² → 128² on Constrained Devices

**File:** `src/ui/ui_hsv_picker.cpp`

Change `DEFAULT_SV_SIZE` from 200 to a runtime-selected value:

```cpp
static int32_t get_default_sv_size() {
    static int32_t size = MemoryInfo::get().is_constrained_device() ? 128 : 200;
    return size;
}
```

Also scale `DEFAULT_HUE_HEIGHT` proportionally (24 → 16).

200×200×4 = 160KB → 128×128×4 = 64KB. Plus hue bar: 200×24×4 = 19.2KB → 128×16×4 = 8.2KB. **Saves ~107KB.**

### 4. Lazy Exclude Object Map Canvas

**File:** `src/ui/ui_exclude_object_map_view.cpp`

Currently the canvas buffer (`lv_draw_buf_create(vw, vh, ARGB8888)`) persists for the panel lifetime. On 800x480 this could be ~600KB.

Change to:
- Allocate `canvas_buf_` only when the map view is shown (user taps the map button)
- Free it when the map view is closed/hidden via `lv_draw_buf_destroy()`
- Null-check before any draw operations

**Saves:** Up to ~600KB when not viewing exclude objects (which is most of the time).

### 5. Skip Sound Theme Loading When Sounds Disabled

**File:** `src/system/sound_manager.cpp`

In `SoundManager::initialize()`, check `AudioSettingsManager::instance().get_sounds_enabled()` before calling `load_theme()`. If sounds are disabled, skip theme loading entirely. Load the theme lazily when sounds are first enabled.

```cpp
void SoundManager::initialize() {
    // ...
    backend_ = create_backend();
    if (!backend_) return;

    theme_name_ = AudioSettingsManager::instance().get_sound_theme();
    if (AudioSettingsManager::instance().get_sounds_enabled()) {
        load_theme(theme_name_);
    }
    // ...
}
```

Also defer in `play()` — if theme not loaded yet and sounds just got enabled, load then.

**Saves:** ~50-100KB of parsed JSON theme data when sounds are off.

### 6. GCode Layer Cache: 4MB → 2MB on Constrained Devices

**File:** `include/gcode_layer_cache.h`

Change:
```cpp
static constexpr size_t DEFAULT_BUDGET_CONSTRAINED = 4 * 1024 * 1024;  // was 4MB
```
to:
```cpp
static constexpr size_t DEFAULT_BUDGET_CONSTRAINED = 2 * 1024 * 1024;  // 2MB
```

The adaptive mode already shrinks to 1MB under pressure (min budget). Reducing the starting budget from 4MB to 2MB means less memory is consumed when the user first opens G-code preview. Layer re-parsing frequency increases slightly, but the streaming controller already handles this gracefully.

**Saves:** 2MB when viewing G-code on constrained devices.

## Future Investigation (Not in This PR)

### A. RGB565 Canvases (#6 from diet plan)
Gradient and spool canvases don't need alpha. RGB565 = 2 bytes/pixel vs ARGB8888 = 4 bytes/pixel. Potential 50% savings on canvas buffers. Need to verify LVGL canvas rendering works correctly with RGB565 format.

### B. Lazy Overlay/Modal Creation (#9)
Currently all panels are pre-created at startup. Overlays and modals that are rarely accessed could be created on-demand and destroyed when closed. Significant architectural change — needs profiling to identify which overlays are worth it.

### C. Dynamic Linking (#10)
Static linking adds ~3-5MB vs dynamic. AD5M has glibc 2.25; our toolchain targets 2.33. Would need to either ship our own glibc or find a compatible toolchain. High effort, high reward.

### D. LTO Effectiveness Verification (#11)
Verify that LTO is actually eliminating dead code. Compare binary size with and without `-flto`. If LTO isn't effective, investigate why (e.g., are we preventing it with visibility attributes or shared library patterns?).

## Testing

1. Build for AD5M with `make ad5m-docker`
2. Compare binary size before/after
3. Deploy and check RSS via telemetry or `cat /proc/$(pidof helix-screen)/status | grep VmRSS`
4. Verify all gated features still work on Pi (where guards are enabled)
5. Verify gradient and HSV picker look acceptable at 128px on 800x480
6. Verify exclude object map works correctly with lazy allocation

## Success Criteria

- AD5M RSS ≤ 12MB at idle (down from 15MB)
- No functional regressions on Pi or desktop
- Binary size measurably smaller
