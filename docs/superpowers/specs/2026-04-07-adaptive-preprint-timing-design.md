# Adaptive Pre-Print Timing Design

**Date:** 2026-04-07
**Status:** Draft
**Scope:** Shared thermal model, enhanced pre-print prediction, detail view estimate, live progress improvements

## Problem

The pre-print prediction system (`PreprintPredictor` + `PrintStartCollector`) has significant accuracy gaps:

1. **Heating estimation is purely empirical** — stores raw wall-clock durations for heating phases with no awareness of starting temperature. A warm-start print (nozzle already at 180C) gets the same estimate as a cold-start (25C).
2. **Tiny sample size** — only 3 entries per temperature bucket. One anomalous print is 33% of the dataset.
3. **Duplicated heating logic** — `PidProgressTracker` has a well-tuned EMA heating rate model (s/C) that's not shared with the pre-print system.
4. **No pre-print estimate in the detail view** — user has no idea how long prep will take before hitting Print.
5. **Progress bar is jerky** — static profile weights don't reflect real durations; `max(time_progress, phase_progress)` causes jumps.

## Solution

Extract PID calibration's adaptive heating rate model into a shared utility. Use physics-based heating estimates (remaining degrees x learned rate) in the pre-print predictor. Improve empirical learning for non-heating phases. Surface a prep time estimate on the detail view.

## Design

### 1. Shared Thermal Rate Model

**New files:** `include/thermal_rate_model.h`, `src/temperature/thermal_rate_model.cpp`

#### ThermalRateModel

Per-heater model that tracks heating rate in seconds-per-degree (s/C) using exponential moving average.

```cpp
class ThermalRateModel {
public:
    // Record a live temperature sample, updates internal EMA
    void record_sample(float temp_c, uint32_t tick_ms);

    // Estimate seconds to reach target from current temp
    float estimate_seconds(float current_temp, float target_temp) const;

    // Get learned rate, or nullopt if insufficient data
    std::optional<float> measured_rate() const;

    // Persistent history (EMA-blended across sessions)
    void load_history(float heat_rate_s_per_deg);
    float get_history_rate() const;

    // Reset for a new tracking session
    void reset(float start_temp);

private:
    float measured_heat_rate_ = 0.0f;
    bool has_measured_heat_rate_ = false;
    float hist_heat_rate_ = 0.0f;
    bool has_history_ = false;
    float start_temp_ = 0.0f;
    float last_temp_ = 0.0f;
    uint32_t last_tick_ = 0;
    uint32_t start_tick_ = 0;
};
```

**EMA parameters (ported from PidProgressTracker):**
- Instantaneous rate requires >= 2C delta from last sample
- First usable measurement requires >= 5C total movement from start
- Live EMA: 30% new sample, 70% accumulated
- History blending on save: 70% new measured, 30% old history

**Rate priority:** measured (live) > history (persisted) > default (printer archetype)

#### ThermalRateManager

Singleton owning per-heater `ThermalRateModel` instances.

```cpp
class ThermalRateManager {
public:
    static ThermalRateManager& instance();

    // Get or create model for a heater
    ThermalRateModel& get_model(const std::string& heater_name);

    // Load all from config, save all to config
    void load_from_config(Config& config);
    void save_to_config(Config& config);

    // Estimate heating time using current temps
    float estimate_heating_seconds(
        const std::string& heater_name,
        float current_temp,
        float target_temp) const;
};
```

**Persistence path:** `/thermal/rates/{heater_name}/heat_rate`

#### Smarter Defaults

Instead of hardcoded 0.5 s/C (extruder) and 1.5 s/C (bed), query `PrinterDetector` for printer archetype:

| Archetype | Extruder Default | Bed Default |
|-----------|-----------------|-------------|
| Small hotend (Bambu, K1) | 0.35 s/C | varies by bed |
| Standard hotend (Voron, Ender) | 0.50 s/C | varies by bed |
| Large bed (350mm+) | -- | 2.0 s/C |
| Medium bed (250-349mm) | -- | 1.5 s/C |
| Small bed (<250mm) | -- | 1.2 s/C |

