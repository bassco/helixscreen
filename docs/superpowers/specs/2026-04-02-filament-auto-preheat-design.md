# Filament Panel Auto-Preheat for Load/Unload

**Date:** 2026-04-02
**Scope:** `ui_panel_filament.cpp` / `ui_panel_filament.h`

## Problem

The filament panel blocks load/unload with "Nozzle too cold" when the extruder is below `min_extrude_temp_`. The user must manually select a material preset, wait for heating, then retry. If a spool is already assigned (external spool or AMS slot), the required temperature is known — we should just heat to it automatically.

## Solution

When load/unload is pressed and the extruder is too cold, resolve the required temperature from the assigned spool/material, start heating, and execute the operation once the target is reached.

## Temperature Source Priority

1. **External spool** — `AmsState::get_external_spool_info()` → `build_active_material()` → `nozzle_min`
2. **AMS active slot** (especially for unload) — `backend->get_system_info().get_active_slot()` → `build_active_material()` → `nozzle_min`
3. **Selected material preset** — if a PLA/PETG/ABS/TPU button is active, use that preset's nozzle temp from `filament_database.h`
4. **Fallback** — `min_extrude_temp_` (Klipper's configured minimum, default 170°C)

## Flow

1. User taps Load or Unload
2. Existing guards run first (operation already in progress, filament sensor checks)
3. If `is_extrusion_allowed()` → proceed immediately (no change)
4. If too cold:
   a. Resolve target temp from priority list
   b. Snapshot `nozzle_target_` as `prior_nozzle_target_` (for cooldown decision)
   c. Send `api_->set_temperature()` for the nozzle
   d. Set `pending_preheat_op_` to LOAD or UNLOAD, store target in `pending_preheat_target_`
   e. Show notification: "Heating to {temp}°C for {material}..." or "Heating to {temp}°C..." if no material name available
5. On each temperature update (`update_all_temps()`), call `check_pending_preheat()`
6. When `nozzle_current_ >= (pending_preheat_target_ - 5)` (5°C threshold, matching AMS sidebar):
   a. Clear pending state
   b. Execute the original operation (`execute_load()` or `execute_unload()`)
7. After operation completes: if `prior_nozzle_target_` was 0, set heater back to 0°C

## State Added to FilamentPanel

```cpp
enum class PreheatOp { NONE, LOAD, UNLOAD };

PreheatOp pending_preheat_op_ = PreheatOp::NONE;
int pending_preheat_target_ = 0;      // Target temp in °C
int prior_nozzle_target_ = 0;         // What nozzle target was before we started heating
```

## Helper: `resolve_preheat_temp()`

Returns `{int temp, std::string material_name}` using the priority list. Encapsulates the lookup logic so `handle_load_button()` and `handle_unload_button()` stay clean.

## Cancellation

- **Tap load/unload again while pending** → cancel preheat, reset state, notify "Preheat cancelled"
- **Navigate away from panel** → cancel (reset pending state in cleanup/teardown)

## Cooldown Logic

After the load/unload operation completes:
- If `prior_nozzle_target_ == 0` → heater was off before, so set nozzle target back to 0°C
- If `prior_nozzle_target_ > 0` → user had already set a target, leave heater running

## What Does NOT Change

- Extrude, purge, and retract keep the existing "too cold" block — these are "user is managing temp" operations
- No XML changes — pure C++ logic
- AMS sidebar preheat flow is untouched
- The `is_extrusion_allowed()` check remains; it's just no longer a dead-end when false

## Testing

- Unit test: `resolve_preheat_temp()` returns correct priority (external spool > AMS slot > preset > fallback)
- Unit test: cooldown logic — prior target 0 → cool down; prior target >0 → leave on
- Manual test: assign external spool with PETG, press Load while cold, verify auto-heat + load
- Manual test: press Load with no spool, no preset → heats to `min_extrude_temp_` (170°C)
- Manual test: cancel by pressing Load again during preheat
