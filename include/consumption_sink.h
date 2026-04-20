// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace helix {

/// Target for per-stream filament consumption updates. Owns its own baseline
/// (snapshot_mm + snapshot_weight_g), material density, and persistence throttle.
/// Tracker calls these hooks in response to print lifecycle + filament_used
/// subject events.
class IConsumptionSink {
  public:
    virtual ~IConsumptionSink() = default;

    /// Stable identifier for logging ("external", "ams:0:2", ...).
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// True when this sink has a valid baseline + density. Tracker skips
    /// apply_delta() when false; sink may become trackable later (e.g. user
    /// sets weight mid-print) and rebaseline() fires on the next delta.
    [[nodiscard]] virtual bool is_trackable() const = 0;

    /// Capture the current remaining_weight_g + filament_used_mm baseline.
    /// Called on print start and when a sink is registered mid-print with a
    /// valid state.
    virtual void snapshot(float filament_used_mm) = 0;

    /// Apply the cumulative filament_used_mm delta since snapshot. Sink
    /// converts mm→g and writes remaining_weight_g back to its store. Must be
    /// monotonically non-decreasing between snapshots; the sink handles resets
    /// internally by rebaselining.
    virtual void apply_delta(float filament_used_mm) = 0;

    /// Flush any in-memory changes to persistent store. Called on print end,
    /// pause, and unregister.
    virtual void flush() = 0;

    /// Reset baseline to the currently-observed state. Used when an external
    /// writer (Spoolman poll, user edit) has updated remaining_weight_g
    /// between ticks.
    virtual void rebaseline(float filament_used_mm) = 0;
};

/// Sink for the non-AMS external spool (single global). Writes go through
/// AmsState::set_external_spool_info_in_memory() for hot updates and
/// set_external_spool_info() for throttled disk persistence via SettingsManager.
class ExternalSpoolSink : public IConsumptionSink {
  public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] bool is_trackable() const override;
    void snapshot(float filament_used_mm) override;
    void apply_delta(float filament_used_mm) override;
    void flush() override;
    void rebaseline(float filament_used_mm) override;

    /// Test hook: override persist throttle (ms). 0 = default 60s. Plain-class
    /// sink (not a singleton) makes this setter acceptable for testing
    /// per-sink throttle behavior (see CLAUDE L065).
    void set_persist_interval_ms_for_testing(uint32_t ms) {
        persist_interval_override_ms_ = ms;
    }

  private:
    bool active_ = false;
    float snapshot_mm_ = 0.0f;
    float snapshot_weight_g_ = 0.0f;
    float density_g_cm3_ = 0.0f;
    float diameter_mm_ = 1.75f;
    float last_written_weight_g_ = 0.0f;
    uint32_t last_persist_tick_ms_ = 0;
    uint32_t persist_interval_override_ms_ = 0;

    [[nodiscard]] uint32_t persist_interval_ms() const;
};

} // namespace helix
