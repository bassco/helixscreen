# Spool Management Enhancements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show remaining filament in grams in the AMS edit modal (#629) and allow tool-to-slot remapping via dropdown (#630).

**Architecture:** Both features modify the AMS edit modal (XML + C++) and the data model (`SlotInfo`). The tool dropdown follows the existing vendor/material dropdown pattern. The slot view badge gets a warning-color style for user overrides.

**Tech Stack:** LVGL 9.5, C++17, XML declarative UI, subject-based data binding

---

### Task 1: Add gram display to remaining filament label (edit modal)

**Files:**
- Modify: `ui_xml/ams_edit_modal.xml:109` (remaining_pct_label)
- Modify: `src/ui/ui_ams_edit_modal.h:116,126` (subject + buffer)
- Modify: `src/ui/ui_ams_edit_modal.cpp:314,945-956,1129-1148` (formatting logic)

This task changes the remaining filament label from showing "75%" to "750g / 1000g (75%)" in view mode, and adds a live gram readout below the slider in edit mode.

- [ ] **Step 1: Add gram label to XML**

In `ui_xml/ams_edit_modal.xml`, add a second label for grams next to the percentage label. The percentage label stays as-is for subject binding; add a new `remaining_grams_label` that will show the gram value.

After line 109 (`<text_body name="remaining_pct_label" text="100%"/>`), the label will be replaced with a combined format. Since the percentage label is already subject-bound, change its format to include grams.

No XML structural changes needed — we'll change the format string written to `remaining_pct_subject_` to include grams.

- [ ] **Step 2: Widen the remaining_pct_buf_ buffer**

In `src/ui/ui_ams_edit_modal.h`, change:

```cpp
// OLD (line 126):
char remaining_pct_buf_[16] = {0};

// NEW:
char remaining_pct_buf_[48] = {0};  // "1000g / 1000g (100%)" needs ~22 chars
```

- [ ] **Step 3: Update update_ui() to format grams + percentage**

In `src/ui/ui_ams_edit_modal.cpp`, replace the percentage-only formatting in `update_ui()` (lines 945-956):

```cpp
// OLD:
int remaining_pct =
    static_cast<int>(100.0f * working_info_.remaining_weight_g / working_info_.total_weight_g);
remaining_pct = std::max(0, std::min(100, remaining_pct));

lv_obj_t* remaining_slider = find_widget("remaining_slider");
if (remaining_slider) {
    lv_slider_set_value(remaining_slider, remaining_pct, LV_ANIM_OFF);
}

// Update remaining percentage label via subject
helix::format::format_percent(remaining_pct, remaining_pct_buf_, sizeof(remaining_pct_buf_));
lv_subject_copy_string(&remaining_pct_subject_, remaining_pct_buf_);

// NEW:
int remaining_pct =
    static_cast<int>(100.0f * working_info_.remaining_weight_g / working_info_.total_weight_g);
remaining_pct = std::max(0, std::min(100, remaining_pct));

lv_obj_t* remaining_slider = find_widget("remaining_slider");
if (remaining_slider) {
    lv_slider_set_value(remaining_slider, remaining_pct, LV_ANIM_OFF);
}

// Update remaining label: "750g / 1000g (75%)" or "75%" if no weight data
format_remaining_label(remaining_pct);
```

- [ ] **Step 4: Add format_remaining_label() helper**

Add a private method to `AmsEditModal` that formats the label with or without grams:

In `src/ui/ui_ams_edit_modal.h`, add to private methods (after line 161):

```cpp
void format_remaining_label(int pct);
```

In `src/ui/ui_ams_edit_modal.cpp`, add the implementation:

```cpp
void AmsEditModal::format_remaining_label(int pct) {
    // Show grams only when we have real weight data (not synthetic 1000g)
    if (original_info_.total_weight_g > 0) {
        int remaining_g = static_cast<int>(working_info_.remaining_weight_g);
        int total_g = static_cast<int>(working_info_.total_weight_g);
        snprintf(remaining_pct_buf_, sizeof(remaining_pct_buf_), "%dg / %dg (%d%%)",
                 remaining_g, total_g, pct);
    } else {
        helix::format::format_percent(pct, remaining_pct_buf_, sizeof(remaining_pct_buf_));
    }
    lv_subject_copy_string(&remaining_pct_subject_, remaining_pct_buf_);
}
```

Key: check `original_info_.total_weight_g` (not `working_info_`) to detect real vs synthetic weight, since `update_ui()` sets synthetic 1000g on `working_info_` before this runs.

- [ ] **Step 5: Update handle_remaining_changed() to show grams during slider drag**

In `src/ui/ui_ams_edit_modal.cpp`, replace lines 1134-1136 in `handle_remaining_changed()`:

```cpp
// OLD:
helix::format::format_percent(percent, remaining_pct_buf_, sizeof(remaining_pct_buf_));
lv_subject_copy_string(&remaining_pct_subject_, remaining_pct_buf_);

// NEW:
format_remaining_label(percent);
```

