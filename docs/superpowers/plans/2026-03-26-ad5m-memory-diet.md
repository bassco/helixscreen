# AD5M Memory Diet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce AD5M RSS from ~15MB to ≤12MB by eliminating dead code and reducing runtime allocations on constrained devices.

**Architecture:** Compile-time feature guards (`HELIX_HAS_*`) gate entire .cpp files for features the AD5M doesn't use. Runtime checks via existing `MemoryInfo::is_constrained_device()` reduce canvas/cache sizes. Build flags default to enabled (1) on all platforms; AD5M explicitly disables them in `mk/cross.mk`.

**Tech Stack:** C++ preprocessor guards, LVGL draw buffers, Makefile build system

**Spec:** `docs/superpowers/specs/2026-03-26-ad5m-memory-diet-design.md`

---

## File Map

### Build system
- Modify: `Makefile` — define default feature flags
- Modify: `mk/cross.mk:193-232` — AD5M-specific flag overrides

### Label printer gate (`HELIX_HAS_LABEL_PRINTER`)
- Modify (wrap entire file): `src/system/label_printer.cpp`, `src/system/label_printer_settings.cpp`, `src/system/label_printer_utils.cpp`, `src/system/label_bitmap.cpp`, `src/system/label_renderer.cpp`, `src/system/sheet_label_layout.cpp`, `src/system/brother_ql_printer.cpp`, `src/system/brother_ql_protocol.cpp`, `src/system/brother_ql_bt_printer.cpp`, `src/system/brother_pt_protocol.cpp`, `src/system/brother_pt_bt_printer.cpp`, `src/system/phomemo_printer.cpp`, `src/system/phomemo_protocol.cpp`, `src/system/phomemo_bt_printer.cpp`, `src/system/niimbot_protocol.cpp`, `src/system/niimbot_bt_printer.cpp`, `src/system/makeid_protocol.cpp`, `src/system/makeid_bt_printer.cpp`, `src/system/ipp_printer.cpp`, `src/system/ipp_protocol.cpp`, `src/system/pwg_raster.cpp`, `src/ui/ui_settings_label_printer.cpp`, `src/ui/ipp_print_modal.cpp`
- Modify (guard cross-refs): `src/xml_registration.cpp`, `src/ui/ui_panel_settings.cpp`, `src/ui/ui_ams_edit_modal.cpp`, `src/ui/ui_spoolman_edit_modal.cpp`, `src/ui/ui_panel_spoolman.cpp`, `src/ui/ui_spoolman_overlay.cpp`

### CFS gate (`HELIX_HAS_CFS`)
- Modify (wrap entire file): `src/printer/ams_backend_cfs.cpp`
- Modify (guard cross-ref): `src/printer/ams_backend.cpp`

### LED gate (`HELIX_HAS_LED`)
- Modify (wrap entire file): `src/led/led_controller.cpp`, `src/led/led_auto_state.cpp`, `src/api/moonraker_api_led.cpp`, `src/printer/printer_led_state.cpp`, `src/ui/ui_led_control_overlay.cpp`, `src/ui/ui_led_chip_factory.cpp`, `src/ui/ui_wizard_led_select.cpp`, `src/ui/ui_settings_led.cpp`, `src/ui/panel_widgets/led_widget.cpp`, `src/ui/panel_widgets/led_controls_widget.cpp`
- Modify (stub header): `include/led/led_controller.h` — inline no-op stubs when `HELIX_HAS_LED=0`
- Modify (guard cross-refs): `src/xml_registration.cpp`, `src/ui/panel_widget_registry.cpp`, `src/application/application.cpp`, `src/application/subject_initializer.cpp`, `src/api/moonraker_discovery_sequence.cpp`, `src/system/settings_manager.cpp`, `src/printer/printer_state.cpp`, `src/printer/printer_discovery.cpp`, `src/ui/ui_wizard.cpp`, `src/ui/ui_panel_settings.cpp`, `src/ui/ui_panel_print_status.cpp`, `src/ui/ui_print_light_timelapse.cpp`, `src/ui/ui_panel_power.cpp`

