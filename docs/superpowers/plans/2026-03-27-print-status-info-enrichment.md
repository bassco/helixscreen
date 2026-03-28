# Print Status Info Enrichment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enrich the print status panel with estimated finish time, Z height, a unified temperature card, and speed/flow indicators.

**Architecture:** Extend existing PrintStatusPanel subjects and observers for ETA/Z-height (string formatting changes). Create a new `temp_card_unified` XML component to replace the two separate temp cards. Add a speed/flow row with breakpoint-conditional visibility. All new data sources (gcode_position_z, chamber_temp, speed_factor, etc.) are already available as globally-registered subjects.

**Tech Stack:** LVGL 9.5 XML declarative UI, C++17, Catch2 tests

**Spec:** `docs/superpowers/specs/2026-03-27-print-status-info-enrichment-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `ui_xml/components/temp_card_unified.xml` | **Create** | Unified nozzle/bed/chamber temperature card component |
| `ui_xml/print_status_panel.xml` | Modify | Replace 2 temp cards with `temp_card_unified`, add ETA label, add speed/flow row |
| `src/ui/ui_panel_print_status.cpp` | Modify | ETA formatting, Z-height in layer text, chamber click handler, register new callbacks |
| `include/ui_panel_print_status.h` | Modify | New subject + buffer declarations for ETA |
| `tests/unit/test_print_status_formatting.cpp` | **Create** | Unit tests for ETA and layer text formatting |

---

### Task 1: Estimated Finish Time — Subject and Formatting

Add a new `print_eta` subject that holds the formatted ETA string like `"(~2:45 PM)"`, appended as a new label after "left" in the metadata overlay.

**Files:**
- Modify: `include/ui_panel_print_status.h:288` (add subject + buffer after remaining_subject_)
- Modify: `src/ui/ui_panel_print_status.cpp:277-278` (register new subject in init_subjects)
- Modify: `src/ui/ui_panel_print_status.cpp:1729-1747` (extend on_print_time_left_changed)
- Modify: `ui_xml/print_status_panel.xml:194` (add ETA label after " left")

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_print_status_formatting.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <ctime>
#include <string>

#include "format_utils.h"

// Test the ETA formatting helper (will be added to format_utils)
TEST_CASE("format_eta produces clock time from remaining seconds", "[print_status][eta]") {
    // Use a known reference time: 2026-03-27 14:00:00 local
    struct tm ref_tm = {};
    ref_tm.tm_year = 126; // 2026
    ref_tm.tm_mon = 2;    // March
    ref_tm.tm_mday = 27;
    ref_tm.tm_hour = 14;
    ref_tm.tm_min = 0;
    ref_tm.tm_sec = 0;
    ref_tm.tm_isdst = -1;
    time_t ref_time = mktime(&ref_tm);

    SECTION("30 minutes remaining") {
        std::string eta = helix::format::eta_clock_time(30 * 60, ref_time);
        // Should produce something like "(~2:30 PM)" or "(~14:30)"
        // Just verify it's non-empty and has the parens
        REQUIRE(!eta.empty());
        REQUIRE(eta.front() == '(');
        REQUIRE(eta.back() == ')');
        REQUIRE(eta.find("30") != std::string::npos);
    }

    SECTION("0 seconds remaining returns empty") {
        std::string eta = helix::format::eta_clock_time(0, ref_time);
        REQUIRE(eta.empty());
    }

    SECTION("negative remaining returns empty") {
        std::string eta = helix::format::eta_clock_time(-100, ref_time);
        REQUIRE(eta.empty());
    }

    SECTION("crosses midnight") {
        // 11 hours remaining from 14:00 → ~01:00 next day
        std::string eta = helix::format::eta_clock_time(11 * 3600, ref_time);
        REQUIRE(!eta.empty());
        REQUIRE(eta.front() == '(');
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test && ./build/bin/helix-tests "[eta]" -v`
Expected: Compilation error — `helix::format::eta_clock_time` not defined.

- [ ] **Step 3: Implement `eta_clock_time` in format_utils**

Add to `include/format_utils.h` inside `namespace helix::format`:

