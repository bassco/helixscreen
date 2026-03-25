# AMS Per-Unit Environment Display & Dryer Control — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the dryer info bar and modal with a per-unit environment display that shows temp/humidity on unit cards, with a full overlay for details and dryer controls.

**Architecture:** Three layers — compact inline indicator on unit cards, full environment overlay opened on tap, backend capability abstraction. Reuses existing per-unit subject infrastructure. Deletes the old dryer card/bar/modal entirely.

**Tech Stack:** LVGL 9.5 XML components, C++ observer pattern, lv_subject_t bindings, existing AmsState subject infrastructure.

**Spec:** `docs/devel/plans/2026-03-25-ams-environment-display-design.md`

---

### Task 1: Backend Abstraction — Capability Methods & Material Comfort Data

**Files:**
- Modify: `include/ams_backend.h` (~line 551, 565)
- Modify: `include/ams_backend_cfs.h`
- Modify: `src/printer/ams_backend_cfs.cpp`
- Modify: `include/filament_database.h` (~line 339, near DryingPreset)
- Test: `tests/unit/test_ams_backend_cfs.cpp`

- [ ] **Step 1: Write test for has_environment_sensors() and MaterialComfortRange**

```cpp
// In test_ams_backend_cfs.cpp
TEST_CASE("CFS environment capabilities", "[ams][cfs]") {
    // CFS backend must report environment sensor support
    // Cannot instantiate without api/client, so test via parse_box_status which
    // returns AmsSystemInfo with environment data set
    auto status = make_cfs_status_json();
    auto info = AmsBackendCfs::parse_box_status(status["box"]);
    REQUIRE(info.units.size() > 0);
    REQUIRE(info.units[0].environment.has_value());
}

TEST_CASE("Material comfort ranges", "[filament]") {
    auto range = filament::get_comfort_range("PLA");
    REQUIRE(range != nullptr);
    REQUIRE(range->max_humidity_good == Catch::Approx(50.0f));
    REQUIRE(range->max_humidity_warn == Catch::Approx(65.0f));

    auto unknown = filament::get_comfort_range("UNKNOWN_MATERIAL");
    REQUIRE(unknown == nullptr);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[cfs]" "[filament]"`
Expected: FAIL — `get_comfort_range` doesn't exist

- [ ] **Step 3: Add has_environment_sensors() to AmsBackend base class**

In `include/ams_backend.h`, add near the other capability methods (~line 540):
```cpp
[[nodiscard]] virtual bool has_environment_sensors() const { return false; }
```

**Dryer API change:** Adding `int unit = 0` to virtual `start_drying`/`stop_drying` changes the vtable signature. ALL backend overrides must be updated:
- `src/printer/ams_backend_happy_hare.cpp` — add `int unit` param
- `src/printer/ams_backend_mock.cpp` — add `int unit` param
- `src/printer/ams_backend_valgace.cpp` — add `int unit` param
- Their corresponding headers

```cpp
virtual AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1, int unit = 0);
virtual AmsError stop_drying(int unit = 0);
```

- [ ] **Step 4: Override in CFS backend**

In `include/ams_backend_cfs.h`, add:
```cpp
[[nodiscard]] bool has_environment_sensors() const override { return true; }
```

- [ ] **Step 5: Add MaterialComfortRange to filament_database.h (header-only, inline)**

`filament_database.h` is header-only — there is no `.cpp` file. All functions must be inline.

```cpp
struct MaterialComfortRange {
    const char* material;
    float max_humidity_good;  // Below = green
    float max_humidity_warn;  // Below = yellow, above = red
};

inline const MaterialComfortRange* get_comfort_range(const std::string& material);
```