Defaults are used only until the first real measurement replaces them.

#### PID Calibration Refactor

`PidProgressTracker` drops its inline EMA heating rate fields and delegates to a `ThermalRateModel` instance. The oscillation tracking (zero-crossing counting, cycle period measurement) stays in `PidProgressTracker` since it's PID-specific. Net result: less code in PID, same behavior.

### 2. Enhanced PreprintPredictor

#### Heating Phases: Physics-Based

Heating phases (HEATING_BED, HEATING_NOZZLE) are removed from the duration history entirely. Instead, at prediction time:

```
heating_estimate = ThermalRateManager::estimate_heating_seconds(
    heater_name, current_temp, target_temp)
```

This automatically handles:
- Warm-start vs cold-start (uses actual current temp)
- Different target temperatures (uses actual target)
- Printer-specific heating performance (learned rate)

#### Non-Heating Phases: Improved Empirical Learning

**Increased history:** MAX_ENTRIES raised from 3 to 10 per bucket.

**Exponential time-decay weighting:** Instead of fixed positional weights (50/30/20), weight entries by recency using exponential decay. More recent prints count more, but 10 entries provides statistical stability. One anomaly is ~10% of the dataset, not 33%.

**Anomaly rejection:** Replace the hard 15-minute cutoff with median absolute deviation (MAD). Reject entries where a phase duration exceeds 3x the median for that phase. This adapts to the printer's actual behavior rather than using an arbitrary threshold.

**Missing-phase normalization:** If a print didn't include BED_MESH, don't store a zero duration for that phase. Omit it from the entry entirely. When predicting, average only over entries that actually ran that phase.

#### Simplified Bucketing

With heating extracted to ThermalRateModel, temperature bucketing only matters for non-heating phases (thermal expansion can affect QGL/bed mesh accuracy).

Simplify from 25C nozzle buckets to two categories:
- **Cold start:** bed was < 40C when print started
- **Warm start:** bed was >= 40C when print started

Drop nozzle temperature bucketing — it was a proxy for heating time, which is now handled by the thermal model.

#### Smarter Non-Heating Defaults

Query `PrinterDetector` capabilities for initial guesses:

| Operation | Default (if capable) |
|-----------|---------------------|
| HOMING | 20s |
| BED_MESH | 90s |
| QGL | 60s |
| Z_TILT | 45s |
| CLEANING | 15s |
| PURGING | 10s |

These are replaced by learned values after the first print that includes each operation.

### 3. Detail View Estimate

#### New Subject

`PrintPreparationManager` gains a `preprint_estimate_subject_` (int, seconds) that recalculates when:
- User toggles a pre-print checkbox (bed mesh, QGL, etc.)
- Filament preset changes (different target temperatures)
- Current heater temps change significantly (> 10C since last calc)

#### Calculation

```
total_estimate =
    ThermalRateManager::estimate_heating_seconds("extruder", current_nozzle, target_nozzle)
  + ThermalRateManager::estimate_heating_seconds("heater_bed", current_bed, target_bed)
  + PreprintPredictor::estimate_operation_seconds(HOMING)
  + (bed_mesh_enabled ? PreprintPredictor::estimate_operation_seconds(BED_MESH) : 0)
  + (qgl_enabled ? PreprintPredictor::estimate_operation_seconds(QGL) : 0)
  + (z_tilt_enabled ? PreprintPredictor::estimate_operation_seconds(Z_TILT) : 0)
  + (clean_enabled ? PreprintPredictor::estimate_operation_seconds(CLEANING) : 0)
  + (purge_enabled ? PreprintPredictor::estimate_operation_seconds(PURGING) : 0)
```

#### Display