### Runtime optimizations
- Modify: `src/ui/ui_gradient_canvas.cpp:34` — constrained-aware size
- Modify: `src/ui/ui_hsv_picker.cpp:27-28` — constrained-aware size
- Modify: `src/system/sound_manager.cpp:63-66` — lazy theme loading
- Modify: `include/gcode_layer_cache.h:47` — reduce constrained budget

---

### Task 1: Add Feature Flags to Build System

**Files:**
- Modify: `Makefile`
- Modify: `mk/cross.mk:214-216`

- [ ] **Step 1: Add default feature flags to Makefile**

After the `CXXFLAGS += $(SOUND_CXXFLAGS) $(TRACKER_CXXFLAGS)` line (around line 685), add:

```makefile
# Feature gates — default ON for all platforms.
# Disabled per-platform in mk/cross.mk for memory-constrained targets.
HELIX_HAS_LABEL_PRINTER ?= 1
HELIX_HAS_LED ?= 1
HELIX_HAS_CFS ?= 1
CXXFLAGS += -DHELIX_HAS_LABEL_PRINTER=$(HELIX_HAS_LABEL_PRINTER) \
            -DHELIX_HAS_LED=$(HELIX_HAS_LED) \
            -DHELIX_HAS_CFS=$(HELIX_HAS_CFS)
```

- [ ] **Step 2: Disable flags for AD5M in mk/cross.mk**

In the AD5M `TARGET_CFLAGS` (line 214), append:

```makefile
    TARGET_CFLAGS := -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a7 \
        -Os -flto -ffunction-sections -fdata-sections -funwind-tables \
        -Wno-error=conversion -Wno-error=sign-conversion -DHELIX_RELEASE_BUILD -DHELIX_PLATFORM_AD5M \
        -DHELIX_HAS_LABEL_PRINTER=0 -DHELIX_HAS_LED=0 -DHELIX_HAS_CFS=0
```

- [ ] **Step 3: Verify native build still compiles**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds (all flags default to 1)

- [ ] **Step 4: Commit**

```bash
git add Makefile mk/cross.mk
git commit -m "build: add HELIX_HAS_LABEL_PRINTER, HELIX_HAS_LED, HELIX_HAS_CFS feature flags"
```

---

### Task 2: Gate Label Printer Code

**Files:**
- Modify: All 23 label printer .cpp files listed in file map
- Modify: `src/xml_registration.cpp`
- Modify: `src/ui/ui_panel_settings.cpp`
- Modify: `src/ui/ui_ams_edit_modal.cpp`, `src/ui/ui_spoolman_edit_modal.cpp`, `src/ui/ui_panel_spoolman.cpp`, `src/ui/ui_spoolman_overlay.cpp`

- [ ] **Step 1: Wrap all label printer .cpp files**

For each of these 23 files, wrap the ENTIRE file contents (after the SPDX header) with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#if HELIX_HAS_LABEL_PRINTER

// ... existing file contents ...

#endif // HELIX_HAS_LABEL_PRINTER
```

Files to wrap:
- `src/system/label_printer.cpp`
- `src/system/label_printer_settings.cpp`
- `src/system/label_printer_utils.cpp`
- `src/system/label_bitmap.cpp`
- `src/system/label_renderer.cpp`
- `src/system/sheet_label_layout.cpp`
- `src/system/brother_ql_printer.cpp`
- `src/system/brother_ql_protocol.cpp`
- `src/system/brother_ql_bt_printer.cpp`
- `src/system/brother_pt_protocol.cpp`
- `src/system/brother_pt_bt_printer.cpp`
- `src/system/phomemo_printer.cpp`
- `src/system/phomemo_protocol.cpp`
- `src/system/phomemo_bt_printer.cpp`
- `src/system/niimbot_protocol.cpp`
- `src/system/niimbot_bt_printer.cpp`
- `src/system/makeid_protocol.cpp`
- `src/system/makeid_bt_printer.cpp`
- `src/system/ipp_printer.cpp`
- `src/system/ipp_protocol.cpp`
- `src/system/pwg_raster.cpp`
- `src/ui/ui_settings_label_printer.cpp`
- `src/ui/ipp_print_modal.cpp`

- [ ] **Step 2: Guard XML registrations in src/xml_registration.cpp**

Wrap the label printer XML registrations:

```cpp
#if HELIX_HAS_LABEL_PRINTER
    register_xml("components/ipp_print_modal.xml");