Implement inline lookup table:
```cpp
static inline const MaterialComfortRange COMFORT_RANGES[] = {
    {"PLA",    50.0f, 65.0f},
    {"PETG",   40.0f, 55.0f},
    {"ABS",    35.0f, 50.0f},
    {"ASA",    35.0f, 50.0f},
    {"PA",     20.0f, 35.0f},
    {"Nylon",  20.0f, 35.0f},
    {"TPU",    40.0f, 55.0f},
    {"PC",     30.0f, 45.0f},
    {"PVA",    15.0f, 30.0f},
    {"HIPS",   40.0f, 55.0f},
};
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[cfs]" "[filament]" -v`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add include/ams_backend.h include/ams_backend_cfs.h src/printer/ams_backend_cfs.cpp \
        include/filament_database.h tests/unit/test_ams_backend_cfs.cpp
git commit -m "feat(ams): add environment capability methods and material comfort ranges"
```

---

### Task 2: Compact Environment Indicator — XML Component

**Files:**
- Create: `ui_xml/components/ams_environment_indicator.xml`
- Modify: `src/ui/ui_panel_ams.cpp` (~line 104, registration)
- Modify: `src/application/main.cpp` (if component registration needed there)

- [ ] **Step 1: Create the XML component**

Create `ui_xml/components/ams_environment_indicator.xml`:
```xml
<?xml version="1.0"?>
<component>
  <!--
    AMS Environment Indicator - Compact temp/humidity row for unit cards.

    Shows current temperature and humidity from per-unit subjects.
    Hidden when no environment data available (values are 0).
    Clickable: opens environment overlay.

    Subjects (set by index via C++):
    - ams_env_ind_temp_text: formatted temp string ("24°C")
    - ams_env_ind_humidity_text: formatted humidity string ("46%")
    - ams_env_ind_visible: 1 when environment data available
    - ams_env_ind_humidity_color: color token for humidity severity
    - ams_env_ind_drying_active: 1 when drying in progress
    - ams_env_ind_drying_text: "47°C → 55°C  2:30 left"

    Callback:
    - on_env_indicator_clicked: opens environment overlay
  -->
  <view name="ams_environment_indicator"
        extends="lv_obj" width="100%" height="content"
        style_pad_top="#space_xxs" style_pad_bottom="#space_xxs"
        style_pad_left="#space_sm" style_pad_right="#space_sm"
        flex_flow="row" style_flex_main_place="center" style_flex_cross_place="center"
        style_pad_gap="#space_sm" scrollable="false" clickable="true">
    <bind_flag_if_eq subject="ams_env_ind_visible" flag="hidden" ref_value="0"/>
    <event_cb trigger="clicked" callback="on_env_indicator_clicked"/>
    <!-- Passive mode: temp + humidity -->
    <lv_obj width="content" height="content" style_pad_all="0"
            flex_flow="row" style_flex_cross_place="center" style_pad_gap="#space_xs"
            scrollable="false" clickable="false" event_bubble="true">
      <bind_flag_if_not_eq subject="ams_env_ind_drying_active" flag="hidden" ref_value="0"/>
      <icon src="thermometer" size="sm" variant="secondary"
            clickable="false" event_bubble="true"/>
      <text_small bind_text="ams_env_ind_temp_text"
                  clickable="false" event_bubble="true"/>
      <divider_vertical clickable="false" event_bubble="true"/>
      <icon src="water" size="sm" variant="secondary"
            clickable="false" event_bubble="true"/>
      <text_small name="humidity_value" bind_text="ams_env_ind_humidity_text"
                  clickable="false" event_bubble="true"/>
    </lv_obj>
    <!-- Drying mode: active status -->
    <lv_obj width="content" height="content" style_pad_all="0"
            flex_flow="row" style_flex_cross_place="center" style_pad_gap="#space_xs"
            scrollable="false" clickable="false" event_bubble="true">
      <bind_flag_if_eq subject="ams_env_ind_drying_active" flag="hidden" ref_value="0"/>
      <icon src="heat_wave" size="sm" variant="warning"
            clickable="false" event_bubble="true"/>
      <text_small bind_text="ams_env_ind_drying_text" style_text_color="#warning"
                  clickable="false" event_bubble="true"/>
    </lv_obj>
  </view>