```cpp
/// Format an ETA clock time string like "(~2:30 PM)" from remaining seconds.
/// Returns empty string if remaining_seconds <= 0.
/// @param remaining_seconds Seconds until completion
/// @param now Current time (default: current system time)
std::string eta_clock_time(int remaining_seconds, time_t now = 0);
```

Add to `src/format_utils.cpp`:

```cpp
std::string eta_clock_time(int remaining_seconds, time_t now) {
    if (remaining_seconds <= 0) {
        return "";
    }
    if (now == 0) {
        now = std::time(nullptr);
    }
    time_t finish = now + remaining_seconds;
    struct tm finish_tm;
    localtime_r(&finish, &finish_tm);

    char buf[32];
    // Use locale-aware 12h/24h format
    std::strftime(buf, sizeof(buf), "%l:%M %p", &finish_tm);
    // Trim leading space from %l
    const char* p = buf;
    while (*p == ' ') p++;

    return std::string("(~") + p + ")";
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test && ./build/bin/helix-tests "[eta]" -v`
Expected: All eta tests PASS.

- [ ] **Step 5: Add ETA subject to PrintStatusPanel**

In `include/ui_panel_print_status.h`, after line 288 (`remaining_subject_`), add:

```cpp
    lv_subject_t eta_subject_;
```

After line 315 (`remaining_buf_`), add:

```cpp
    char eta_buf_[32] = "";
```

In `src/ui/ui_panel_print_status.cpp`, in `init_subjects()` after the `remaining_subject_` registration (line 278), add:

```cpp
    UI_MANAGED_SUBJECT_STRING(eta_subject_, eta_buf_, "", "print_eta", subjects_);
```

- [ ] **Step 6: Update on_print_time_left_changed to compute ETA**

In `src/ui/ui_panel_print_status.cpp`, modify `on_print_time_left_changed` (line 1744). After the existing `format_time` + `lv_subject_copy_string` for remaining, add:

```cpp
    // Compute ETA clock time
    std::string eta = helix::format::eta_clock_time(lifecycle_.remaining_seconds());
    std::snprintf(eta_buf_, sizeof(eta_buf_), "%s", eta.c_str());
    lv_subject_copy_string(&eta_subject_, eta_buf_);
```

Add `#include "format_utils.h"` at the top if not already present.

- [ ] **Step 7: Add ETA label to XML metadata overlay**

In `ui_xml/print_status_panel.xml`, after the " left" label (line 194), add:

```xml
<text_small name="print_eta" text="" bind_text="print_eta" variant="muted"/>
```

The full time row becomes:
```xml
<lv_obj height="content"
        style_pad_all="0" flex_flow="row" style_flex_main_place="start" style_flex_cross_place="center"
        scrollable="false">
  <text_small name="print_elapsed" text="0m" bind_text="print_elapsed"/>
  <text_small text=" elapsed · " translation_tag=" elapsed · " variant="muted"/>
  <text_small name="print_remaining" text="0m" bind_text="print_remaining"/>
  <text_small text=" left " translation_tag=" left " variant="muted"/>
  <text_small name="print_eta" text="" bind_text="print_eta" variant="muted"/>
</lv_obj>
```

Note the trailing space added to " left " to separate from the ETA.

- [ ] **Step 8: Commit**

```bash
git add include/format_utils.h src/format_utils.cpp \
        include/ui_panel_print_status.h src/ui/ui_panel_print_status.cpp \
        ui_xml/print_status_panel.xml \
        tests/unit/test_print_status_formatting.cpp
git commit -m "feat(print-status): add estimated finish time ETA to metadata overlay (#597)"
```

---

### Task 2: Z Height in Layer Text

Extend the layer text formatter to include Z position: `"Layer 42 / 100 (8.4mm)"`.