- XML label in `print_file_detail.xml` bound to `preprint_estimate_subject_`
- Formatting: "~X min" rounded to nearest 30s when > 2 min, nearest 10s when < 2 min
- Updates immediately on checkbox toggle (subject recalculation)

### 4. Live Progress Improvements

#### Duration-Proportional Phase Weights

Phase weights are no longer static from profile JSON. They're derived from predicted durations at print start:

```
If predicted: heating=180s, QGL=60s, mesh=90s
Then weights: heating=55%, QGL=18%, mesh=27%
```

The progress bar moves proportionally to real time, not arbitrary weight units.

#### 5-Second Update with Animation

- Prediction recalculates every 5 seconds (existing interval)
- Progress bar target updates every 5 seconds
- LVGL animation smoothly transitions the bar to the new target (~500ms ease)
- No sub-second interpolation — steady, confident movement

#### Temperature-Driven Heating Progress

During HEATING_BED/HEATING_NOZZLE phases, progress within that phase is:

```
phase_progress = (current_temp - start_temp) / (target_temp - start_temp)
```

Read directly from temperature subjects. This gives physically meaningful progress rather than time-based guessing.

#### ETA Rounding

- \> 2 minutes: round to nearest 30 seconds
- <= 2 minutes: round to nearest 10 seconds
- Absorbs small fluctuations, prevents display flicker

#### Monotonic Bias

Once ETA is under 2 minutes, bias toward only decreasing unless a phase genuinely overruns its prediction by > 20%. Prevents the "almost done... wait, more time" anxiety loop.

#### Remove max(time, phase) Hack

The current `effective_progress = max(time_progress, phase_progress)` that causes jumps is removed. Progress is always:

```
completed_phases_weight + current_phase_fraction * current_phase_weight
```

Single source of truth, no competing progress sources.

### 5. Config Migration (v10 -> v11)

Standard versioned migration following existing patterns in `config.cpp`.

#### Migration Steps

1. **Move thermal rates:**
   - Copy `/calibration/pid_history/extruder/heat_rate` to `/thermal/rates/extruder/heat_rate`
   - Copy `/calibration/pid_history/heater_bed/heat_rate` to `/thermal/rates/heater_bed/heat_rate`
   - Skip if source doesn't exist (fresh install)

2. **Clean predictor entries:**
   - For each entry in `/print_start_history/entries`, remove HEATING_BED and HEATING_NOZZLE keys from `phase_durations`
   - Preserves all non-heating phase data

3. **Remove old paths:**
   - Delete `/calibration/pid_history/` after successful copy

4. **Bump version:**
   - `CURRENT_CONFIG_VERSION = 11`

Migration is idempotent — checks for target path existence before copying, safe to re-run.

### 6. Total Print Time / Finish Time Integration

Currently, the ETA finish time ("~3:45 PM") is calculated purely from `print_duration` (extrusion-only elapsed) + progress extrapolation. Pre-print prep time is not factored in, so the ETA shown at the start of a print is wrong by 5-10+ minutes.

#### Problem

- `print_time_left_` is derived from `print_duration / progress * (100 - progress)` — extrusion only
- `eta_clock_time()` adds `remaining_seconds` to current time — does not include prep
- During the pre-print phase (progress=0), there's no remaining time estimate at all from the print side
- The user sees "finish at 3:45 PM" but the print won't actually finish until 3:52 PM because prep wasn't counted

#### Solution

When computing ETA and total remaining time, add the pre-print estimate:

**During pre-print phase (print not yet started):**
```
total_remaining = preprint_remaining + slicer_estimated_print_time
finish_time = now + total_remaining
```

**During early print phase (progress < 5%, prep just finished):**
```
// Prep is done, but print ETA is still noisy at low progress
// Use slicer estimate directly, don't extrapolate from tiny progress
total_remaining = slicer_estimated_print_time * (100 - progress) / 100
```