</component>
```

- [ ] **Step 2: Register component in ui_panel_ams.cpp**

In `ensure_ams_widgets_registered()` (~line 108), add:
```cpp
lv_xml_register_component_from_file("A:ui_xml/components/ams_environment_indicator.xml");
```

Register the callback:
```cpp
lv_xml_register_event_cb("on_env_indicator_clicked", on_env_indicator_clicked);
```

- [ ] **Step 3: Add per-unit indicator display subjects to AmsState**

The indicator needs **per-unit** subjects since multi-unit setups show multiple cards simultaneously. Use arrays like the existing `unit_temp_[MAX_UNITS]` pattern.

In `include/ams_state.h`, add:
```cpp
// Per-unit environment indicator display subjects
lv_subject_t env_ind_temp_text_[MAX_UNITS];
char env_ind_temp_text_buf_[MAX_UNITS][16];
lv_subject_t env_ind_humidity_text_[MAX_UNITS];
char env_ind_humidity_text_buf_[MAX_UNITS][16];
lv_subject_t env_ind_visible_[MAX_UNITS];
lv_subject_t env_ind_drying_active_[MAX_UNITS];
lv_subject_t env_ind_drying_text_[MAX_UNITS];
char env_ind_drying_text_buf_[MAX_UNITS][32];
```

Initialize in `init_subjects()` with indexed XML names (e.g., `ams_env_ind_0_temp_text`).

In `sync_from_backend()`, format per-unit environment data:
```cpp
for (const auto& unit : info.units) {
    int idx = unit.unit_index;
    if (idx < 0 || idx >= MAX_UNITS) continue;
    if (unit.environment.has_value()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", (int)unit.environment->temperature_c);
        lv_subject_copy_string(&env_ind_temp_text_[idx], buf);
        snprintf(buf, sizeof(buf), "%d%%", (int)unit.environment->humidity_pct);
        lv_subject_copy_string(&env_ind_humidity_text_[idx], buf);
        if (lv_subject_get_int(&env_ind_visible_[idx]) != 1)
            lv_subject_set_int(&env_ind_visible_[idx], 1);
    } else {
        if (lv_subject_get_int(&env_ind_visible_[idx]) != 0)
            lv_subject_set_int(&env_ind_visible_[idx], 0);
    }
}
```

**Note:** The single-unit AMS panel (ams_panel.xml) uses unit index 0 subjects. The overview panel creates per-card indicators that bind to each unit's subjects. The C++ code in AmsOverviewPanel must set the correct subject names on each card's indicator after creation.

- [ ] **Step 4: Build and test visually**

Run: `make -j && ./build/bin/helix-screen --test -vv`
Navigate to AMS panel → verify indicator shows below slot grid with temp/humidity values.

- [ ] **Step 5: Commit**

```bash
git add ui_xml/components/ams_environment_indicator.xml src/ui/ui_panel_ams.cpp \
        include/ams_state.h src/printer/ams_state.cpp
git commit -m "feat(ams): add compact environment indicator on unit cards"
```

---

### Task 3: Wire Indicator into Unit Card XML

**Files:**
- Modify: `ui_xml/components/ams_unit_detail.xml` (~line 41, before closing tag)
- Modify: `ui_xml/ams_unit_card.xml` (overview card, at `ui_xml/` root not `components/`)
- Modify: `ui_xml/ams_panel.xml` (line 62, remove dryer info bar)
- Modify: `ui_xml/ams_overview_panel.xml` (line 68, remove dryer info bar)

- [ ] **Step 1: Add indicator to ams_unit_detail.xml**

Before the closing `</view>` tag, add:
```xml
<ams_environment_indicator name="env_indicator"/>
```

- [ ] **Step 2: Add indicator to ams_unit_card.xml**

Add below the slot bars / count label area.

- [ ] **Step 3: Remove ams_dryer_info_bar from both panels**

In `ams_panel.xml` line 62: remove `<ams_dryer_info_bar name="dryer_info_bar"/>`
In `ams_overview_panel.xml` line 68: remove `<ams_dryer_info_bar name="dryer_info_bar"/>`

- [ ] **Step 4: Build and verify**

Run: `make -j && ./build/bin/helix-screen --test -vv`
Verify: dryer bar gone, environment indicator visible on unit card.

- [ ] **Step 5: Commit**

```bash
git add ui_xml/components/ams_unit_detail.xml ui_xml/components/ams_unit_card.xml \
        ui_xml/ams_panel.xml ui_xml/ams_overview_panel.xml