- [ ] **Step 6: Build and verify**

```bash
make -j
```

Expected: Clean build, no errors.

- [ ] **Step 7: Commit**

```bash
git add src/ui/ui_ams_edit_modal.cpp src/ui/ui_ams_edit_modal.h
git commit -m "feat(ams): show remaining filament in grams in edit modal (prestonbrown/helixscreen#629)"
```

Note: No XML changes needed — the label format change is purely in C++ formatting strings.

---

### Task 2: Add tool dropdown to AMS edit modal

**Files:**
- Modify: `ui_xml/ams_edit_modal.xml:149-161` (add tool row after temperature display)
- Modify: `src/ui/ui_ams_edit_modal.h` (add tool state + handler)
- Modify: `src/ui/ui_ams_edit_modal.cpp` (populate dropdown, handle changes, save)

- [ ] **Step 1: Add tool dropdown row to XML**

In `ui_xml/ams_edit_modal.xml`, after the temperature display row (after line 161, before `</lv_obj>` closing the right column), add a tool assignment row:

```xml
          <!-- Tool assignment dropdown -->
          <lv_obj width="100%"
                  height="content" style_pad_all="0" flex_flow="row" style_flex_main_place="start"
                  style_flex_cross_place="center" style_pad_gap="#space_sm" scrollable="false">
            <text_small text="Tool" translation_tag="Tool"/>
            <lv_dropdown name="tool_dropdown"
                         width="#dropdown_width" height="content" style_radius="#border_radius"
                         style_border_width="0" options="None">
              <event_cb trigger="value_changed" callback="ams_edit_tool_changed_cb"/>
            </lv_dropdown>
          </lv_obj>
```

- [ ] **Step 2: Register the tool_changed callback**

In `src/ui/ui_ams_edit_modal.cpp`, find `register_callbacks()` and add:

```cpp
lv_xml_register_event_cb("ams_edit_tool_changed_cb", on_tool_changed_cb);
```

- [ ] **Step 3: Add handler declarations to header**

In `src/ui/ui_ams_edit_modal.h`, add to the event handlers section (after line 188):

```cpp
void handle_tool_changed(int index);
```

Add to the static callbacks section (after line 218):

```cpp
static void on_tool_changed_cb(lv_event_t* e);
```

- [ ] **Step 4: Implement the static callback**

In `src/ui/ui_ams_edit_modal.cpp`, add with the other static callbacks:

```cpp
void AmsEditModal::on_tool_changed_cb(lv_event_t* e) {
    if (!s_active_instance_ || !s_active_instance_->dialog_) return;
    lv_obj_t* dropdown = lv_event_get_target_obj(e);
    int selected = lv_dropdown_get_selected(dropdown);
    s_active_instance_->handle_tool_changed(selected);
}
```

- [ ] **Step 5: Implement handle_tool_changed()**

```cpp
void AmsEditModal::handle_tool_changed(int index) {
    // Index 0 = "None" (-1), index 1 = T0 (0), index 2 = T1 (1), etc.
    int new_tool = index - 1;
    working_info_.mapped_tool = new_tool;
    working_info_.tool_mapping_override = (new_tool != original_info_.mapped_tool);
    spdlog::debug("[AmsEditModal] Tool changed to: {} (override={})",
                  new_tool, working_info_.tool_mapping_override);
    update_sync_button_state();
}
```

- [ ] **Step 6: Populate tool dropdown in update_ui()**

In `src/ui/ui_ams_edit_modal.cpp`, add tool dropdown population in `update_ui()`, after the temperature display update (after `update_temp_display()` call around line 966):

```cpp
    // Populate tool dropdown with available tools
    lv_obj_t* tool_dropdown = find_widget("tool_dropdown");
    if (tool_dropdown) {
        int tool_count = ToolState::instance().tool_count();
        std::string tool_options = "None";
        for (int i = 0; i < tool_count; i++) {
            tool_options += '\n';
            tool_options += "T" + std::to_string(i);
        }
        lv_dropdown_set_options(tool_dropdown, tool_options.c_str());

        // Set initial selection: mapped_tool -1 → index 0 (None), T0 → index 1, etc.
        int tool_idx = working_info_.mapped_tool + 1;
        tool_idx = std::max(0, std::min(tool_idx, tool_count));
        lv_dropdown_set_selected(tool_dropdown, tool_idx);
    }
```

Add include at top of file if not already present:

```cpp
#include "tool_state.h"
```

- [ ] **Step 7: Build and verify**

```bash
make -j
```

Expected: Clean build, no errors.

- [ ] **Step 8: Commit**

```bash
git add ui_xml/ams_edit_modal.xml src/ui/ui_ams_edit_modal.cpp src/ui/ui_ams_edit_modal.h
git commit -m "feat(ams): add tool dropdown to edit modal (prestonbrown/helixscreen#630)"
```

---

### Task 3: Add tool_mapping_override flag to SlotInfo

