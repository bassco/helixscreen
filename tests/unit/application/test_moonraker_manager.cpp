// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_manager.cpp
 * @brief Unit tests for MoonrakerManager class
 *
 * Tests Moonraker client/API lifecycle, configuration, and notification queue.
 *
 * Note: MoonrakerManager has heavy dependencies (MoonrakerClient, MoonrakerAPI,
 * EmergencyStopOverlay, etc.) that require full LVGL initialization. These tests
 * focus on the configuration interface. Full initialization tests are done as
 * integration tests.
 */

#include "runtime_config.h"

#include "../../catch_amalgamated.hpp"

// ============================================================================
// RuntimeConfig Tests (MoonrakerManager dependency)
// ============================================================================

TEST_CASE("MoonrakerManager uses RuntimeConfig for mock decisions", "[application][config]") {
    RuntimeConfig config;

    SECTION("Default is not mock mode") {
        REQUIRE_FALSE(config.should_mock_moonraker());
        REQUIRE_FALSE(config.should_use_test_files());
    }

    SECTION("Test mode enables mock Moonraker") {
        config.test_mode = true;
        REQUIRE(config.should_mock_moonraker());
        REQUIRE(config.should_use_test_files());
    }

    SECTION("Real Moonraker flag overrides mock") {
        config.test_mode = true;
        config.use_real_moonraker = true;
        REQUIRE_FALSE(config.should_mock_moonraker());
        // Note: should_use_test_files is controlled by use_real_files, not use_real_moonraker
        REQUIRE(config.should_use_test_files());
    }

    SECTION("Real files flag affects API mock") {
        config.test_mode = true;
        config.use_real_files = true;
        REQUIRE_FALSE(config.should_use_test_files());
        REQUIRE(config.should_mock_moonraker()); // Moonraker mock unaffected
    }
}

TEST_CASE("RuntimeConfig simulation speedup", "[application][config]") {
    RuntimeConfig config;

    REQUIRE(config.sim_speedup == 1.0);

    config.sim_speedup = 10.0;
    REQUIRE(config.sim_speedup == 10.0);

    config.sim_speedup = 0.5;
    REQUIRE(config.sim_speedup == 0.5);
}

TEST_CASE("RuntimeConfig mock_auto_start_print flag", "[application][config]") {
    RuntimeConfig config;

    REQUIRE_FALSE(config.mock_auto_start_print);

    config.mock_auto_start_print = true;
    REQUIRE(config.mock_auto_start_print);
}

TEST_CASE("RuntimeConfig mock_auto_history flag", "[application][config]") {
    RuntimeConfig config;

    REQUIRE_FALSE(config.mock_auto_history);

    config.mock_auto_history = true;
    REQUIRE(config.mock_auto_history);
}

TEST_CASE("RuntimeConfig mock_ams_gate_count", "[application][config]") {
    RuntimeConfig config;

    // Default is 4 gates
    REQUIRE(config.mock_ams_gate_count == 4);

    config.mock_ams_gate_count = 8;
    REQUIRE(config.mock_ams_gate_count == 8);
}

// ============================================================================
// Mid-Print Detection Tests (should_start_print_collector)
// ============================================================================
// Tests the logic that prevents "Preparing Print" from showing when the app
// starts while a print is already in progress.

#include "moonraker_manager.h"
#include "printer_state.h"

using namespace helix;

TEST_CASE("should_start_print_collector - fresh print start", "[application][print_start]") {
    // Transition from STANDBY to PRINTING with 0% progress = fresh print start
    REQUIRE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                           PrintJobState::PRINTING, 0, true));
    // Non-initial transitions always start (user explicitly started a print)
    REQUIRE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                           PrintJobState::PRINTING, 0, false));
}

TEST_CASE("should_start_print_collector - mid-print detection (app boot only)",
          "[application][print_start]") {
    // App boots, finds print already running (initial transition with progress > 0)
    // This is the ONLY case where mid-print detection should suppress the collector
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                                 PrintJobState::PRINTING, 1, true));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                                 PrintJobState::PRINTING, 31, true));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                                 PrintJobState::PRINTING, 99, true));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                                 PrintJobState::PRINTING, 100, true));
}

TEST_CASE("should_start_print_collector - reprint after cancel with stale progress",
          "[application][print_start]") {
    // Real flow: CANCELLED → STANDBY → PRINTING with stale progress from old print.
    // The prev_state seen is STANDBY (not CANCELLED). Non-initial transition must
    // start the collector regardless of stale progress.
    REQUIRE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                           PrintJobState::PRINTING, 57, false));
    REQUIRE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                           PrintJobState::PRINTING, 100, false));
    REQUIRE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                           PrintJobState::PRINTING, 1, false));
}

TEST_CASE("should_start_print_collector - already printing", "[application][print_start]") {
    // If already printing, no transition → don't start
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::PRINTING,
                                                                 PrintJobState::PRINTING, 0, false));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::PRINTING,
                                                                 PrintJobState::PRINTING, 50, false));
}

TEST_CASE("should_start_print_collector - paused states", "[application][print_start]") {
    // Transition from PAUSED to PRINTING = resume, not fresh start
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::PAUSED,
                                                                 PrintJobState::PRINTING, 0, false));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::PAUSED,
                                                                 PrintJobState::PRINTING, 50, false));
    // Transition to PAUSED (not PRINTING) = don't start
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                                 PrintJobState::PAUSED, 0, false));
}

TEST_CASE("should_start_print_collector - non-printing transitions", "[application][print_start]") {
    // Transitions that don't involve PRINTING = don't start
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::STANDBY,
                                                                 PrintJobState::COMPLETE, 0, false));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::PRINTING,
                                                                 PrintJobState::COMPLETE, 100, false));
    REQUIRE_FALSE(MoonrakerManager::should_start_print_collector(PrintJobState::PRINTING,
                                                                 PrintJobState::CANCELLED, 50, false));
}
