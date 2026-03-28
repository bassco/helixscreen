# Print Status Info Enrichment

**Issue:** prestonbrown/helixscreen#597
**Date:** 2026-03-27

## Summary

Enrich the print status panel with additional at-a-glance information: estimated finish time, Z height, speed/flow indicators, and chamber temperature. Consolidate nozzle + bed into a unified temperature card to reclaim vertical space across all breakpoints.

## Changes

### 1. Estimated Finish Time (all breakpoints)

Append absolute clock time to the existing remaining-time text in the metadata overlay.

**Before:** `1h 02m elapsed · 24m left`
**After:** `1h 02m elapsed · 24m left (~2:45 PM)`

- Calculated as `now() + print_time_left` (seconds subject already exists).
- Uses system locale for 12h/24h format.
- Updates on the same cadence as `print_remaining` (observer-driven, ~30s effective).
- New string subject `print_remaining_with_eta` replaces `print_remaining` binding in the metadata overlay. Format: `"24m left (~2:45 PM)"`. The C++ observer that currently formats `print_remaining` is extended to also compute and append the ETA.
- Parenthetical styling keeps it secondary to the relative time.

### 2. Z Height in Layer Text (all breakpoints)

Extend the layer progress text with the current Z position.

**Before:** `Layer 42 / 100`
**After:** `Layer 42 / 100 (8.4mm)`

- Source: `gcode_position_z` subject (centimillimeters, divide by 100 for mm display).
- Formatted to 1 decimal place.
- The existing `print_layer_text` subject string is extended in its C++ formatter to include the Z value. No new subject needed.

### 3. Unified Temperature Card

Replace the two separate nozzle/bed `ui_card` widgets with a single `temp_card_unified` XML component containing stacked rows.

**Layout (each row):**
```
Label    temp_display(size="md", show_target)    Status
```

**Rows:**
1. **Nozzle** — always shown. Tap → nozzle temp controls.
2. **Bed** — always shown. Tap → bed temp controls.
3. **Chamber** — conditional:
   - Hidden when no chamber sensor or heater (`printer_has_chamber_sensor == 0 AND printer_has_chamber_heater == 0`).
   - Passive sensor: show current temp only (`show_target="false"`), not tappable.
   - Active heater: show current/target (`show_target="true"`), tappable → chamber controls (same pattern as nozzle/bed).

**Detection:** Existing subjects `printer_has_chamber_sensor` and `printer_has_chamber_heater` drive visibility. `chamber_target` subject (stays 0 for passive sensors) drives whether the target column renders.

**Breakpoint behavior:**
- Tiny (≤390px): 2 rows only (nozzle + bed). Chamber hidden — not enough vertical space.
- Small+: All rows shown when applicable.

**Sizing:** Each row is a fixed-height flexbox. The card height is `content`-based (grows with rows). Uses `temp_display size="md"` (body font) instead of the current `size="lg"` (heading font). The visual density is higher but all information remains legible.

**Tappability:** Each row is a separate clickable `lv_obj` with its own `event_cb`. Applying [L039]: callbacks named `on_print_temp_nozzle_clicked`, `on_print_temp_bed_clicked`, `on_print_temp_chamber_clicked`.

**Vertical space savings:** Eliminates one `temp_card_height` + one `space_lg` gap. Savings per breakpoint:
| Breakpoint | Old (2 cards) | New (1 card) | Saved |
|------------|---------------|--------------|-------|
| Tiny | 96 + 12 = 108px | ~56px | ~52px |
| Small | 128 + 12 = 140px | ~76px | ~64px |
| Medium | 144 + 16 = 160px | ~84px | ~76px |
| Large | 160 + 20 = 180px | ~92px | ~88px |

### 4. Speed / Flow Indicators

New row in the right column controls section, between the temperature card and the filament/AMS status row.

**Format:** `Spd 100% · Fl 100%`

- Source: existing `speed_factor` and `flow_factor` subjects (integer 0-100%).
- Two `text_small` labels with muted separator.
- "Spd" and "Fl" are universal abbreviations; not wrapped in `lv_tr()` per [L070].

**Breakpoint behavior:**
| Breakpoint | Behavior |
|------------|----------|
| Tiny | Hidden |
| Small | Hidden (accessible via Tune) |
| Medium+ | Shown |

Visibility controlled by a `print_speed_flow_visible` subject set from C++ based on breakpoint, or simpler: just use the existing `ui_breakpoint` subject with `bind_flag_if_lt` to hide below medium.

### 5. Metadata Overlay Adjustments

The metadata overlay height tokens may need a small increase (~4-6px) to accommodate the finish time text without clipping. The ETA is appended to the existing remaining-time label, so it adds width not height — but longer strings may wrap on narrow screens.

**Mitigation:** On tiny/small, use abbreviated format: `(~2:45)` instead of `(~2:45 PM)`.

## Files Affected

| File | Change |
|------|--------|
| `ui_xml/print_status_panel.xml` | Replace 2 temp cards with `temp_card_unified`, add speed/flow row, update time binding |
| `ui_xml/components/temp_card_unified.xml` | **NEW** — unified temperature card component |
| `src/ui/ui_panel_print_status.cpp` | New subjects for ETA, extend layer text formatter, speed/flow visibility, chamber temp observers, register new callbacks |
| `include/ui_panel_print_status.h` | New subject declarations, chamber observer guards |
| `src/ui/ui_panel_print_status.cpp` | Register new XML event callbacks (unique names per [L039]) |
| `tests/unit/test_print_status_panel.cpp` | Test ETA formatting, layer text with Z, chamber visibility logic |

## Out of Scope

- **Additional non-chamber temperature sensors** (MCU, host, stepper drivers) — these are diagnostic, not useful during printing. Could be a future "sensors" overlay.
- **True volumetric flow (mm³/s)** — requires nozzle diameter config that isn't reliably available. Speed/flow percentages are sufficient.
- **Total object height from slicer** — metadata field `object_height` is inconsistently provided by slicers. Current Z position is always available and more useful (shows where you actually are).

## Testing

- Unit tests for ETA string formatting (edge cases: unknown time left, midnight rollover, 12h vs 24h).
- Unit tests for layer text with Z position formatting.
- Visual verification at all 5 breakpoints using `--test -s WxH` for: 480x320, 480x400, 800x480, 1024x600, 1280x720.
- Verify chamber row visibility with mock configs: no chamber, passive sensor, active heater.
- Verify speed/flow row hidden on small/tiny, shown on medium+.