**Files:**
- Modify: `include/ams_types.h:635` (add override bool to SlotInfo)

- [ ] **Step 1: Add the flag**

In `include/ams_types.h`, after line 635 (`int mapped_tool = -1;`), add:

```cpp
    bool tool_mapping_override = false; ///< True if user manually remapped this slot's tool
```

- [ ] **Step 2: Build and verify**

```bash
make -j
```

Expected: Clean build. The flag is a simple bool with default value, so no other code needs updating.

- [ ] **Step 3: Commit**

```bash
git add include/ams_types.h
git commit -m "feat(ams): add tool_mapping_override flag to SlotInfo"
```

---

### Task 4: Warning-color tool badge for overrides on slot view

**Files:**
- Modify: `ui_xml/ams_slot_view.xml:90-114` (add bind_style for override badge)
- Modify: `src/ui/ui_ams_slot.cpp:464-495` (apply override styling)

- [ ] **Step 1: Update apply_tool_badge() to use warning color for overrides**

In `src/ui/ui_ams_slot.cpp`, modify `apply_tool_badge()` to accept the override flag and set badge color:

```cpp
// Change signature (line 464):
// OLD:
static void apply_tool_badge(AmsSlotData* data, int mapped_tool) {

// NEW:
static void apply_tool_badge(AmsSlotData* data, int mapped_tool, bool is_override) {
```

Then inside the `if (mapped_tool >= 0)` block (after line 481), replace the auto-contrast logic:

```cpp
    if (mapped_tool >= 0) {
        char tool_text[8];
        snprintf(tool_text, sizeof(tool_text), "T%d", mapped_tool);
        lv_label_set_text(data->tool_badge, tool_text);
        lv_obj_remove_flag(data->tool_badge_bg, LV_OBJ_FLAG_HIDDEN);

        // Use warning color for user overrides, muted for firmware defaults
        if (is_override) {
            lv_color_t warn_color = ui_theme_get_color("warning");
            lv_obj_set_style_bg_color(data->tool_badge_bg, warn_color, LV_PART_MAIN);
        } else {
            lv_color_t muted_color = ui_theme_get_color("text_muted");
            lv_obj_set_style_bg_color(data->tool_badge_bg, muted_color, LV_PART_MAIN);
        }

        // Auto-contrast text color based on badge background
        if (data->tool_badge) {
            lv_color_t bg = lv_obj_get_style_bg_color(data->tool_badge_bg, LV_PART_MAIN);
            lv_color_t text_color = theme_manager_get_contrast_color(bg);
            lv_obj_set_style_text_color(data->tool_badge, text_color, LV_PART_MAIN);
        }
        spdlog::trace("[AmsSlot] Slot {} tool badge: {} (override={})",
                      data->slot_index, tool_text, is_override);
    }
```

- [ ] **Step 2: Update all callers of apply_tool_badge()**

Search for all calls to `apply_tool_badge()` in `ui_ams_slot.cpp` and add the override parameter. There should be two call sites (around lines 857 and 1067):

```cpp
// At each call site, change:
// OLD:
apply_tool_badge(data, slot.mapped_tool);

// NEW:
apply_tool_badge(data, slot.mapped_tool, slot.tool_mapping_override);
```

- [ ] **Step 3: Remove inline style_bg_color from XML badge**

In `ui_xml/ams_slot_view.xml`, remove the inline `style_bg_color` from the tool badge since we now set it programmatically (per L040 — inline attrs override dynamic styles):

```xml
<!-- OLD (line 98): -->
style_bg_color="#text_muted"

<!-- Remove this attribute entirely from the tool_badge lv_obj -->
```

The background color will be set in `apply_tool_badge()` for both default and override cases.

- [ ] **Step 4: Build and verify**

```bash
make -j
```

Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_ams_slot.cpp ui_xml/ams_slot_view.xml
git commit -m "feat(ams): warning-color tool badge for user overrides"
```

---

### Task 5: Manual testing

**Files:** None (testing only)

- [ ] **Step 1: Launch the app**

```bash
./build/bin/helix-screen --test -vv -p ams 2>&1 | tee /tmp/ams-test.log
```

Use `run_in_background: true`.

- [ ] **Step 2: Test remaining grams display**

User should:
1. Tap a slot to open the edit modal
2. Verify the remaining label shows "Xg / Yg (Z%)" format (if the slot has weight data from Spoolman) or just "Z%" (if no weight data)
3. Tap the edit (tune) button on remaining
4. Drag the slider and verify the gram value updates live
5. Accept the change and verify the label updates

- [ ] **Step 3: Test tool dropdown**

User should:
1. In the same edit modal, find the "Tool" dropdown
2. Verify it shows "None", "T0", "T1", etc. based on printer's tool count
3. Change the tool assignment and save
4. Verify the slot view badge updates to show the new tool
5. If changed from the firmware default, verify the badge is warning-colored

- [ ] **Step 4: Read logs and verify**

Read `/tmp/ams-test.log` for any errors or unexpected behavior.