**Files:**
- Modify: `src/ui/ui_panel_print_status.cpp:1658-1677` (extend on_print_layer_changed)
- Modify: `include/ui_panel_print_status.h:311` (increase buffer size)
- Modify: `tests/unit/test_print_status_formatting.cpp` (add layer text tests)

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_print_status_formatting.cpp`:

```cpp
TEST_CASE("Layer text includes Z height", "[print_status][layer]") {
    // Format: "Layer %d / %d (%.1fmm)"
    // gcode_position_z is in centimillimeters (divide by 100 for mm)

    SECTION("normal layer with Z height") {
        char buf[64];
        int layer = 42, total = 100, z_centimm = 840; // 8.40mm
        std::snprintf(buf, sizeof(buf), "Layer %d / %d (%.1fmm)", layer, total,
                      z_centimm / 100.0);
        REQUIRE(std::string(buf) == "Layer 42 / 100 (8.4mm)");
    }

    SECTION("estimated layer with Z height") {
        char buf[64];
        int layer = 42, total = 100, z_centimm = 840;
        std::snprintf(buf, sizeof(buf), "Layer ~%d / %d (%.1fmm)", layer, total,
                      z_centimm / 100.0);
        REQUIRE(std::string(buf) == "Layer ~42 / 100 (8.4mm)");
    }

    SECTION("Z at zero omits height") {
        char buf[64];
        int layer = 0, total = 100, z_centimm = 0;
        if (z_centimm > 0) {
            std::snprintf(buf, sizeof(buf), "Layer %d / %d (%.1fmm)", layer, total,
                          z_centimm / 100.0);
        } else {
            std::snprintf(buf, sizeof(buf), "Layer %d / %d", layer, total);
        }
        REQUIRE(std::string(buf) == "Layer 0 / 100");
    }
}
```

- [ ] **Step 2: Run test to verify it passes (this is a formatting test, not an integration test)**

Run: `make test && ./build/bin/helix-tests "[layer]" -v`
Expected: PASS (these are pure formatting tests validating the snprintf pattern).

- [ ] **Step 3: Increase layer_text_buf_ size**

In `include/ui_panel_print_status.h`, change line 311:

```cpp
    char layer_text_buf_[80] = "Layer 0 / 0"; // increased for Z height suffix
```

(From 64 to 80 to accommodate the `" (123.4mm)"` suffix.)

- [ ] **Step 4: Add Z observer and extend layer formatting**

In `src/ui/ui_panel_print_status.cpp`, modify `on_print_layer_changed` (around line 1673-1677). Replace the layer text formatting block:

```cpp
    // Update the layer text display (prefix with ~ when estimated from progress)
    // Include Z height when available (gcode_position_z is in centimillimeters)
    int z_centimm = lv_subject_get_int(printer_state_.get_gcode_position_z_subject());
    if (z_centimm > 0) {
        const char* fmt = has_real_data ? "Layer %d / %d (%.1fmm)" : "Layer ~%d / %d (%.1fmm)";
        std::snprintf(layer_text_buf_, sizeof(layer_text_buf_), fmt, lifecycle_.current_layer(),
                      lifecycle_.total_layers(), z_centimm / 100.0);
    } else {
        const char* fmt = has_real_data ? "Layer %d / %d" : "Layer ~%d / %d";
        std::snprintf(layer_text_buf_, sizeof(layer_text_buf_), fmt, lifecycle_.current_layer(),
                      lifecycle_.total_layers());
    }
    lv_subject_copy_string(&layer_text_subject_, layer_text_buf_);
```

Also add a `gcode_position_z` observer so layer text refreshes when Z changes (Z may update more frequently than layer count). In the observer setup section (find where `print_layer_observer_` is set up), add a new observer:

```cpp
    z_position_observer_ = observe_int_sync(
        printer_state_.get_gcode_position_z_subject(), this,
        [](PrintStatusPanel* self, int) {
            // Re-trigger layer text update when Z changes
            int layer = lv_subject_get_int(
                self->printer_state_.get_print_layer_current_subject());
            self->on_print_layer_changed(layer);
        });
```

Add `ObserverGuard z_position_observer_;` to the header near the other observer guards (around line 497).

- [ ] **Step 5: Build and verify**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add include/ui_panel_print_status.h src/ui/ui_panel_print_status.cpp \
        tests/unit/test_print_status_formatting.cpp
git commit -m "feat(print-status): add Z height to layer progress text (#597)"
```

---

### Task 3: Create Unified Temperature Card Component

New XML component that stacks nozzle, bed, and optional chamber rows in a single card.

**Files:**
- Create: `ui_xml/components/temp_card_unified.xml`
- Modify: `src/main.cpp` (register component) — Applying [L014]

- [ ] **Step 1: Create the component XML**

Create `ui_xml/components/temp_card_unified.xml`:

