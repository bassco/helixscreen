// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/pid_progress_tracker.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("PidProgressTracker heating phase detection", "[pid_progress]") {
    PidProgressTracker tracker;
    tracker.start(PidProgressTracker::Heater::EXTRUDER, 200, 25.0f);

    REQUIRE(tracker.phase() == PidProgressTracker::Phase::HEATING);

    // Simulate temperature climbing
    tracker.on_temperature(50.0f, 1000);
    REQUIRE(tracker.phase() == PidProgressTracker::Phase::HEATING);
    REQUIRE(tracker.progress_percent() > 0);
    REQUIRE(tracker.progress_percent() < 40);

    // Overshoot past target
    tracker.on_temperature(210.0f, 60000);
    REQUIRE(tracker.phase() == PidProgressTracker::Phase::HEATING);
    // Still heating — haven't crossed back down yet

    // Cross back below target — oscillation begins
    tracker.on_temperature(195.0f, 75000);
    REQUIRE(tracker.phase() == PidProgressTracker::Phase::OSCILLATING);
    REQUIRE(tracker.progress_percent() >= 40);
}

TEST_CASE("PidProgressTracker oscillation counting", "[pid_progress]") {
    PidProgressTracker tracker;
    tracker.start(PidProgressTracker::Heater::EXTRUDER, 200, 25.0f);

    // Fast-forward through heating phase
    tracker.on_temperature(210.0f, 30000); // overshoot
    tracker.on_temperature(195.0f, 45000); // first downward crossing -> oscillating

    REQUIRE(tracker.oscillation_count() == 1);

    // Simulate 4 more oscillation cycles (up-down pairs)
    uint32_t t = 45000;
    for (int i = 0; i < 4; i++) {
        t += 15000;
        tracker.on_temperature(205.0f, t); // above target
        t += 15000;
        tracker.on_temperature(195.0f, t); // below target — downward crossing
    }

    REQUIRE(tracker.oscillation_count() == 5);
    // After 5 cycles, progress should be at or near 95%
    REQUIRE(tracker.progress_percent() >= 93);
    REQUIRE(tracker.progress_percent() <= 95);
}

TEST_CASE("PidProgressTracker heating ETA calculation", "[pid_progress]") {
    PidProgressTracker tracker;
    tracker.start(PidProgressTracker::Heater::EXTRUDER, 200, 25.0f);

    // After 10 seconds, temp went from 25 to 50 (1 deg/sec)
    tracker.on_temperature(50.0f, 10000);

    // Remaining: 150 degrees at 1 deg/sec = 150s heating + default oscillation time
    auto eta = tracker.eta_seconds();
    REQUIRE(eta.has_value());
    // Heating remaining should be ~150s, plus oscillation estimate
    REQUIRE(*eta > 140);
    REQUIRE(*eta < 300); // heating + oscillation
}

TEST_CASE("PidProgressTracker oscillation ETA", "[pid_progress]") {
    PidProgressTracker tracker;
    tracker.start(PidProgressTracker::Heater::EXTRUDER, 200, 25.0f);

    // Enter oscillation phase
    tracker.on_temperature(210.0f, 30000);
    tracker.on_temperature(195.0f, 45000);

    // Complete one full cycle (30 seconds per cycle)
    tracker.on_temperature(205.0f, 60000);
    tracker.on_temperature(195.0f, 75000); // cycle 2 complete at 75s

    // Measured cycle period: 30 seconds (75000 - 45000) / 1 completed cycle after first
    // Remaining: 3 cycles * 30s = 90s
    auto eta = tracker.eta_seconds();
    REQUIRE(eta.has_value());
    REQUIRE(*eta >= 80);
    REQUIRE(*eta <= 100);
}