#endif
```

And later:

```cpp
#if HELIX_HAS_LABEL_PRINTER
    register_xml("label_printer_settings.xml");
#endif
```

- [ ] **Step 3: Guard label printer cross-references**

In `src/ui/ui_panel_settings.cpp`, wrap the `#include "ui_settings_label_printer.h"` and any code that calls `LabelPrinterSettingsOverlay`:

```cpp
#if HELIX_HAS_LABEL_PRINTER
#include "ui_settings_label_printer.h"
#endif
```

Guard the label printer settings menu item registration/callback similarly.

In `src/ui/ui_ams_edit_modal.cpp`, `src/ui/ui_spoolman_edit_modal.cpp`, and `src/ui/ui_panel_spoolman.cpp`, wrap label-related includes and calls:

```cpp
#if HELIX_HAS_LABEL_PRINTER
#include "ipp_print_modal.h"
#include "label_printer_settings.h"
#include "label_printer_utils.h"
#endif
```

Guard any call sites that use label printer functions (print label buttons, etc.) with `#if HELIX_HAS_LABEL_PRINTER`.

In `src/ui/ui_spoolman_overlay.cpp`, wrap `#include "ui_settings_label_printer.h"` and related code.

- [ ] **Step 4: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds with all flags at default (1)

- [ ] **Step 5: Commit**

```bash
git add src/system/label_printer.cpp src/system/label_printer_settings.cpp \
  src/system/label_printer_utils.cpp src/system/label_bitmap.cpp \
  src/system/label_renderer.cpp src/system/sheet_label_layout.cpp \
  src/system/brother_ql_printer.cpp src/system/brother_ql_protocol.cpp \
  src/system/brother_ql_bt_printer.cpp src/system/brother_pt_protocol.cpp \
  src/system/brother_pt_bt_printer.cpp src/system/phomemo_printer.cpp \
  src/system/phomemo_protocol.cpp src/system/phomemo_bt_printer.cpp \
  src/system/niimbot_protocol.cpp src/system/niimbot_bt_printer.cpp \
  src/system/makeid_protocol.cpp src/system/makeid_bt_printer.cpp \
  src/system/ipp_printer.cpp src/system/ipp_protocol.cpp \
  src/system/pwg_raster.cpp src/ui/ui_settings_label_printer.cpp \
  src/ui/ipp_print_modal.cpp src/xml_registration.cpp \
  src/ui/ui_panel_settings.cpp src/ui/ui_ams_edit_modal.cpp \
  src/ui/ui_spoolman_edit_modal.cpp src/ui/ui_panel_spoolman.cpp \
  src/ui/ui_spoolman_overlay.cpp
git commit -m "build: gate label printer code behind HELIX_HAS_LABEL_PRINTER"
```

---

### Task 3: Gate CFS Backend Code

**Files:**
- Modify: `src/printer/ams_backend_cfs.cpp`
- Modify: `include/ams_backend_cfs.h`
- Modify: `src/printer/ams_backend.cpp`

- [ ] **Step 1: Wrap CFS backend source**

In `src/printer/ams_backend_cfs.cpp`, wrap entire contents after SPDX:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#if HELIX_HAS_CFS

// ... existing file contents ...

#endif // HELIX_HAS_CFS
```

- [ ] **Step 2: Wrap CFS header**

In `include/ams_backend_cfs.h`, wrap the class definition:

```cpp
#pragma once
#if HELIX_HAS_CFS

// ... existing header contents ...

#endif // HELIX_HAS_CFS
```

- [ ] **Step 3: Guard cross-reference in ams_backend.cpp**

In `src/printer/ams_backend.cpp`, wrap the include and factory case:

```cpp
#if HELIX_HAS_CFS
#include "ams_backend_cfs.h"
#endif
```

And in the factory switch:

```cpp
    case AmsType::CFS:
#if HELIX_HAS_CFS
        spdlog::debug("[AMS Backend] Creating CFS backend");
        return std::make_unique<printer::AmsBackendCfs>(api, client);
#else
        spdlog::info("[AMS Backend] CFS support not compiled in");
        return nullptr;