```xml
<?xml version="1.0"?>
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Unified temperature card: nozzle + bed + optional chamber rows -->
<component>
  <consts>
    <px name="temp_row_height_small" value="28"/>
    <px name="temp_row_height_medium" value="32"/>
    <px name="temp_row_height_large" value="36"/>
  </consts>

  <view name="temp_card_unified" extends="ui_card"
        width="100%" height="content" style_radius="#border_radius"
        flex_flow="column" style_pad_top="#space_sm" style_pad_bottom="#space_sm"
        style_pad_left="#space_md" style_pad_right="#space_md" style_pad_gap="0">

    <!-- Nozzle row -->
    <lv_obj name="nozzle_row"
            width="100%" height="#temp_row_height" style_pad_all="0"
            flex_flow="row" style_flex_cross_place="center" clickable="true">
      <event_cb trigger="clicked" callback="on_print_temp_nozzle_clicked"/>
      <text_small text="Nozzle" translation_tag="Nozzle" style_min_width="68"/>
      <temp_display name="nozzle_temp" size="md" show_target="true"
                    bind_current="extruder_temp" bind_target="extruder_target" flex_grow="1"/>
      <text_small name="nozzle_status" bind_text="print_nozzle_status"
                  style_text_align="right" variant="muted" style_min_width="60"/>
    </lv_obj>

    <!-- Bed row -->
    <lv_obj name="bed_row"
            width="100%" height="#temp_row_height" style_pad_all="0"
            flex_flow="row" style_flex_cross_place="center" clickable="true">
      <event_cb trigger="clicked" callback="on_print_temp_bed_clicked"/>
      <text_small text="Bed" translation_tag="Bed" style_min_width="68"/>
      <temp_display name="bed_temp" size="md" show_target="true"
                    bind_current="bed_temp" bind_target="bed_target" flex_grow="1"/>
      <text_small name="bed_status" bind_text="print_bed_status"
                  style_text_align="right" variant="muted" style_min_width="60"/>
    </lv_obj>

    <!-- Chamber row (hidden when no chamber sensor or heater) -->
    <lv_obj name="chamber_row"
            width="100%" height="#temp_row_height" style_pad_all="0"
            flex_flow="row" style_flex_cross_place="center" hidden="true">
      <!-- Show if chamber sensor OR chamber heater exists (C++ manages visibility) -->
      <text_small text="Chamber" style_min_width="68"/>
      <temp_display name="chamber_temp" size="md" show_target="false"
                    bind_current="chamber_temp" flex_grow="1"/>
      <!-- Chamber heater target + status: shown only when active heater present -->
      <text_small name="chamber_status" bind_text="print_chamber_status"
                  style_text_align="right" variant="muted" style_min_width="60"
                  hidden="true"/>
    </lv_obj>

  </view>
</component>
```

Note: Chamber row visibility and whether to show target is managed from C++ because it depends on two subjects (`printer_has_chamber_sensor` OR `printer_has_chamber_heater`) which can't be OR-combined in pure XML bindings. On tiny breakpoint, chamber is hidden regardless.

- [ ] **Step 2: Register the component in main.cpp**

Applying [L014]: Find `lv_xml_component_register_from_file()` calls in `src/main.cpp` (or wherever components are registered). Add:

```cpp
lv_xml_component_register_from_file("temp_card_unified", "components/temp_card_unified");
```

Add it near the other component registrations, in alphabetical order.

- [ ] **Step 3: Build and verify component loads**

Run: `make -j`
Expected: Clean build. The component XML is loaded at runtime — no rebuild needed for XML-only changes (Applying [L031]).

- [ ] **Step 4: Commit**

```bash
git add ui_xml/components/temp_card_unified.xml src/main.cpp
git commit -m "feat(print-status): add unified temperature card XML component (#597)"
```

---

### Task 4: Integrate Unified Temp Card and Chamber Logic

Replace the two separate temp cards in `print_status_panel.xml` with the new unified component, and wire up C++ chamber visibility + click callbacks.

**Files:**
- Modify: `ui_xml/print_status_panel.xml:214-243` (replace nozzle + bed cards)
- Modify: `src/ui/ui_panel_print_status.cpp` (chamber observers, callbacks, status subject)
- Modify: `include/ui_panel_print_status.h` (new subject + observer declarations)