git commit -m "feat(ams): wire environment indicator into unit cards, remove dryer info bar"
```

---

### Task 4: Environment Overlay — XML Layout

**Files:**
- Create: `ui_xml/ams_environment_overlay.xml`

- [ ] **Step 1: Create overlay XML**

Create `ui_xml/ams_environment_overlay.xml` with:
- Header: back button + "Environment" title + unit name
- Large readout section: thermometer icon + temp, water icon + humidity (color-coded)
- Material comfort ranges card: list of loaded materials with OK/Marginal/Too humid status
- Dryer controls section (hidden when no dryer):
  - Smart preset recommendation
  - Preset dropdown
  - Temperature input field
  - Duration input field
  - Start/Stop button
  - Progress bar + countdown (visible when drying active)
- No-dryer note (hidden when dryer available)

Use subjects for all dynamic data. Follow overlay pattern from existing panels (e.g., ams_panel.xml structure with back button + overlay_panel_width).

- [ ] **Step 2: Register component**

In `ui_panel_ams.cpp`, register the overlay XML.

- [ ] **Step 3: Build and verify layout renders**

Run: `make -j && ./build/bin/helix-screen --test -vv`

- [ ] **Step 4: Commit**

```bash
git add ui_xml/ams_environment_overlay.xml src/ui/ui_panel_ams.cpp
git commit -m "feat(ams): create environment overlay XML layout"
```

---

### Task 5: Environment Overlay — C++ Logic

**Files:**
- Create: `include/ui_ams_environment_overlay.h`
- Create: `src/ui/ui_ams_environment_overlay.cpp`
- Modify: `src/ui/ui_panel_ams.cpp` (callback registration)

- [ ] **Step 1: Create header**

```cpp
// include/ui_ams_environment_overlay.h
#pragma once
#include "observer_factory.h"
#include <lvgl.h>

namespace helix::ui {

class AmsEnvironmentOverlay {
  public:
    static void register_callbacks();
    static void show(int unit_index);

  private:
    static void on_back_clicked(lv_event_t* e);
    static void on_preset_changed(lv_event_t* e);
    static void on_start_drying(lv_event_t* e);
    static void on_stop_drying(lv_event_t* e);

    static void update_comfort_ranges(int unit_index);
    static void update_smart_preset(int unit_index);
    static void update_dryer_status(int unit_index);

    static int active_unit_;
    static lv_obj_t* overlay_;
    static bool callbacks_registered_;
};

} // namespace helix::ui
```

- [ ] **Step 2: Implement core logic**

In `src/ui/ui_ams_environment_overlay.cpp`:
- `show(int unit_index)`: creates overlay from XML, populates with data from AmsState subjects for the given unit, sets up observers
- `update_comfort_ranges()`: reads slot materials from backend, looks up MaterialComfortRange for each, sets color-coded status text
- `update_smart_preset()`: determines if all slots have same material, pre-selects preset dropdown
- `on_start_drying()`: reads temp/duration from input fields, calls `backend->start_drying(temp, duration, -1, unit)`
- `on_stop_drying()`: calls `backend->stop_drying(unit)`
- Error handling: use `ui_error_reporting.h` toast on failure

- [ ] **Step 3: Wire indicator click to overlay**

The `on_env_indicator_clicked` callback (from Task 2) calls `AmsEnvironmentOverlay::show(unit_index)`.

- [ ] **Step 4: Register callbacks in ui_panel_ams.cpp**

```cpp
AmsEnvironmentOverlay::register_callbacks();
```

- [ ] **Step 5: Build and test**

Run: `make -j && ./build/bin/helix-screen --test -vv`
Test: click environment indicator → overlay opens, shows correct data, back button works.

- [ ] **Step 6: Commit**

```bash
git add include/ui_ams_environment_overlay.h src/ui/ui_ams_environment_overlay.cpp \
        src/ui/ui_panel_ams.cpp
