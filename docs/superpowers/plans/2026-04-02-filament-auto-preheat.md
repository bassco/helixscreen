# Filament Auto-Preheat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Auto-heat the extruder when load/unload is pressed and the nozzle is too cold, using the assigned spool's material temperature instead of blocking with a warning.

**Architecture:** Add preheat state tracking (`PreheatOp`, target temp, prior target) to `FilamentPanel`. When load/unload is pressed while cold, resolve the target temp from the spool/material priority chain, start heating, and poll via the existing `update_all_temps()` callback. Once hot, execute the operation. Restore heater state afterward based on whether it was on before.

**Tech Stack:** C++ (LVGL, spdlog), existing `active_material_provider.h` for temp lookup, `MoonrakerAPI::set_temperature()` for heater control.

---

### Task 1: Add Preheat State and `resolve_preheat_temp()` Helper

**Files:**
- Modify: `include/ui_panel_filament.h:230-245` (add state members + declarations)
- Modify: `src/ui/ui_panel_filament.cpp` (add helper implementation)

- [ ] **Step 1: Add preheat state to header**

In `include/ui_panel_filament.h`, add after line 244 (`int min_extrude_temp_ = 170;`):

```cpp
// Auto-preheat state for load/unload
enum class PreheatOp { NONE, LOAD, UNLOAD };
PreheatOp pending_preheat_op_ = PreheatOp::NONE;
int pending_preheat_target_ = 0;   ///< Target temp in °C for pending preheat
int prior_nozzle_target_ = 0;      ///< Nozzle target before preheat (0 = was off → cool down after)
```

- [ ] **Step 2: Add method declarations to header**

In `include/ui_panel_filament.h`, add in the Private Helpers section (after `update_all_temps()` around line 322):

```cpp
void check_pending_preheat();                        ///< Called from update_all_temps()
void cancel_pending_preheat();                       ///< Reset preheat state + notify
struct PreheatTempResult {
    int temp = 0;
    std::string material_name;
};
PreheatTempResult resolve_preheat_temp() const;      ///< Priority: ext spool > AMS slot > preset > fallback
void start_preheat_for_op(PreheatOp op);             ///< Resolve temp, heat, set pending state
void restore_heater_after_preheat();                 ///< Cool down if heater was off before preheat
```

- [ ] **Step 3: Implement `resolve_preheat_temp()`**

In `src/ui/ui_panel_filament.cpp`, add after the `is_extrusion_allowed()` function (around line 1554):

```cpp
FilamentPanel::PreheatTempResult FilamentPanel::resolve_preheat_temp() const {
    // Priority 1: External spool
    auto ext = AmsState::instance().get_external_spool_info();
    if (ext.has_value()) {
        auto active = helix::build_active_material(*ext);
        if (active.material_info.nozzle_min > 0) {
            return {active.material_info.nozzle_min, active.display_name};
        }
    }

    // Priority 2: AMS active slot (loaded filament)
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        AmsSystemInfo sys_info = backend->get_system_info();
        const SlotInfo* active_slot = sys_info.get_active_slot();
        if (active_slot) {
            auto active = helix::build_active_material(*active_slot);
            if (active.material_info.nozzle_min > 0) {
                return {active.material_info.nozzle_min, active.display_name};
            }
        }
    }

    // Priority 3: Selected material preset
    if (selected_material_ >= 0 && selected_material_ < PRESET_COUNT) {
        auto mat = filament::find_material(PRESET_MATERIAL_NAMES[selected_material_]);
        if (mat) {
            return {mat->nozzle_min, PRESET_MATERIAL_NAMES[selected_material_]};
        }
    }

    // Priority 4: Fallback to min_extrude_temp_
    return {min_extrude_temp_, ""};
}
```

- [ ] **Step 4: Implement `start_preheat_for_op()`**

In `src/ui/ui_panel_filament.cpp`, add after `resolve_preheat_temp()`:

```cpp
void FilamentPanel::start_preheat_for_op(PreheatOp op) {
    auto [target, material_name] = resolve_preheat_temp();

    // Snapshot heater state before we change it
    prior_nozzle_target_ = nozzle_target_;
    pending_preheat_op_ = op;
    pending_preheat_target_ = target;

    if (api_) {
        api_->set_temperature(
            printer_state_.active_extruder_name(), static_cast<double>(target), []() {},
            [](const MoonrakerError& /*err*/) {});
    }

    if (material_name.empty()) {
        NOTIFY_INFO(lv_tr("Heating to {}°C..."), target);
    } else {
        NOTIFY_INFO(lv_tr("Heating to {}°C for {}..."), target, material_name);
    }

    const char* op_name = (op == PreheatOp::LOAD) ? "load" : "unload";
    spdlog::info("[{}] Starting preheat to {}°C ({}) for {}", get_name(), target,
                 material_name.empty() ? "fallback" : material_name, op_name);
}
```