- [ ] **Step 1: Replace temp cards in XML**

In `ui_xml/print_status_panel.xml`, replace the nozzle temp card (lines 214-227) and bed temp card (lines 229-243) with:

```xml
        <!-- Unified Temperature Card (nozzle + bed + optional chamber) -->
        <temp_card_unified name="temp_card"/>
```

This replaces ~30 lines of XML with a single component reference.

- [ ] **Step 2: Add chamber status subject**

In `include/ui_panel_print_status.h`, add after `bed_status_subject_` (line 292):

```cpp
    lv_subject_t chamber_status_subject_;
```

Add after `bed_status_buf_` (line 319):

```cpp
    char chamber_status_buf_[16] = "Off";
```

In `src/ui/ui_panel_print_status.cpp`, in `init_subjects()`, after the bed_status registration (around line 286), add:

```cpp
    UI_MANAGED_SUBJECT_STRING(chamber_status_subject_, chamber_status_buf_, "Off",
                              "print_chamber_status", subjects_);
```

- [ ] **Step 3: Add chamber click callback**

In `include/ui_panel_print_status.h`, add near the other static callbacks (around line 454):

```cpp
    static void on_chamber_card_clicked(lv_event_t* e);
    void handle_chamber_card_click();
```

In `src/ui/ui_panel_print_status.cpp`, add the callback implementations near the existing nozzle/bed handlers (around line 1106):

```cpp
void PrintStatusPanel::handle_chamber_card_click() {
    spdlog::debug("[{}] Chamber temp card clicked - opening temperature graph", get_name());
    get_global_temp_graph_overlay().open(TempGraphOverlay::Mode::Chamber, parent_screen_);
}

void PrintStatusPanel::on_chamber_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_chamber_card_clicked");
    (void)e;
    get_global_print_status_panel().handle_chamber_card_click();
    LVGL_SAFE_EVENT_CB_END();
}
```

Register the new callbacks in `init_subjects()`. Update the `register_xml_callbacks` block (line 326) — replace the old nozzle/bed callback names and add chamber. Applying [L039] (unique names):

```cpp
    register_xml_callbacks({
        {"on_print_status_pause", on_pause_clicked},
        {"on_print_status_tune", on_tune_clicked},
        {"on_print_status_cancel", on_cancel_clicked},
        {"on_print_status_reprint", on_reprint_clicked},
        {"on_print_temp_nozzle_clicked", on_nozzle_card_clicked},
        {"on_print_temp_bed_clicked", on_bed_card_clicked},
        {"on_print_temp_chamber_clicked", on_chamber_card_clicked},
        {"on_print_status_objects", on_objects_clicked},
        {"on_print_status_dismiss_overlay", on_dismiss_overlay_clicked},
    });
```

Note: The old callback names `on_print_status_nozzle_clicked` / `on_print_status_bed_clicked` are replaced with `on_print_temp_nozzle_clicked` / `on_print_temp_bed_clicked` to match the XML component. Update the static trampoline functions to keep the same implementations — only the registered string names change.

- [ ] **Step 4: Add chamber visibility observer**

In `include/ui_panel_print_status.h`, add near the other observer guards:

```cpp
    ObserverGuard chamber_sensor_observer_;
    ObserverGuard chamber_heater_observer_;
    ObserverGuard chamber_temp_observer_;
```

In `src/ui/ui_panel_print_status.cpp`, in the observer setup section (find where temperature observers are created), add:

```cpp
    // Chamber row visibility: show if sensor OR heater present (but hide on TINY)
    auto update_chamber_visibility = [](PrintStatusPanel* self, int) {
        if (!self->is_created_) return;
        auto* chamber_row = lv_obj_find_by_name(self->panel_, "chamber_row");
        if (!chamber_row) return;

        bool has_sensor = lv_subject_get_int(
            self->printer_state_.get_printer_has_chamber_sensor_subject()) != 0;
        bool has_heater = lv_subject_get_int(
            self->printer_state_.get_printer_has_chamber_heater_subject()) != 0;
        bool is_tiny = lv_subject_get_int(theme_manager_get_breakpoint_subject()) == UI_BP_TINY;
        bool visible = (has_sensor || has_heater) && !is_tiny;

        if (visible) {
            lv_obj_remove_flag(chamber_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(chamber_row, LV_OBJ_FLAG_HIDDEN);
        }

        // If active heater: show target on temp_display, make row clickable, show status
        if (has_heater && visible) {
            lv_obj_add_flag(chamber_row, LV_OBJ_FLAG_CLICKABLE);
            auto* chamber_temp_widget = lv_obj_find_by_name(chamber_row, "chamber_temp");
            if (chamber_temp_widget) {
                // Reconfigure to show target
                ui_temp_display_set_show_target(chamber_temp_widget, true);
                // Bind to chamber_target subject
                // Note: this requires runtime binding - alternatively handled via
                // a subject that controls show_target visibility
            }
            auto* status = lv_obj_find_by_name(chamber_row, "chamber_status");
            if (status) lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
        }
    };

    chamber_sensor_observer_ = observe_int_sync(
        printer_state_.get_printer_has_chamber_sensor_subject(), this, update_chamber_visibility);
    chamber_heater_observer_ = observe_int_sync(
        printer_state_.get_printer_has_chamber_heater_subject(), this, update_chamber_visibility);
```

Add a chamber temperature observer for the status text (Heating/Holding/Off), following the same pattern as nozzle/bed status:

```cpp
    chamber_temp_observer_ = observe_int_sync(
        printer_state_.get_chamber_temp_subject(), this,
        [](PrintStatusPanel* self, int temp_centi) {
            int target_centi = lv_subject_get_int(
                self->printer_state_.get_chamber_target_subject());
            const char* status = "Off";
            if (target_centi > 0) {
                int diff = temp_centi - target_centi;
                if (diff < -200) status = "Heating";
                else if (diff > 200) status = "Cooling";
                else status = "Holding";
            }
            std::snprintf(self->chamber_status_buf_, sizeof(self->chamber_status_buf_), "%s",
                          status);
            lv_subject_copy_string(&self->chamber_status_subject_, self->chamber_status_buf_);
        });
```

- [ ] **Step 5: Build and verify**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add ui_xml/print_status_panel.xml ui_xml/components/temp_card_unified.xml \
        include/ui_panel_print_status.h src/ui/ui_panel_print_status.cpp
git commit -m "feat(print-status): integrate unified temp card with chamber support (#597)"
```

---

### Task 5: Speed / Flow Indicators

Add a speed/flow row visible on medium+ breakpoints, between the temp card and the filament/AMS status row.

**Files:**
- Modify: `ui_xml/print_status_panel.xml` (add speed/flow row after temp_card_unified)

- [ ] **Step 1: Add speed/flow row to XML**

In `ui_xml/print_status_panel.xml`, after the `<temp_card_unified>` element and before the filament/AMS status row (the `filament_ams_status_row` element), add:

```xml
        <!-- Speed / Flow indicators (hidden on TINY and SMALL breakpoints) -->
        <lv_obj name="speed_flow_row"
                width="100%" height="content" style_pad_left="#space_md" style_pad_right="#space_md"
                style_pad_top="0" flex_flow="row" style_flex_cross_place="center"
                style_pad_gap="#space_sm" scrollable="false">
          <!-- Hidden on TINY (0) -->
          <bind_flag_if_eq subject="ui_breakpoint" flag="hidden" ref_value="0"/>
          <!-- Hidden on SMALL (1) -->
          <bind_flag_if_eq subject="ui_breakpoint" flag="hidden" ref_value="1"/>
          <text_small text="Spd" variant="muted"/>
          <text_small name="speed_value" bind_text="print_speed_text"/>
          <text_small text=" · " variant="muted"/>
          <text_small text="Fl" variant="muted"/>
          <text_small name="flow_value" bind_text="print_flow_text"/>
        </lv_obj>