git commit -m "feat(ams): implement environment overlay with comfort ranges and dryer controls"
```

---

### Task 6: Delete Old Dryer Components

**Files:**
- Delete: `ui_xml/components/ams_dryer_info_bar.xml`
- Delete: `ui_xml/dryer_presets_modal.xml`
- Delete: `src/ui/ui_ams_dryer_card.cpp`
- Delete: `include/ui_ams_dryer_card.h`
- Modify: `src/ui/ui_panel_ams.cpp` (remove dryer registration/includes)
- Modify: `src/ui/ui_panel_ams_overview.cpp` (remove AmsDryerCard usage)
- Modify: `src/printer/ams_state.cpp` (remove dryer-specific subject wiring that's now obsolete)
- Modify: `include/ams_state.h` (mark old dryer subjects as deprecated or remove if no longer referenced)

- [ ] **Step 1: Remove dryer registrations from ui_panel_ams.cpp**

Remove lines:
```cpp
helix::ui::AmsDryerCard::register_callbacks_static();
lv_xml_register_component_from_file("A:ui_xml/ams_dryer_card.xml");
lv_xml_register_component_from_file("A:ui_xml/dryer_presets_modal.xml");
lv_xml_register_component_from_file("A:ui_xml/components/ams_dryer_info_bar.xml");
```

Remove `#include "ui_ams_dryer_card.h"`.

- [ ] **Step 2: Remove AmsDryerCard from overview panel**

In `src/ui/ui_panel_ams_overview.cpp`, remove any `AmsDryerCard` member variables, setup calls, and includes.

- [ ] **Step 3: Delete the files**

```bash
git rm ui_xml/components/ams_dryer_info_bar.xml
git rm ui_xml/dryer_presets_modal.xml
git rm src/ui/ui_ams_dryer_card.cpp
git rm include/ui_ams_dryer_card.h
```

- [ ] **Step 4: Build to verify clean compile**

Run: `make -j`
Expected: clean build, no references to deleted files.

- [ ] **Step 5: Run tests**

Run: `make test-run`
Expected: all tests pass (dryer card had no dedicated tests).

- [ ] **Step 6: Commit**

```bash
git rm ui_xml/components/ams_dryer_info_bar.xml ui_xml/dryer_presets_modal.xml \
       src/ui/ui_ams_dryer_card.cpp include/ui_ams_dryer_card.h
git add src/ui/ui_panel_ams.cpp src/ui/ui_panel_ams_overview.cpp \
        include/ui_panel_ams_overview.h
git commit -m "refactor(ams): remove old dryer info bar, dryer card, and presets modal"
```

---

### Task 7: Humidity Color Coding

**Files:**
- Modify: `src/ui/ui_ams_environment_overlay.cpp`
- Modify: `src/printer/ams_state.cpp` (indicator subject formatting)

- [ ] **Step 1: Implement humidity color logic**

In the indicator subject sync code, determine color based on loaded materials:
```cpp
// Get the most restrictive material's comfort range
float humidity = unit.environment->humidity_pct;
auto* range = get_strictest_comfort_range(info); // checks all loaded materials
if (!range || humidity < range->max_humidity_good) {
    // Green
    lv_subject_copy_string(&env_ind_humidity_color_, "success");
} else if (humidity < range->max_humidity_warn) {
    // Yellow
    lv_subject_copy_string(&env_ind_humidity_color_, "warning");
} else {
    // Red
    lv_subject_copy_string(&env_ind_humidity_color_, "error");
}
```

- [ ] **Step 2: Apply color in XML via bind_style or C++ styling**

The indicator's humidity text gets styled based on the color subject. Since `bind_style` with dynamic colors is complex, apply in C++ via the observer callback:
```cpp
auto color = ui_theme_get_color(color_token);
lv_obj_set_style_text_color(humidity_label, color, 0);
```

- [ ] **Step 3: Apply same logic to overlay large readout**

- [ ] **Step 4: Build and test visually**

