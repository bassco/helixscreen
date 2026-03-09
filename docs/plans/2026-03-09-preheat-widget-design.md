# Preheat Home Panel Widget Design

**Date:** 2026-03-09
**Status:** Approved

## Overview

A 2×1 home panel widget combining a `ui_split_button` for quick material preheating with live nozzle/bed temperature displays.

## Visual Layout

```
┌──────────────────────────────────────────────────┐
│  🔥 Preheat PLA (210/60) ▼  │  🌡 210°  🛏 60° │
└──────────────────────────────────────────────────┘
 ← split button (flex_grow=1) → ← temp column →
```

**Left half:** `ui_split_button` with `icon="heat_wave"`, variant `primary`. Label shows material name + temps (e.g., "Preheat PLA (210/60)"). Dropdown lists available materials (PLA, PETG, ABS, TPU, plus spool material if assigned). Main click applies the selected preset temps to nozzle + bed. Dropdown selection updates the label but does NOT apply — user must tap main button.

**Right half:** Vertical column with nozzle and bed `<temp_display>` components (size="xs", show_target="false"). Each row clickable — opens `TempGraphOverlay` in Nozzle or Bed mode.

## Behaviors

- **Persist selection:** Last selected material saved to `SettingsManager` key `preheat_widget_material` (int index, default 0=PLA).
- **Temperature subjects:** Use centidegrees (int) per project convention (L021).
- **Label update:** C++ formats label as `"Preheat {MATERIAL} ({nozzle}/{bed})"` from filament database temps.
- **Options:** Built from `PRESET_MATERIAL_NAMES[]` (PLA, PETG, ABS, TPU). Dynamic spool material not included in v1 (can add later).
- **Disabled state:** `bind_state_if_not_eq subject="printer_connection_state" state="disabled" ref_value="2"` — same as other widgets.
- **Widget registry:** disabled by default (opt-in from widget picker).

## Widget Registration

```cpp
{"preheat", "Preheat", "heat_wave", "Quick preheat with material selection",
 "Preheat", nullptr, nullptr, false, 2, 1, 2, 1, 3, 1}
```

## Files

| File | Action |
|------|--------|
| `ui_xml/components/panel_widget_preheat.xml` | Create |
| `src/ui/panel_widgets/preheat_widget.cpp` | Create |
| `include/preheat_widget.h` | Create |
| `src/ui/panel_widget_registry.cpp` | Edit (add def + forward decl) |