- [ ] **Step 5: Implement `check_pending_preheat()`**

In `src/ui/ui_panel_filament.cpp`, add after `start_preheat_for_op()`:

```cpp
void FilamentPanel::check_pending_preheat() {
    if (pending_preheat_op_ == PreheatOp::NONE) {
        return;
    }

    constexpr int TEMP_THRESHOLD = 5;
    if (nozzle_current_ < (pending_preheat_target_ - TEMP_THRESHOLD)) {
        return;
    }

    PreheatOp op = pending_preheat_op_;
    pending_preheat_op_ = PreheatOp::NONE;
    pending_preheat_target_ = 0;

    spdlog::info("[{}] Preheat complete, executing {}", get_name(),
                 op == PreheatOp::LOAD ? "load" : "unload");

    if (op == PreheatOp::LOAD) {
        execute_load();
    } else {
        execute_unload();
    }
}
```

- [ ] **Step 6: Implement `cancel_pending_preheat()`**

In `src/ui/ui_panel_filament.cpp`, add after `check_pending_preheat()`:

```cpp
void FilamentPanel::cancel_pending_preheat() {
    if (pending_preheat_op_ == PreheatOp::NONE) {
        return;
    }

    spdlog::info("[{}] Preheat cancelled", get_name());
    pending_preheat_op_ = PreheatOp::NONE;
    pending_preheat_target_ = 0;

    // Restore heater to prior state
    restore_heater_after_preheat();

    NOTIFY_INFO(lv_tr("Preheat cancelled"));
}
```

- [ ] **Step 7: Implement `restore_heater_after_preheat()`**

In `src/ui/ui_panel_filament.cpp`, add after `cancel_pending_preheat()`:

```cpp
void FilamentPanel::restore_heater_after_preheat() {
    if (prior_nozzle_target_ == 0 && api_) {
        spdlog::info("[{}] Heater was off before preheat, cooling down", get_name());
        api_->set_temperature(
            printer_state_.active_extruder_name(), 0, []() {},
            [](const MoonrakerError& /*err*/) {});
    }
    prior_nozzle_target_ = 0;
}
```

- [ ] **Step 8: Build to verify compilation**

Run: `make -j`
Expected: Clean build, no errors.

- [ ] **Step 9: Commit**

```bash
git add include/ui_panel_filament.h src/ui/ui_panel_filament.cpp
git commit -m "feat(filament): add auto-preheat state and helper methods"
```

---

### Task 2: Wire Preheat Into Load/Unload and Temperature Update

**Files:**
- Modify: `src/ui/ui_panel_filament.cpp:718-771` (`handle_load_button`, `handle_unload_button`)
- Modify: `src/ui/ui_panel_filament.cpp:524-550` (`update_all_temps`)

- [ ] **Step 1: Modify `handle_load_button()` to preheat instead of block**

In `src/ui/ui_panel_filament.cpp`, replace the `is_extrusion_allowed()` check in `handle_load_button()` (lines 724-728):

Old:
```cpp
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING(lv_tr("Nozzle too cold for filament load ({}°C, min: {}°C)"), nozzle_current_,
                       min_extrude_temp_);
        return;
    }
```

New:
```cpp
    // Cancel existing preheat if user taps again
    if (pending_preheat_op_ != PreheatOp::NONE) {
        cancel_pending_preheat();
        return;
    }

    if (!is_extrusion_allowed()) {
        start_preheat_for_op(PreheatOp::LOAD);
        return;
    }
```

- [ ] **Step 2: Modify `handle_unload_button()` to preheat instead of block**

In `src/ui/ui_panel_filament.cpp`, replace the `is_extrusion_allowed()` check in `handle_unload_button()` (lines 752-755):

Old:
```cpp
    if (!is_extrusion_allowed()) {
        NOTIFY_WARNING(lv_tr("Nozzle too cold for filament unload ({}°C, min: {}°C)"), nozzle_current_,
                       min_extrude_temp_);
        return;
    }
```

New:
```cpp
    // Cancel existing preheat if user taps again
    if (pending_preheat_op_ != PreheatOp::NONE) {
        cancel_pending_preheat();
        return;
    }

    if (!is_extrusion_allowed()) {
        start_preheat_for_op(PreheatOp::UNLOAD);
        return;
    }
```

- [ ] **Step 3: Call `check_pending_preheat()` from `update_all_temps()`**