Test with CFS (46% humidity, PLA loaded) — should show green/yellow depending on threshold.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_ams_environment_overlay.cpp src/printer/ams_state.cpp
git commit -m "feat(ams): add humidity color coding based on material comfort ranges"
```

---

### Task 8: Smart Preset Detection

**Files:**
- Modify: `src/ui/ui_ams_environment_overlay.cpp`

- [ ] **Step 1: Implement material analysis**

```cpp
void AmsEnvironmentOverlay::update_smart_preset(int unit_index) {
    auto* backend = AmsState::instance().get_backend();
    if (!backend) return;
    auto info = backend->get_system_info();

    // Collect unique materials from unit's slots
    std::set<std::string> materials;
    for (const auto& unit : info.units) {
        if (unit.unit_index != unit_index) continue;
        for (const auto& slot : unit.slots) {
            if (!slot.material.empty() && slot.status != SlotStatus::EMPTY) {
                materials.insert(slot.material);
            }
        }
    }

    if (materials.size() == 1) {
        // Single material — recommend its preset
        auto preset = filament::get_drying_preset(*materials.begin());
        // Set recommendation text + pre-select dropdown
    } else if (materials.size() > 1) {
        // Mixed — warn and suggest conservative
        // Set warning text, don't auto-select
    }
}
```

- [ ] **Step 2: Wire to overlay show()**

Call `update_smart_preset(unit_index)` when overlay opens.

- [ ] **Step 3: Test with CFS (all PLA)**

Verify: "Loaded: PLA (all slots) — Recommended: 55°C / 4h" shows and PLA preset is selected in dropdown.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_ams_environment_overlay.cpp
git commit -m "feat(ams): smart preset detection from loaded slot materials"
```

---

### Task 9: Integration Testing & Polish

**Files:**
- Modify: various (cleanup)
- Test: `tests/unit/test_ams_backend_cfs.cpp`

- [ ] **Step 1: Add tests for environment indicator visibility**

Test that indicator subjects are set correctly when backend has environment data vs when it doesn't.

- [ ] **Step 2: Test overlay open/close lifecycle**

Verify no observer leaks, no dangling pointers after overlay close.

- [ ] **Step 3: Test on K2 Max hardware**

Cross-compile and deploy:
```bash
K2_HOST=192.168.30.197 make k2-docker
K2_HOST=192.168.30.197 make deploy-k2
```

Verify:
- Environment indicator shows on AMS panel with real CFS data (24°C / 46%)
- Tap indicator opens overlay with comfort ranges for PLA
- Overlay shows "No dryer available" note
- Back button returns to AMS panel
- No crashes

- [ ] **Step 4: Test mock dryer backend**

Run with mock AMS: `HELIX_MOCK_AMS=1 ./build/bin/helix-screen --test -vv`
Verify dryer controls appear when mock backend supports drying.

- [ ] **Step 5: Final commit**

```bash
git add tests/unit/test_ams_backend_cfs.cpp
git commit -m "test(ams): add environment display integration tests"
```

---

### Task 10: Translation & Documentation

**Files:**
- Modify: `ui_xml/translations/translations.xml` (add new strings)
- Modify: `docs/devel/FILAMENT_MANAGEMENT.md` (update environment section)

- [ ] **Step 1: Add translation strings**

Add to `ui_xml/translations/translations.xml`:
- "Environment" (overlay title)
- "Ambient Temp" / "Humidity" (labels)
- "Material Comfort Ranges" (card title)
- "OK" / "Marginal" / "Too humid" (status labels)
- "No dryer available." / "Store filament in dry box for best results."
- "Loaded:" / "Recommended:" / "Start Drying" / "Stop Drying"
- "Mixed materials loaded..." (warning)
- Preset names (PLA, PETG, ABS, etc.) are NOT translated per [L070]

- [ ] **Step 2: Update filament management docs**

Add section about per-unit environment display to `docs/devel/FILAMENT_MANAGEMENT.md`.

- [ ] **Step 3: Commit**

```bash
git add ui_xml/translations/translations.xml docs/devel/FILAMENT_MANAGEMENT.md
git commit -m "docs: add environment display to filament management docs + translations"
```
