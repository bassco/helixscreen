// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_consumption_tracker.h"

namespace helix {

FilamentConsumptionTracker& FilamentConsumptionTracker::instance() {
    static FilamentConsumptionTracker inst;
    return inst;
}

void FilamentConsumptionTracker::start() {
    // Observer registration lands in Task 4.
}

void FilamentConsumptionTracker::stop() {
    print_state_obs_.reset();
    filament_used_obs_.reset();
    active_ = false;
}

void FilamentConsumptionTracker::on_print_state_changed(int /*job_state*/) {
    // Implemented in Task 4.
}

void FilamentConsumptionTracker::on_filament_used_changed(int /*filament_mm*/) {
    // Implemented in Task 5.
}

void FilamentConsumptionTracker::snapshot() {
    // Implemented in Task 4.
}

void FilamentConsumptionTracker::persist() {
    // Implemented in Task 6.
}

} // namespace helix
