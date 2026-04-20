# Unified Filament Consumption Tracker

**Date**: 2026-04-19
**Status**: Approved — ready for implementation plan
**Context**: Issue surfaced when Snapmaker U1 slots 1–3 displayed `-1` as spool weight. Root cause: the Snapmaker backend (and nearly every other AMS backend) never populates `remaining_weight_g`. The existing `FilamentConsumptionTracker` handles the external spool only; AMS slots are never decremented during prints.

## Problem

`FilamentConsumptionTracker` today watches `print_stats.filament_used_mm`, converts to grams via the external spool's material density, and writes back to `AmsState::set_external_spool_info()`. It has no notion of AMS slots.

Per the backend audit, none of these populate `remaining_weight_g` from printer-side data without Spoolman: Snapmaker, CFS, IFS (AD5X), HappyHare, ACE, ToolChanger. AFC has a theoretical path via `AFC_lane.weight` but it's unconfirmed in practice. Bambu is not in scope here.

Result: users on non-Spoolman setups have no live spool weight tracking on AMS slots. Manual edits (like the user's 350g correction on U1 slots 1–3) sit static forever.

## Goals

1. Decrement `remaining_weight_g` per AMS slot during a print, for backends that don't self-report.
2. Persist decrements to Moonraker's `lane_data` namespace via `FilamentSlotOverrideStore`, so values survive restarts.
3. Preserve existing external-spool behavior unchanged from the user's perspective.
4. Do not double-count: skip any slot handled by Spoolman or by a self-reporting backend.
5. Attribute consumption correctly on multi-tool printers (one spool per extruder, e.g. Snapmaker U1).

## Non-goals (v1)

- Spoolman write-back / proxy mirroring. When a slot has a `spool_id`, Moonraker's existing `SET_ACTIVE_SPOOL` + Klipper reporting already flows consumption into Spoolman. YAGNI to duplicate.
- Slice-time preflight ("is there enough filament?") integration. No slicer ships this today; OrcaSlicer PR [#4771](https://github.com/SoftFever/OrcaSlicer/pull/4771) is open and would read Spoolman directly if merged.
- AFC native-reporting path. Declare `tracks_consumption_natively()` false for now; revisit if/when we confirm the AFC data stream in practice.
- UI changes beyond verifying the edit modal accepts ≥5000g.

## Design

### 1. Architecture: unified tracker with pluggable sinks

Refactor `helix::FilamentConsumptionTracker` from a single-spool implementation into a multi-stream coordinator. The tracker continues to own the print-lifecycle plumbing (state observer, print-start snapshot, end/cancel flush, disconnect handling). It delegates *where* the decrement lands to a pluggable sink.

```
FilamentConsumptionTracker  (singleton, owns lifecycle)
  └─ sinks: vector<unique_ptr<IConsumptionSink>>
       ├─ ExternalSpoolSink            (one instance, writes settings.json)
       └─ AmsSlotSink[n]                (one per AMS slot, writes lane_data)
```

Sink interface (sketch):

```cpp
class IConsumptionSink {
public:
    virtual ~IConsumptionSink() = default;
    // Identifier for logging.
    virtual std::string_view name() const = 0;
    // True when this sink has a known baseline and a valid density. Tracker skips when false.
    virtual bool is_trackable() const = 0;
    // Called at print-start. Capture current remaining_weight_g + filament_used_mm.
    // May choose to do nothing if not trackable.
    virtual void snapshot(float filament_used_mm) = 0;
    // Called on each delta tick (guaranteed positive mm since last snapshot).
    // Sink computes grams via its own material density and updates remaining_weight_g.
    virtual void apply_delta(float filament_used_mm) = 0;
    // Called on print-end, pause, and final teardown. Flushes in-memory value to persistent store.
    virtual void flush() = 0;
    // Re-baseline after an external write (Spoolman poll, user edit) — tracker detects via
    // compare-to-last-written. Sink resets its snapshot to the observed value.
    virtual void rebaseline(float filament_used_mm) = 0;
};
```

### 2. Data source: per-extruder where available

Today the tracker reads `PrinterState::get_print_filament_used_subject()` — a single aggregate of `print_stats.filament_used` (mm). For multi-tool printers only the active tool accrues at any instant, so even the aggregate routed to the "currently active slot" would be correct — but per-extruder is cleaner and future-proof.

Add (if not already present) per-extruder filament_used subjects:

- `PrinterState::get_extruder_filament_used_subject(int idx, SubjectLifetime&)` — populated from Klipper objects `extruder`, `extruder1`, `extruder2`, `extruder3` (mm, integer, same representation as the aggregate).
- Fire when the extruder's `filament_used` field changes.

Subjects are **dynamic** (created/destroyed on printer reconnect), so observers **must** use `SubjectLifetime` tokens per [L077].

The tracker subscribes to one subject per declared extruder; each subject's callback routes through the backend's `slot_for_extruder()` mapping to the right `AmsSlotSink`. On backends where this mapping isn't available, fall back to the aggregate subject + `system_info_.current_slot`.

### 3. Backend capability declarations

Add two virtuals to `AmsBackendBase` (or equivalent base):

```cpp
// Returns the slot index that currently sources this extruder, or nullopt if the backend
// doesn't model per-extruder attribution (single-extruder multi-slot backends).
virtual std::optional<int> slot_for_extruder(int extruder_idx) const { return std::nullopt; }

// Returns true if the backend already populates remaining_weight_g from a live printer-side
// source (i.e. the tracker should not touch slots on this backend).
virtual bool tracks_consumption_natively() const { return false; }
```

Overrides:

- `AmsBackendSnapmaker`: `slot_for_extruder(i) = i` (identity; 4 independent tools).
- `AmsBackendToolchanger`: `slot_for_extruder(i) = i` (also identity for Klipper tool-changer configs).
- `AmsBackendHappyHare`, `AmsBackendCfs`, `AmsBackendAce`, `AmsBackendAd5xIfs`: leave default (nullopt) — single extruder; tracker uses aggregate + current_slot.
- `AmsBackendAfc`: leave default; optionally return `true` from `tracks_consumption_natively()` if field testing confirms AFC_lane.weight is live. Deferred to a later PR.

### 4. Gating policy (per slot, re-evaluated each tick)

A slot sink is `is_trackable()` when **all** are true:

- `remaining_weight_g >= 0` (user has set a known starting weight)
- `spool_id == 0` (not Spoolman-linked; Spoolman path handles writeback)
- Owning backend's `tracks_consumption_natively() == false`
- Material density resolvable via `filament::find_material(material)`

Corollaries:
- Unknown weight → no tracking. UI shows "full" fallback (existing behavior in `ui_panel_ams.cpp:1019`).
- User edits a slot mid-print to set weight → sink becomes trackable, `rebaseline()` fires on next tick with the new value.
- User links a slot to Spoolman mid-print → sink becomes untrackable; tracker stops decrementing it but retains the last flushed value in persistence.

### 5. Sink implementations

**`ExternalSpoolSink`** (replaces today's inline logic):
- Reads/writes via `AmsState::get_external_spool_info()` / `set_external_spool_info{_in_memory}()`.
- Persistence: `settings.json` (existing).
- Throttle: 60s (existing behavior via `lv_tick_elaps(last_persist_tick_ms_) >= persist_interval_ms()`).
- Rebaseline-on-external-write: existing 0.5g delta check preserved.

**`AmsSlotSink`** (new; one per AMS slot across all backends):
- Reads current `AmsSlotInfo` via `AmsState::instance().get_slot_info(backend_id, slot_idx)`.
- Writes via `AmsState::instance().set_slot_info(backend_id, slot_idx, updated_info, persist=false)` per tick (in-memory), then `persist=true` every 60s.
- Persist=true routes through the backend → `FilamentSlotOverrideStore` → Moonraker `lane_data/<backend>/lane<N>`.
- Rebaseline-on-external-write: watch `remaining_weight_g` drift > 0.5g between ticks (same semantics as external).

### 6. Lifecycle

Print state transitions (already observed via `print_state_enum_subject`):

- `→ PRINTING`: for each registered sink, call `snapshot()`. Sinks that aren't trackable silently skip.
- `→ COMPLETE | CANCELLED | ERROR`: `flush()` on all sinks; clear `active_`.
- `→ PAUSED`: `flush()` on all sinks (crash-safety; preserves existing behavior).
- On each per-extruder (or aggregate-fallback) filament_used tick: route delta to the correct sink via `slot_for_extruder()` or `current_slot`, call `apply_delta()`.

Sink registration:
- `ExternalSpoolSink`: registered once at tracker construction.
- `AmsSlotSink`s: created/destroyed in response to backend lifecycle. `AmsState` (or a small manager) observes backend registration/deregistration and adds/removes sinks. Tracker exposes `register_sink()` / `unregister_sink()`.

### 7. Persistence details

- External: unchanged. `settings.json` via `SettingsManager::set_external_spool_info()`.
- AMS: `FilamentSlotOverrideStore` already serializes `remaining_weight_g` (see `filament_slot_override_store.cpp:70`). No schema change. Writes go through Moonraker's database plugin at `lane_data/<backend_id>/lane<N>`.
- Throttle interval: 60s. Configurable via existing `persist_interval_ms()` knob.
- Final-flush on print end bypasses the throttle.

### 8. UI

No required changes. Verify:
- `ui_ams_edit_modal.cpp` weight roller/input accepts values up to 5000g. If capped lower (e.g. 3000g), lift the cap. Track as a small follow-up task in the plan if needed.
- "Full" fallback at `ui_panel_ams.cpp:1019` already handles `remaining_weight_g < 0` correctly — no change.
- Consider: when unknown, render the modal's weight field blank rather than `-1`. Small polish; include in the plan.

### 9. Testing

Per [L065], no test-only public methods on production classes; use the friend-class pattern for private access.

Test coverage:

1. **Sink unit tests** (per sink, with mocked dependencies):
   - `ExternalSpoolSink`: snapshot, delta, flush, external-write rebaseline, noise filter, sub-gram suppression, filament_used reset.
   - `AmsSlotSink`: same matrix, plus per-slot isolation (two sinks with independent baselines don't interfere).

2. **Tracker lifecycle tests** (with mock sinks, via `FilamentConsumptionTrackerTestAccess`):
   - State transitions → snapshot / flush calls in correct order.
   - Mid-print edit → rebaseline without full re-snapshot.
   - Untrackable sinks silently skip; becoming trackable mid-print starts tracking from that point.
   - Multiple sinks with different material densities compute grams independently.

3. **Backend integration tests**:
   - `AmsBackendMock` extended to validate `slot_for_extruder()` routing for multi-extruder configs.
   - Per-backend smoke test: register backend, simulate a print, assert `remaining_weight_g` decremented on the expected slot and nowhere else.

4. **Gating matrix tests**:
   - Spool_id set → skip.
   - `tracks_consumption_natively()==true` → skip.
   - Unknown material density → skip.
   - Unknown weight → skip.
   - All flags go → track.

Zero AMS/Snapmaker tests exist on the tracker today; this spec closes that gap.

## Risks

- **Dynamic subject lifetime**. Per-extruder subjects are dynamic. Forgetting a `SubjectLifetime` token causes use-after-free on reconnect ([L077]). Mitigated by mandatory lifetime-token parameter on the new accessor.
- **Double-count if Spoolman poll and tracker both run on a slot**. Gated by `spool_id == 0` check; verified by test.
- **Backend-declared consumption**. If we flip AFC to `tracks_consumption_natively=true` and the Klipper field doesn't actually update, users lose tracking silently. Deferred to a future PR with explicit field testing.
- **Density resolution for unknown materials**. Today's tracker warns and skips. Same behavior retained — the user gets a log line, no crash. Future: prompt the user to set a material.

## Success criteria

1. On Snapmaker U1 with user-entered weights on slots 1–3 and no Spoolman: start a print, observe `remaining_weight_g` decrement live per-slot, persist across restart.
2. External spool tracking behaves identically to today (no regressions on existing tests).
3. A slot linked to Spoolman sees weight updates only from Spoolman's 30s poll (no double-decrement from the tracker).
4. All new code covered by tests; no test-only public methods on production classes.

## Implementation phases (handoff to writing-plans)

Rough ordering, 1 commit per phase:

1. Introduce `IConsumptionSink` interface + `ExternalSpoolSink`; refactor tracker to delegate (behavioral no-op for external path).
2. Add per-extruder `filament_used_mm` subjects to `PrinterState` with lifetime tokens.
3. Add `AmsBackendBase::slot_for_extruder()` + `tracks_consumption_natively()` virtuals; implement identity mapping on Snapmaker + Toolchanger.
4. Implement `AmsSlotSink` + registration manager wiring to backend lifecycle.
5. Wire per-extruder routing with aggregate+current_slot fallback.
6. Edit-modal polish (blank instead of `-1`, verify ≥5000g cap).
7. Tests: sink, tracker lifecycle, backend integration, gating.
