// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "consumption_sink.h"
#include "ui_observer_guard.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace helix {

class FilamentConsumptionTracker {
  public:
    /// Opaque handle returned by register_sink() and consumed by unregister_sink().
    using SinkHandle = IConsumptionSink*;

    static FilamentConsumptionTracker& instance();

    /// Register lifecycle + filament-used observers. Call once during app init
    /// after AmsState and PrinterState subjects are available. Also installs
    /// the ExternalSpoolSink on first call.
    void start();

    /// Tear down observers. Safe to call multiple times. Does NOT unregister
    /// sinks — the tracker keeps its registry across start/stop cycles so that
    /// `register_sink` / `unregister_sink` calls from AmsState remain valid.
    void stop();

    /// True while a snapshot is live (print in progress; at least one sink
    /// successfully snapshotted).
    [[nodiscard]] bool is_active() const {
        return active_;
    }

    /// Register a sink. Tracker takes ownership. If a print is currently
    /// active, immediately calls snapshot() on the new sink using the current
    /// aggregate filament_used reading so mid-print registration still tracks
    /// from the point of registration. Returns a non-owning handle.
    SinkHandle register_sink(std::unique_ptr<IConsumptionSink> sink);

    /// Unregister a sink. Flushes the sink before destruction. Safe to call
    /// with a null / already-removed handle (no-op).
    void unregister_sink(SinkHandle handle);

    /// Test-only visibility of the sink registry. Not for production use.
    [[nodiscard]] std::size_t sink_count_for_testing() const {
        return sinks_.size();
    }

    /// Test-only: count of AmsSlotSink instances (ignores ExternalSpoolSink).
    [[nodiscard]] std::size_t ams_sink_count_for_testing() const;

  private:
    friend struct FilamentConsumptionTrackerTestAccess;

    FilamentConsumptionTracker() = default;
    ~FilamentConsumptionTracker() = default;
    FilamentConsumptionTracker(const FilamentConsumptionTracker&) = delete;
    FilamentConsumptionTracker& operator=(const FilamentConsumptionTracker&) = delete;

    /// All registered sinks. The ExternalSpoolSink is installed lazily by
    /// start() and lives here for the process lifetime; AmsSlotSinks are added
    /// and removed by AmsState in response to backend lifecycle events.
    std::vector<std::unique_ptr<IConsumptionSink>> sinks_;

    /// Convenience handle to the one-and-only ExternalSpoolSink. Non-owning.
    IConsumptionSink* external_sink_raw_ = nullptr;

    /// True when at least one sink successfully snapshotted on print start.
    /// Preserved for backwards-compatible `is_active()` semantics and used to
    /// decide whether on_filament_used_changed forwards deltas.
    bool active_ = false;

    /// True between the PRINTING transition and a terminal state transition.
    /// Tracks the print lifecycle independently of sink trackability so that
    /// mid-print sink registration can snapshot the new sink.
    bool print_in_progress_ = false;

    ObserverGuard print_state_obs_;
    ObserverGuard filament_used_obs_;

    void on_print_state_changed(int job_state);
    void on_filament_used_changed(int filament_mm);

    /// Snapshot every registered sink. Called on PRINTING transition.
    void snapshot_all_sinks(float filament_used_mm);

    /// Flush every registered sink. Called on COMPLETE / CANCELLED / ERROR / PAUSED.
    void flush_all_sinks();

    /// Apply a delta to every currently-trackable sink.
    void apply_delta_all_sinks(float filament_used_mm);

    /// True when at least one registered sink is currently trackable.
    [[nodiscard]] bool any_sink_trackable() const;
};

} // namespace helix