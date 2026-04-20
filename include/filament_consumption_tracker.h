// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "consumption_sink.h"
#include "ui_observer_guard.h"

namespace helix {

class FilamentConsumptionTracker {
  public:
    static FilamentConsumptionTracker& instance();

    /// Register lifecycle + filament-used observers. Call once during app init
    /// after AmsState and PrinterState subjects are available.
    void start();

    /// Tear down observers. Safe to call multiple times.
    void stop();

    /// True while a snapshot is live (print in progress, spool assigned,
    /// density resolved).
    [[nodiscard]] bool is_active() const {
        return active_;
    }

  private:
    friend struct FilamentConsumptionTrackerTestAccess;

    FilamentConsumptionTracker() = default;
    ~FilamentConsumptionTracker() = default;
    FilamentConsumptionTracker(const FilamentConsumptionTracker&) = delete;
    FilamentConsumptionTracker& operator=(const FilamentConsumptionTracker&) = delete;

    /// Sink owning weight-tracking state for the non-AMS external spool. Phase 1
    /// holds a single sink directly; a registry replaces this in a later phase.
    ExternalSpoolSink external_sink_;

    bool active_ = false;

    ObserverGuard print_state_obs_;
    ObserverGuard filament_used_obs_;

    void on_print_state_changed(int job_state);
    void on_filament_used_changed(int filament_mm);
};

} // namespace helix