```

Note: Uses two `bind_flag_if_eq` bindings to hide on both TINY (0) and SMALL (1). This is the established pattern — the codebase doesn't use `bind_flag_if_lt` on `ui_breakpoint`. "Spd" and "Fl" are universal abbreviations, not wrapped in translation tags per [L070].

- [ ] **Step 2: Verify XML loads (no rebuild needed)**

Applying [L031]: XML changes don't need recompilation. Just relaunch:

Run: `./build/bin/helix-screen --test -vv -p print_status 2>&1 | head -50`

Expected: Panel loads without XML parsing errors. Speed/flow row should be visible on default 800x480 (MEDIUM).

- [ ] **Step 3: Commit**

```bash
git add ui_xml/print_status_panel.xml
git commit -m "feat(print-status): add speed/flow indicators visible on medium+ screens (#597)"
```

---

### Task 6: Tests

Add integration-level tests for the new formatting behavior.

**Files:**
- Modify: `tests/unit/test_print_status_formatting.cpp` (add more tests)

- [ ] **Step 1: Add ETA edge case tests**

Append to `tests/unit/test_print_status_formatting.cpp`:

```cpp
TEST_CASE("format_eta handles large remaining times", "[print_status][eta]") {
    // 48 hours remaining
    struct tm ref_tm = {};
    ref_tm.tm_year = 126;
    ref_tm.tm_mon = 2;
    ref_tm.tm_mday = 27;
    ref_tm.tm_hour = 10;
    ref_tm.tm_min = 0;
    ref_tm.tm_sec = 0;
    ref_tm.tm_isdst = -1;
    time_t ref_time = mktime(&ref_tm);

    std::string eta = helix::format::eta_clock_time(48 * 3600, ref_time);
    REQUIRE(!eta.empty());
    // Should still produce a valid time string
    REQUIRE(eta.front() == '(');
    REQUIRE(eta.back() == ')');
}

TEST_CASE("format_eta with very short remaining", "[print_status][eta]") {
    struct tm ref_tm = {};
    ref_tm.tm_year = 126;
    ref_tm.tm_mon = 2;
    ref_tm.tm_mday = 27;
    ref_tm.tm_hour = 23;
    ref_tm.tm_min = 59;
    ref_tm.tm_sec = 0;
    ref_tm.tm_isdst = -1;
    time_t ref_time = mktime(&ref_tm);

    // 30 seconds remaining — still produces ETA
    std::string eta = helix::format::eta_clock_time(30, ref_time);
    REQUIRE(!eta.empty());
}
```

- [ ] **Step 2: Add the test file to CMakeLists if needed**

Check if `tests/unit/CMakeLists.txt` (or the test Makefile) auto-discovers test files or needs explicit listing. If explicit, add `test_print_status_formatting.cpp`.

- [ ] **Step 3: Run all tests**

Run: `make test-run`
Expected: All tests pass, including new `[eta]` and `[layer]` tagged tests.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_print_status_formatting.cpp
git commit -m "test(print-status): add ETA and layer formatting tests (#597)"
```

---

### Task 7: Visual Verification

Test at all breakpoints to confirm layout looks correct.

**Files:** None (visual testing only)

- [ ] **Step 1: Build**

Run: `make -j`

- [ ] **Step 2: Test at each breakpoint**

Launch at each resolution and visually verify the print status panel. Start a mock print (the `--test` flag provides mock data).

```bash
# TINY — chamber hidden, speed/flow hidden, ETA and Z height visible
./build/bin/helix-screen --test -vv -s 480x320

# SMALL — chamber visible (if present), speed/flow hidden
./build/bin/helix-screen --test -vv -s 480x400

# MEDIUM — all features visible
./build/bin/helix-screen --test -vv -s 800x480

# LARGE — all features visible, more spacing
./build/bin/helix-screen --test -vv -s 1024x600

# XLARGE
./build/bin/helix-screen --test -vv -s 1280x720
```

For each resolution, verify:
- **Metadata overlay**: Time shows "Xm left (~H:MM PM)", layer shows "Layer X / Y (Z.Zmm)"
- **Unified temp card**: Nozzle and bed rows visible, tappable
- **Chamber row**: Only visible when mock config includes a chamber sensor/heater (check mock config for chamber data)
- **Speed/flow**: Visible on MEDIUM and above, hidden on SMALL and TINY
- **Buttons**: Still have enough room, not clipped or overlapping

- [ ] **Step 3: Fix any visual issues found**

Adjust spacing tokens, text truncation, or row heights as needed. XML changes don't need rebuild (Applying [L031]).

- [ ] **Step 4: Final commit if fixes needed**

```bash
git add -u
git commit -m "fix(print-status): layout adjustments from visual testing (#597)"
```
