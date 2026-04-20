// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_consumption_tracker.h"

#include "app_globals.h"
#include "observer_factory.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace helix {

FilamentConsumptionTracker& FilamentConsumptionTracker::instance() {
    static FilamentConsumptionTracker inst;
    return inst;
}

void FilamentConsumptionTracker::start() {
    PrinterState& printer = get_printer_state();

    print_state_obs_ = helix::ui::observe_int_sync<FilamentConsumptionTracker>(
        printer.get_print_state_enum_subject(), this,
        [](FilamentConsumptionTracker* self, int state) {
            self->on_print_state_changed(state);
        });

    filament_used_obs_ = helix::ui::observe_int_sync<FilamentConsumptionTracker>(
        printer.get_print_filament_used_subject(), this,
        [](FilamentConsumptionTracker* self, int mm) {
            self->on_filament_used_changed(mm);
        });
}

void FilamentConsumptionTracker::stop() {
    print_state_obs_.reset();
    filament_used_obs_.reset();
    active_ = false;
}

void FilamentConsumptionTracker::on_print_state_changed(int job_state) {
    auto state = static_cast<PrintJobState>(job_state);
    auto* printer_mm = get_printer_state().get_print_filament_used_subject();
    float mm = static_cast<float>(lv_subject_get_int(printer_mm));

    switch (state) {
        case PrintJobState::PRINTING:
            if (!active_) {
                external_sink_.snapshot(mm);
                active_ = external_sink_.is_trackable();
            }
            break;
        case PrintJobState::COMPLETE:
        case PrintJobState::CANCELLED:
        case PrintJobState::ERROR:
            if (active_) {
                external_sink_.flush();
                spdlog::info(
                    "[FilamentTracker] Print ended in state {}; persisted final weight",
                    job_state);
            }
            active_ = false;
            break;
        case PrintJobState::PAUSED:
            if (active_) {
                external_sink_.flush(); // crash-safety snapshot
            }
            break;
        default:
            break;
    }
}

void FilamentConsumptionTracker::on_filament_used_changed(int filament_mm) {
    if (!active_) {
        return;
    }
    external_sink_.apply_delta(static_cast<float>(filament_mm));
}

} // namespace helix