**During normal print phase (progress >= 5%):**
- Existing algorithm takes over (progress-based extrapolation blended with slicer estimate)
- Prep time is already elapsed and captured in `print_elapsed` (wall-clock)

#### Modified Files

| File | Change |
|------|--------|
| `src/printer/printer_print_state.cpp` | Add preprint_remaining to ETA during prep phase; use slicer estimate at low progress |
| `src/ui/ui_panel_print_status.cpp` | During pre-print phase, display combined ETA from prep + slicer estimate |

#### Behavioral Notes

- The detail view estimate (Section 3) and the print status ETA use the same underlying prediction — `ThermalRateManager` + `PreprintPredictor`. No duplication.
- Once the print is underway (progress > 5%), the pre-print system has no further role in ETA. The existing progress-based algorithm is accurate for the printing phase.
- The transition from "prep ETA" to "print ETA" should be seamless — no visible jump. At the moment prep completes and printing starts, both estimates should converge (prep remaining → 0, print remaining picks up the full slicer estimate).

## File Changes

### New Files

| File | Purpose |
|------|---------|
| `include/thermal_rate_model.h` | Shared thermal rate EMA model + manager singleton |
| `src/temperature/thermal_rate_model.cpp` | Implementation |
| `tests/unit/test_thermal_rate_model.cpp` | Unit tests for thermal model |

### Modified Files

| File | Change |
|------|--------|
| `include/pid_progress_tracker.h` | Remove inline EMA fields, hold `ThermalRateModel` instance |
| `src/ui/pid_progress_tracker.cpp` | Delegate heating rate to `ThermalRateModel` |
| `include/preprint_predictor.h` | Remove heating from history, add thermal model dependency, MAX_ENTRIES=10, new weighting |
| `src/ui/preprint_predictor.cpp` | Hybrid prediction: thermal for heating, improved empirical for ops |
| `include/print_start_collector.h` | Feed temp samples to thermal model during heating, duration-proportional weights |
| `src/print/print_start_collector.cpp` | Remove max(time,phase), use proportional progress, animated bar, monotonic bias |
| `include/ui_print_preparation_manager.h` | Add `preprint_estimate_subject_`, estimate calculation |
| `src/ui/ui_print_preparation_manager.cpp` | Wire estimate on checkbox toggle, query current temps |
| `ui_xml/print_file_detail.xml` | Add prep time estimate label bound to new subject |
| `include/printer_detector.h` | Expose archetype info for thermal defaults (bed size, hotend type) |
| `include/config.h` | Bump CURRENT_CONFIG_VERSION to 11 |
| `src/system/config.cpp` | Add `migrate_v10_to_v11()` |
| `tests/unit/test_preprint_predictor.cpp` | Updated tests for new prediction logic |
| `tests/unit/test_config.cpp` | Migration test for v10->v11 |
| `src/printer/printer_print_state.cpp` | Add preprint_remaining to ETA during prep phase |
| `src/ui/ui_panel_print_status.cpp` | Combined ETA display during pre-print phase |

### Unchanged

- Profile system (`print_start_profiles/*.json`) — untouched
- `PrintStartCollector` phase detection logic — untouched
- `PrinterState` subjects — existing ones reused, one new added
- `TemperatureService` presets — untouched
- `operation_patterns.h` — untouched

## Testing Strategy

- **ThermalRateModel unit tests:** EMA convergence, rate priority (measured > history > default), edge cases (no samples, single sample, large temperature jumps)
- **PreprintPredictor unit tests:** Hybrid prediction accuracy, anomaly rejection (MAD), missing-phase normalization, cold/warm bucket separation
- **Config migration test:** v10 config with PID history -> v11 with thermal rates moved, heating stripped from entries
- **Integration:** PID calibration still works correctly through ThermalRateModel delegation
- **Manual validation:** Run PID calibration, confirm learned rate persists to new path. Start a print, confirm detail view shows estimate, confirm live progress is smooth and proportional.