#endif
```

- [ ] **Step 4: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/printer/ams_backend_cfs.cpp include/ams_backend_cfs.h src/printer/ams_backend.cpp
git commit -m "build: gate CFS backend behind HELIX_HAS_CFS"
```

---

### Task 4: Gate LED Code

**Files:**
- Modify: All 10 LED .cpp files listed in file map
- Modify: `include/led/led_controller.h` — add no-op stubs
- Modify: `src/xml_registration.cpp`, `src/ui/panel_widget_registry.cpp`
- Modify: Cross-referencing files (application.cpp, printer_state.cpp, etc.)

- [ ] **Step 1: Add no-op stub to led_controller.h**

The key insight: many core files call `LedController::instance()`. Rather than adding `#if` guards everywhere, make the header provide inline no-ops when LED is disabled.

At the top of `include/led/led_controller.h`, after the `#pragma once`:

```cpp
#pragma once

#if !HELIX_HAS_LED
// Stub: LedController exists but all methods are no-ops when LED support is disabled.
// This avoids #if guards in every file that references LedController.
namespace helix::led {
class LedController {
  public:
    static LedController& instance() { static LedController s; return s; }
    void init(class MoonrakerAPI*, class MoonrakerClient*) {}
    void deinit() {}
    void apply_startup_preference() {}
    bool has_leds() const { return false; }
    // Add stubs for any other methods called from non-LED code
};
} // namespace helix::led
#else
// ... existing full header contents ...
#endif // HELIX_HAS_LED
```

Check all call sites in non-LED files to ensure every called method has a stub. The key methods called externally are:
- `instance()` — returns singleton
- `init()`, `deinit()` — lifecycle
- `apply_startup_preference()` — called from application.cpp
- `has_leds()` — capability check

Read the full header to identify all public methods called from outside LED code and add matching stubs.

- [ ] **Step 2: Wrap all LED .cpp files**

For each LED .cpp file, wrap entire contents after SPDX with `#if HELIX_HAS_LED` / `#endif`:

- `src/led/led_controller.cpp`
- `src/led/led_auto_state.cpp`
- `src/api/moonraker_api_led.cpp`
- `src/printer/printer_led_state.cpp`
- `src/ui/ui_led_control_overlay.cpp`
- `src/ui/ui_led_chip_factory.cpp`
- `src/ui/ui_wizard_led_select.cpp`
- `src/ui/ui_settings_led.cpp`
- `src/ui/panel_widgets/led_widget.cpp`
- `src/ui/panel_widgets/led_controls_widget.cpp`

- [ ] **Step 3: Guard LED-only includes in cross-referencing files**

In files that include LED headers ONLY used for LED-specific UI (not LedController itself):

```cpp
// In src/application/application.cpp:
#if HELIX_HAS_LED
#include "led/ui_led_control_overlay.h"
#endif

// In src/application/subject_initializer.cpp:
#if HELIX_HAS_LED
#include "led/ui_led_control_overlay.h"
#endif
```

Guard the corresponding call sites (overlay initialization, subject registration) with `#if HELIX_HAS_LED`.

Note: `#include "led/led_controller.h"` does NOT need guards — the stub header handles it.

- [ ] **Step 4: Guard LED XML registrations**

In `src/xml_registration.cpp`:

```cpp
#if HELIX_HAS_LED
    register_xml("components/panel_widget_led.xml");
    register_xml("components/panel_widget_led_controls.xml");
    register_xml("led_action_chip.xml");
    register_xml("led_color_swatch.xml");
    register_xml("led_control_overlay.xml");
    register_xml("setting_led_chip_row.xml");
    register_xml("led_settings_overlay.xml");
    register_xml("wizard_led_select.xml");
#endif
```

In `src/ui/panel_widget_registry.cpp`:

```cpp
#if HELIX_HAS_LED
void register_led_widget();
void register_led_controls_widget();
#endif
```

Gate the widget table entries and registration calls:

```cpp
#if HELIX_HAS_LED
    {"led",              "LED Light",         ...},
    {"led_controls",     "LED Controls",      ...},
#endif
```

And:

```cpp
#if HELIX_HAS_LED
    register_led_widget();
    register_led_controls_widget();
#endif
```

- [ ] **Step 5: Guard LED references in settings, wizard, and other UI**

