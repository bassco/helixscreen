# Fine/Coarse Jog Toggle

**Date:** 2026-03-24
**Issue:** prestonbrown/helixscreen#505
**Status:** Design

## Problem

The motion panel jog pad has two rings (inner/outer) hardcoded to 1mm/10mm. Users need sub-millimeter steps for precision work (probe dock positioning, Z-offset tuning) and the existing 4-distance enum (`Dist0_1mm`, `Dist1mm`, `Dist10mm`, `Dist100mm`) is inaccessible from the UI. Z buttons are similarly hardcoded to ±1mm and ±10mm.

## Design

Add a two-segment toggle button (`Fine | Coarse`) below the jog pad. Switching the toggle updates jog pad ring distances, jog pad ring labels, and Z button distances/labels together.

### Distance Mapping

| Mode | Jog Inner Ring | Jog Outer Ring | Z Small Buttons | Z Large Buttons |
|------|---------------|----------------|-----------------|-----------------|
| **Fine** | 0.1mm | 1mm | ±0.1mm | ±1mm |
| **Coarse** (default) | 1mm | 10mm | ±1mm | ±10mm |

Default is **Coarse** to preserve current behavior for existing users.

### UI Placement

The toggle lives in the left column, directly below the jog pad container. It uses a `ui_button_group` segmented control with two options: "Fine" and "Coarse".

### Changes Required

#### 1. Motion Panel XML (`ui_xml/motion_panel.xml`)

- Add a `ui_button_group` below `jog_pad_container` with two segments
- Add an `event_cb` on the button group for `value_changed` → `on_jog_mode_changed`
- Change Z button text labels from hardcoded strings to `bind_text` on subjects (`motion_z_large_label`, `motion_z_small_label`)

#### 2. Jog Pad Ring Labels (`src/ui/ui_jog_pad.cpp`)

- Ring label strings (currently hardcoded `"1mm"` and `"10mm"` in the `LV_EVENT_DRAW_POST` handler) become dynamic based on `state->current_distance`
- When `Dist0_1mm`: inner label = "0.1", outer label = "1"
- When `Dist1mm`: inner label = "1", outer label = "10"
- The zone distance logic (lines 580-606) already handles this correctly — inner zone uses `distance_values[current_distance]` when `≤ Dist1mm`, outer uses `distance_values[current_distance]` when `≥ Dist10mm`

#### 3. Motion Panel C++ (`src/ui/ui_panel_motion.cpp` / `.h`)

- Add `on_jog_mode_changed` event callback, registered via `lv_xml_register_event_cb`
- When toggled to Fine: call `ui_jog_pad_set_distance(jog_pad_, JogDistance::Dist0_1mm)`, update Z label subjects
- When toggled to Coarse: call `ui_jog_pad_set_distance(jog_pad_, JogDistance::Dist1mm)`, update Z label subjects
- Add two new subjects for Z button labels: `motion_z_large_label` and `motion_z_small_label`
- Update `handle_z_button()` to read distance from `current_distance_` mode instead of parsing from button name

#### 4. Persistence

- Save preference via `SettingsManager` under key `motion.jog_mode` (string: `"fine"` or `"coarse"`)
- Load on panel creation; default to `"coarse"`

### What Does NOT Change

- Jog pad geometry (ring zones at 25%/60%, home button)
- Z button count and layout (4 buttons, 2 up + 2 down)
- Feedrates (6000 mm/min XY, 600 mm/min Z)
- Bed-moves inversion logic
- Position display formatting
- QGL/Z-Tilt leveling buttons

## Key Files

| File | Change |
|------|--------|
| `ui_xml/motion_panel.xml` | Add toggle widget, bind Z labels to subjects |
| `include/ui_panel_motion.h` | Add Z label subjects, jog mode persistence |
| `src/ui/ui_panel_motion.cpp` | Toggle callback, Z distance from mode, persistence |
| `src/ui/ui_jog_pad.cpp` | Dynamic ring label strings in draw handler |
