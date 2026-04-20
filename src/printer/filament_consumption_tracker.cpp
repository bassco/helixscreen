// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_consumption_tracker.h"

#include "app_globals.h"
#include "observer_factory.h"
#include "printer_state.h"

#include <algorithm>
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

    // Install the one-and-only ExternalSpoolSink on the first start(). Any
    // later stop()/start() cycle keeps the same sink alive.
    if (external_sink_raw_ == nullptr) {
        auto ext = std::make_unique<ExternalSpoolSink>();
        external_sink_raw_ = ext.get();
        sinks_.push_back(std::move(ext));
    }
}

void FilamentConsumptionTracker::stop() {
    print_state_obs_.reset();
    filament_used_obs_.reset();
    active_ = false;
    print_in_progress_ = false;
}

FilamentConsumptionTracker::SinkHandle
FilamentConsumptionTracker::register_sink(std::unique_ptr<IConsumptionSink> sink) {
    if (!sink) {
        return nullptr;
    }
    IConsumptionSink* raw = sink.get();
    sinks_.push_back(std::move(sink));

    // If a print is already in progress, snapshot the new sink immediately so
    // it can start tracking from this point forward. If the new sink becomes
    // trackable and the tracker was previously idle (no other trackable sink),
    // flip active_ so deltas start flowing.
    if (print_in_progress_) {
        auto* subj = get_printer_state().get_print_filament_used_subject();
        const float mm = static_cast<float>(lv_subject_get_int(subj));
        raw->snapshot(mm);
        if (raw->is_trackable()) {
            active_ = true;
        }
        spdlog::debug("[FilamentTracker] Mid-print registered sink '{}' "
                      "(trackable={})",
                      raw->name(), raw->is_trackable());
    }
    return raw;
}

void FilamentConsumptionTracker::unregister_sink(SinkHandle handle) {
    if (!handle) {
        return;
    }
    auto it = std::find_if(
        sinks_.begin(), sinks_.end(),
        [handle](const std::unique_ptr<IConsumptionSink>& s) {
            return s.get() == handle;
        });
    if (it == sinks_.end()) {
        return;
    }
    (*it)->flush();
    if (external_sink_raw_ == handle) {
        external_sink_raw_ = nullptr;
    }
    sinks_.erase(it);
}

std::size_t FilamentConsumptionTracker::ams_sink_count_for_testing() const {
    std::size_t count = 0;
    for (const auto& s : sinks_) {
        if (dynamic_cast<const AmsSlotSink*>(s.get()) != nullptr) {
            ++count;
        }
    }
    return count;
}

void FilamentConsumptionTracker::on_print_state_changed(int job_state) {
    auto state = static_cast<PrintJobState>(job_state);
    auto* printer_mm = get_printer_state().get_print_filament_used_subject();
    const float mm = static_cast<float>(lv_subject_get_int(printer_mm));

    switch (state) {
        case PrintJobState::PRINTING:
            if (!print_in_progress_) {
                snapshot_all_sinks(mm);
                print_in_progress_ = true;
                // active_ is true iff at least one sink is tracking so that
                // is_active() retains its original "tracker has live state"
                // meaning for existing tests.
                active_ = any_sink_trackable();
            }
            break;
        case PrintJobState::COMPLETE:
        case PrintJobState::CANCELLED:
        case PrintJobState::ERROR:
            if (print_in_progress_) {
                flush_all_sinks();
                spdlog::info("[FilamentTracker] Print ended in state {}; "
                             "flushed sinks",
                             job_state);
            }
            active_ = false;
            print_in_progress_ = false;
            break;
        case PrintJobState::PAUSED:
            if (print_in_progress_) {
                flush_all_sinks(); // crash-safety snapshot
            }
            break;
        default:
            break;
    }
}

void FilamentConsumptionTracker::on_filament_used_changed(int filament_mm) {
    if (!print_in_progress_) {
        return;
    }
    apply_delta_all_sinks(static_cast<float>(filament_mm));
    // Sinks may have toggled trackability on this tick (e.g. external write
    // made a previously untrackable sink trackable). Keep is_active() in sync.
    active_ = any_sink_trackable();
}

void FilamentConsumptionTracker::snapshot_all_sinks(float filament_used_mm) {
    for (auto& s : sinks_) {
        s->snapshot(filament_used_mm);
    }
}

void FilamentConsumptionTracker::flush_all_sinks() {
    for (auto& s : sinks_) {
        s->flush();
    }
}

void FilamentConsumptionTracker::apply_delta_all_sinks(float filament_used_mm) {
    for (auto& s : sinks_) {
        s->apply_delta(filament_used_mm);
    }
}

bool FilamentConsumptionTracker::any_sink_trackable() const {
    for (const auto& s : sinks_) {
        if (s->is_trackable()) {
            return true;
        }
    }
    return false;
}

} // namespace helix