In `src/ui/ui_panel_filament.cpp`, add at the end of `update_all_temps()` (before the closing `}`), after the `targets_changed` block:

```cpp
    // Check if pending preheat target has been reached
    check_pending_preheat();
```

- [ ] **Step 4: Build to verify compilation**

Run: `make -j`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_panel_filament.cpp
git commit -m "feat(filament): auto-preheat on load/unload instead of blocking"
```

---

### Task 3: Cooldown After Operation and Navigation Cancellation

**Files:**
- Modify: `src/ui/ui_panel_filament.cpp:1577-1742` (`execute_load`, `execute_unload` success callbacks)
- Modify: `src/ui/ui_panel_filament.cpp` (cleanup on teardown)

- [ ] **Step 1: Add cooldown in `execute_load()` success callback**

In `src/ui/ui_panel_filament.cpp`, in `execute_load()`, find the fallback G-code success callback (around line 1638):

Old:
```cpp
        [this]() {
            helix::ui::async_call(
                [](void* ud) { static_cast<FilamentPanel*>(ud)->operation_guard_.end(); }, this);
            NOTIFY_SUCCESS(lv_tr("Filament loaded"));
        },
```

New:
```cpp
        [this]() {
            helix::ui::async_call(
                [](void* ud) {
                    auto* self = static_cast<FilamentPanel*>(ud);
                    self->operation_guard_.end();
                    self->restore_heater_after_preheat();
                },
                this);
            NOTIFY_SUCCESS(lv_tr("Filament loaded"));
        },
```

Also add the same `restore_heater_after_preheat()` call in the `run_filament_macro` success path. Find `run_filament_macro()` and add restore logic in the success callback.

- [ ] **Step 2: Add cooldown in `execute_unload()` success callback**

In `src/ui/ui_panel_filament.cpp`, in `execute_unload()`, find the fallback G-code success callback (around line 1727):

Old:
```cpp
        [this]() {
            helix::ui::async_call(
                [](void* ud) { static_cast<FilamentPanel*>(ud)->operation_guard_.end(); }, this);
            NOTIFY_SUCCESS(lv_tr("Filament unloaded"));
        },
```

New:
```cpp
        [this]() {
            helix::ui::async_call(
                [](void* ud) {
                    auto* self = static_cast<FilamentPanel*>(ud);
                    self->operation_guard_.end();
                    self->restore_heater_after_preheat();
                },
                this);
            NOTIFY_SUCCESS(lv_tr("Filament unloaded"));
        },
```

Also add `restore_heater_after_preheat()` in the AMS backend unload path (around line 1671, after `backend->unload_filament()` succeeds — add to wherever `operation_guard_.end()` is called on success).

- [ ] **Step 3: Cancel preheat on panel teardown/navigation**

Find where the panel resets state when navigated away from. Check if `deinit_subjects()` or the destructor handles this. Add at the top of `deinit_subjects()`:

```cpp
    cancel_pending_preheat();
```

Note: `cancel_pending_preheat()` already checks for `NONE` state, so this is safe to call unconditionally.

- [ ] **Step 4: Build to verify compilation**

Run: `make -j`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add include/ui_panel_filament.h src/ui/ui_panel_filament.cpp
git commit -m "feat(filament): cooldown after preheat op + cancel on navigation"
```

---

### Task 4: Manual Testing on K1C

**Prerequisite:** Build and deploy using `K1_HOST=192.168.30.182 make k1-test`

- [ ] **Step 1: Test with external spool assigned (PETG)**

1. Assign a PETG spool to external spool (nozzle_min ~230°C)
2. With extruder cold, tap **Load**
3. Verify notification: "Heating to 230°C for PETG..." (or similar)
4. Wait for temp to reach ~225°C
5. Verify load operation executes automatically
6. Verify heater turns OFF after load (since it was off before)

- [ ] **Step 2: Test with heater already on**

1. Set nozzle to 200°C via PLA preset
2. Assign a PETG spool (230°C)
3. Tap **Load** — should preheat to 230°C
4. After load completes, verify heater stays ON (was already on at 200°C before)

- [ ] **Step 3: Test cancellation**

1. With extruder cold, tap **Load** (preheat starts)
2. Tap **Load** again — verify "Preheat cancelled" notification
3. Verify heater returns to prior state

- [ ] **Step 4: Test with no spool, no preset**

1. Clear external spool, no AMS, no preset selected
2. Tap **Load** while cold
3. Verify it heats to `min_extrude_temp_` (170°C)
4. Verify load executes after reaching temp

- [ ] **Step 5: Test unload**

1. With filament loaded and extruder cold
2. Tap **Unload**
3. Verify preheat uses the loaded slot's material temp (if AMS) or external spool temp
