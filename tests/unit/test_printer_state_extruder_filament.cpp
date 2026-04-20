// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_state_extruder_filament.cpp
 * @brief Per-extruder filament_used_mm subject exposure on PrinterState.
 *
 * Phase 3 of the unified filament consumption tracker plan. The tracker
 * routes per-tool deltas via PrinterState::get_extruder_filament_used_subject(idx, lifetime);
 * this file locks down the accessor's contract: non-null return for idx 0-3,
 * and status-update propagation from Klipper's extruder<n>.filament_used field.
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"
#include "ui_update_queue.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using json = nlohmann::json;

TEST_CASE("PrinterState: per-extruder filament_used subject returns non-null for idx 0-3",
          "[printer_state][filament_used]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SubjectLifetime lifetime;
    for (int i = 0; i < 4; ++i) {
        auto* subj = state.get_extruder_filament_used_subject(i, lifetime);
        REQUIRE(subj != nullptr);
        REQUIRE(static_cast<bool>(lifetime));
    }
}

TEST_CASE("PrinterState: per-extruder subject reflects status update for extruder1",
          "[printer_state][filament_used]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SubjectLifetime lifetime;
    auto* subj = state.get_extruder_filament_used_subject(1, lifetime);
    REQUIRE(subj != nullptr);

    // Simulate a Klipper status update for extruder1 (key matches Klipper object name).
    json status;
    status["extruder1"]["filament_used"] = 123.4;
    state.update_from_status(status);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(lv_subject_get_int(subj) == 123);
}

TEST_CASE("PrinterState: extruder idx 0 maps to Klipper 'extruder' key",
          "[printer_state][filament_used]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SubjectLifetime lifetime;
    auto* subj0 = state.get_extruder_filament_used_subject(0, lifetime);
    REQUIRE(subj0 != nullptr);

    json status;
    status["extruder"]["filament_used"] = 42.0;
    state.update_from_status(status);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(lv_subject_get_int(subj0) == 42);
}

TEST_CASE("PrinterState: per-extruder subjects are independent",
          "[printer_state][filament_used]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SubjectLifetime lt0, lt2;
    auto* subj0 = state.get_extruder_filament_used_subject(0, lt0);
    auto* subj2 = state.get_extruder_filament_used_subject(2, lt2);
    REQUIRE(subj0 != nullptr);
    REQUIRE(subj2 != nullptr);
    REQUIRE(subj0 != subj2);

    json status;
    status["extruder"]["filament_used"] = 100.0;
    status["extruder2"]["filament_used"] = 250.0;
    state.update_from_status(status);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(lv_subject_get_int(subj0) == 100);
    REQUIRE(lv_subject_get_int(subj2) == 250);
}
