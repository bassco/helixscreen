# Spool Management Enhancements

**Issues:** prestonbrown/helixscreen#629, prestonbrown/helixscreen#630
**Date:** 2026-03-29

## Overview

Two enhancements to the AMS spool management screen, both surfaced by an AFC user:

1. **Show remaining filament in grams** — the edit modal currently shows percentage only; users need grams to decide if a spool has enough for the next print.
2. **Tool-to-spool remapping** — allow reassigning tools (T0, T1...) to slots from the edit modal.

## Feature 1: Remaining Filament in Grams (#629)

### Scope

Changes are limited to the **AMS edit modal** (`ams_edit_modal.xml` + `ui_ams_edit_modal.cpp`). Slot cards remain compact (no grams display).

### Current Behavior

- "Remaining" label shows percentage only (e.g., "75%")
- View mode: progress bar
- Edit mode: slider 0-100%
- When `total_weight_g` is unknown, a synthetic 1000g is used internally

### New Behavior

- **View mode:** Label changes from "75%" to "750g / 1000g (75%)"
- **Edit mode:** Slider still controls percentage. A live-updating label shows calculated grams as the slider moves (e.g., "750g remaining")
- **Fallback:** When `total_weight_g` is unknown (-1 or 0), show percentage only — no misleading gram values from synthetic weights

### Data Sources

- `SlotInfo::remaining_weight_g` and `SlotInfo::total_weight_g` — already populated by all AMS backends
- Calculation: `grams = slider_pct * total_weight_g / 100.0`

## Feature 2: Tool Remapping (#630)

### Scope

- **AMS edit modal:** Add a "Tool" dropdown row
- **AMS slot view:** Badge color change for overridden mappings
- **AmsState / backend:** Persist override

### New UI Element: Tool Dropdown

Add a "Tool" row to the AMS edit modal form section, alongside existing fields (vendor, material, color, temperature):

- **Widget:** LVGL dropdown
- **Options:** "None", "T0", "T1", ... "T{n-1}" where n = printer's tool count
- **Default selection:** Current `SlotInfo::mapped_tool` (-1 maps to "None")
- **Duplicates allowed:** Multiple slots can map to the same tool (endless spool scenarios)

### Slot View Badge Styling

The existing tool badge on `ams_slot_view.xml` already shows "T0", "T1", etc. Add visual distinction for overrides:

- **Default mapping (from firmware):** Current badge style (unchanged)
- **User override:** Warning color badge background

Implementation note (per L040): use two `bind_style` variants (default vs override) with no inline style attributes on the badge color properties, so the reactive style change works correctly.

### Data Flow

1. User opens AMS edit modal for a slot
2. Dropdown pre-selects current tool mapping
3. User changes tool selection
4. On save:
   - `working_info_.mapped_tool` is updated
   - Override is persisted through the existing slot save path
   - `AmsState` notifies observers
5. Slot view badge updates text and style via subject observation

### Override Tracking

`SlotInfo` needs a way to distinguish firmware mappings from user overrides. Options:

- Add `bool tool_mapping_override` to `SlotInfo`
- Or derive it: if current mapping differs from firmware's `tool_to_slot_map`, it's an override

Prefer the explicit boolean — it survives firmware map changes and is cheaper to check.

### Backend Considerations

- **AFC / Happy Hare:** May support native tool remap commands (e.g., `SET_TOOL_MAP`). If available, send the command; otherwise, store as a UI-only override that's applied when building print mappings.
- **ACE / CFS / Tool Changer:** Check backend capabilities. If no native remap, UI-only override works the same way.
- The `FilamentMapper` already checks `SlotInfo::mapped_tool` when computing mappings, so UI-only overrides will be respected during print setup.

## Files to Modify

| File | Changes |
|------|---------|
| `ui_xml/ams_edit_modal.xml` | Add gram labels (view + edit), add tool dropdown row |
| `src/ui/ui_ams_edit_modal.cpp/h` | Gram calculation display, tool dropdown population + save logic |
| `ui_xml/ams_slot_view.xml` | Add bind_style for override badge color |
| `src/ui/ui_ams_slot.cpp` | Expose override flag as subject for badge styling |
| `include/ams_types.h` | Add `tool_mapping_override` bool to `SlotInfo` |

## Out of Scope

- Grams display on slot cards (kept compact)
- Editing remaining filament in grams directly (slider stays percentage-based)
- Drag-and-drop remapping in the filament mapping modal
- Spoolman panel changes (already shows grams)