In `src/ui/ui_panel_settings.cpp`:
```cpp
#if HELIX_HAS_LED
#include "ui_settings_led.h"
#endif
```
Guard the LED settings menu item.

In `src/ui/ui_wizard.cpp`:
```cpp
#if HELIX_HAS_LED
#include "ui_wizard_led_select.h"
#endif
```
Guard the LED wizard step.

In `src/ui/ui_panel_power.cpp`:
```cpp
#if HELIX_HAS_LED
#include "ui_led_chip_factory.h"
#endif
```
Guard LED chip usage.

In `src/ui/ui_print_light_timelapse.cpp`, `src/ui/ui_panel_print_status.cpp`:
The `#include "led/led_controller.h"` stays (stub handles it). Guard any LED-specific UI logic with `#if HELIX_HAS_LED`.

- [ ] **Step 6: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add include/led/led_controller.h src/led/ src/api/moonraker_api_led.cpp \
  src/printer/printer_led_state.cpp src/ui/ui_led_control_overlay.cpp \
  src/ui/ui_led_chip_factory.cpp src/ui/ui_wizard_led_select.cpp \
  src/ui/ui_settings_led.cpp src/ui/panel_widgets/led_widget.cpp \
  src/ui/panel_widgets/led_controls_widget.cpp src/xml_registration.cpp \
  src/ui/panel_widget_registry.cpp src/application/application.cpp \
  src/application/subject_initializer.cpp src/ui/ui_panel_settings.cpp \
  src/ui/ui_wizard.cpp src/ui/ui_panel_power.cpp \
  src/ui/ui_print_light_timelapse.cpp src/ui/ui_panel_print_status.cpp \
  src/api/moonraker_discovery_sequence.cpp src/system/settings_manager.cpp \
  src/printer/printer_state.cpp src/printer/printer_discovery.cpp
git commit -m "build: gate LED subsystem behind HELIX_HAS_LED"
```

---

### Task 5: Reduce Gradient Canvas on Constrained Devices

**Files:**
- Modify: `src/ui/ui_gradient_canvas.cpp:34`

- [ ] **Step 1: Replace compile-time constant with runtime selection**

In `src/ui/ui_gradient_canvas.cpp`, replace:

```cpp
constexpr int32_t GRADIENT_BUFFER_SIZE = 256;
```

with:

```cpp
#include "memory_utils.h"

static int32_t gradient_buffer_size() {
    static const int32_t size =
        helix::system::MemoryInfo::get().is_constrained_device() ? 128 : 256;
    return size;
}
```

Then replace all uses of `GRADIENT_BUFFER_SIZE` with `gradient_buffer_size()`. There are ~5 references in the file (lines 34, 95, 182, 209-210).

- [ ] **Step 2: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/ui_gradient_canvas.cpp
git commit -m "perf: reduce gradient canvas to 128x128 on constrained devices"
```

---

### Task 6: Reduce HSV Picker on Constrained Devices

**Files:**
- Modify: `src/ui/ui_hsv_picker.cpp:27-28`

- [ ] **Step 1: Replace compile-time constants with runtime selection**

In `src/ui/ui_hsv_picker.cpp`, replace:

```cpp
constexpr int32_t DEFAULT_SV_SIZE = 200;
constexpr int32_t DEFAULT_HUE_HEIGHT = 24;
```

with:

```cpp
#include "memory_utils.h"

static int32_t default_sv_size() {
    static const int32_t size =
        helix::system::MemoryInfo::get().is_constrained_device() ? 128 : 200;
    return size;
}

static int32_t default_hue_height() {
    static const int32_t size =
        helix::system::MemoryInfo::get().is_constrained_device() ? 16 : 24;
    return size;
}
```

Replace all uses of `DEFAULT_SV_SIZE` with `default_sv_size()` and `DEFAULT_HUE_HEIGHT` with `default_hue_height()`. Check lines 347-348 and any other references.

- [ ] **Step 2: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/ui_hsv_picker.cpp
git commit -m "perf: reduce HSV picker to 128x128 on constrained devices"
```

---

### Task 7: Skip Sound Theme Loading When Sounds Disabled

**Files:**
- Modify: `src/system/sound_manager.cpp:50-72`

- [ ] **Step 1: Defer theme loading**

In `SoundManager::initialize()`, change:

```cpp
void SoundManager::initialize() {
    if (initialized_) {
        spdlog::debug("[SoundManager] Already initialized");
        return;
    }

    backend_ = create_backend();
    if (!backend_) {
        spdlog::info("[SoundManager] No sound backend available, sounds disabled");
        return;
    }

    // Load the configured theme
    theme_name_ = AudioSettingsManager::instance().get_sound_theme();
    load_theme(theme_name_);
```

to:

```cpp
void SoundManager::initialize() {
    if (initialized_) {
        spdlog::debug("[SoundManager] Already initialized");
        return;
    }

    backend_ = create_backend();
    if (!backend_) {
        spdlog::info("[SoundManager] No sound backend available, sounds disabled");
        return;
    }

    // Defer theme loading until sounds are actually needed
    theme_name_ = AudioSettingsManager::instance().get_sound_theme();
    if (AudioSettingsManager::instance().get_sounds_enabled()) {
        load_theme(theme_name_);
    } else {
        spdlog::info("[SoundManager] Sounds disabled, deferring theme load");
    }
```

- [ ] **Step 2: Add lazy load in play()**

In `SoundManager::play()`, after the `sounds_enabled` check, add a lazy load before the theme lookup:

```cpp
    // Lazy-load theme on first use (deferred if sounds were disabled at init)
    if (current_theme_.sounds.empty() && !theme_name_.empty()) {
        load_theme(theme_name_);
    }
```

- [ ] **Step 3: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/system/sound_manager.cpp
git commit -m "perf: defer sound theme loading when sounds disabled"
```

---

### Task 8: Reduce GCode Layer Cache on Constrained Devices

**Files:**
- Modify: `include/gcode_layer_cache.h:47`

- [ ] **Step 1: Reduce constrained budget**

In `include/gcode_layer_cache.h`, change:

```cpp
static constexpr size_t DEFAULT_BUDGET_CONSTRAINED = 4 * 1024 * 1024;
```

to:

```cpp
static constexpr size_t DEFAULT_BUDGET_CONSTRAINED = 2 * 1024 * 1024;
```

- [ ] **Step 2: Build and verify**

Run: `make -j 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add include/gcode_layer_cache.h
git commit -m "perf: reduce gcode layer cache from 4MB to 2MB on constrained devices"
```

---

### Task 9: Build Verification — Full Test Suite

- [ ] **Step 1: Run full test suite**

Run: `make test-run 2>&1 | tail -20`
Expected: All tests pass. The feature flags default to 1, so tests should see no difference.

- [ ] **Step 2: Test with flags disabled (simulating AD5M)**

Run: `HELIX_HAS_LABEL_PRINTER=0 HELIX_HAS_LED=0 HELIX_HAS_CFS=0 make -j 2>&1 | tail -10`
Expected: Build succeeds with all three features disabled.

Note: Tests that exercise label printer, LED, or CFS code may need `#if` guards too. If the build fails, identify which test files include gated headers and add matching guards. Test files to check:
- `tests/unit/test_brother_*.cpp`
- `tests/unit/test_phomemo_*.cpp`
- `tests/unit/test_niimbot_*.cpp`
- `tests/unit/test_makeid_*.cpp`
- `tests/unit/test_ipp_*.cpp`
- `tests/unit/test_label_*.cpp`
- `tests/unit/test_led_*.cpp`
- `tests/unit/test_pwg_*.cpp`

Wrap each test file's contents with the same `#if HELIX_HAS_X` guard.

- [ ] **Step 3: Fix any test compilation issues and commit**

```bash
git add tests/
git commit -m "test: gate label/LED/CFS tests behind feature flags"
```

---

### Task 10: AD5M Docker Build Verification

- [ ] **Step 1: Cross-compile for AD5M**

Run: `make ad5m-docker 2>&1 | tail -20`
Expected: Build succeeds. Binary at `build/ad5m/bin/helix-screen`.

- [ ] **Step 2: Check binary size**

Run: `ls -la build/ad5m/bin/helix-screen`
Compare against previous release binary size. Expected: measurable reduction.

- [ ] **Step 3: Commit any remaining fixes**

If the AD5M build revealed issues, fix and commit.
