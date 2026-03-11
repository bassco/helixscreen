// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_android_platform.cpp
 * @brief Unit tests for Android platform detection and wizard step logic
 *
 * Tests the runtime-overridable platform detection and the extracted
 * wizard step counting logic used for Android conditionalization.
 */

#include "platform_info.h"
#include "wizard_step_logic.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Platform Detection Tests
// ============================================================================

TEST_CASE("Platform detection defaults to non-Android on macOS/Linux", "[android][platform]") {
    // Reset any previous override
    set_platform_override(-1);

    // On test builds (macOS/Linux), __ANDROID__ is not defined
    REQUIRE(is_android_platform() == false);
}

TEST_CASE("Platform override to true makes is_android_platform return true",
          "[android][platform]") {
    set_platform_override(1);
    REQUIRE(is_android_platform() == true);

    // Clean up
    set_platform_override(-1);
}

TEST_CASE("Platform override to false makes is_android_platform return false",
          "[android][platform]") {
    set_platform_override(0);
    REQUIRE(is_android_platform() == false);

    // Clean up
    set_platform_override(-1);
}

TEST_CASE("Platform override reset to -1 restores compile-time default", "[android][platform]") {
    // Force Android
    set_platform_override(1);
    REQUIRE(is_android_platform() == true);

    // Reset to compile-time default
    set_platform_override(-1);
    REQUIRE(is_android_platform() == false);
}

// ============================================================================
// Wizard Step Logic -- Total Steps
// ============================================================================

TEST_CASE("Wizard total steps with no skips is 13", "[android][wizard]") {
    WizardSkipFlags no_skips{};
    REQUIRE(wizard_calculate_display_total(no_skips) == 13);
}

TEST_CASE("Wizard total steps with wifi skipped is 12", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    REQUIRE(wizard_calculate_display_total(skips) == 12);
}

TEST_CASE("Wizard total steps with wifi + touch_cal + language skipped is 11",
          "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    skips.touch_cal = true;
    skips.language = true;
    REQUIRE(wizard_calculate_display_total(skips) == 10);
}

// ============================================================================
// Wizard Step Logic -- Display Step Numbers
//
// wizard_calculate_display_step() returns a 1-based display number.
// It counts non-skipped steps before the given internal step, plus 1.
// ============================================================================

TEST_CASE("Display step calculation with wifi skipped", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;

    // Step 0 (touch_cal): first step -> display 1
    REQUIRE(wizard_calculate_display_step(0, skips) == 1);
    // Step 1 (language): step 0 not skipped -> display 2
    REQUIRE(wizard_calculate_display_step(1, skips) == 2);
    // Step 3 (connection): steps 0,1 not skipped, step 2 skipped -> display 3
    REQUIRE(wizard_calculate_display_step(3, skips) == 3);
    // Step 4: steps 0,1,3 not skipped -> display 4
    REQUIRE(wizard_calculate_display_step(4, skips) == 4);
}

TEST_CASE("Display step at summary (step 12) with wifi skipped is 12", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    // 12 steps before step 12, one skipped (wifi) -> 11 non-skipped + 1 = 12
    REQUIRE(wizard_calculate_display_step(12, skips) == 12);
}

// ============================================================================
// Wizard Step Logic -- Navigation Forward
// ============================================================================

TEST_CASE("wizard_next_step(1, wifi=true) returns 3", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    REQUIRE(wizard_next_step(1, skips) == 3);
}

TEST_CASE("wizard_next_step(2, wifi=true) returns 3", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    // Even if somehow on step 2, next non-skipped is 3
    REQUIRE(wizard_next_step(2, skips) == 3);
}

TEST_CASE("Navigation forward skips all disabled steps correctly", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    skips.ams = true;
    skips.led = true;

    // Forward from step 6: next non-skipped should skip 7 (ams) and 8 (led) -> 9
    REQUIRE(wizard_next_step(6, skips) == 9);
}

TEST_CASE("wizard_next_step returns -1 at end", "[android][wizard]") {
    WizardSkipFlags no_skips{};
    REQUIRE(wizard_next_step(13, no_skips) == -1);
}

// ============================================================================
// Wizard Step Logic -- Navigation Backward
// ============================================================================

TEST_CASE("wizard_prev_step(3, wifi=true) returns 1", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    REQUIRE(wizard_prev_step(3, skips) == 1);
}

TEST_CASE("wizard_prev_step(3, wifi+language=true) returns 0", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    skips.language = true;
    REQUIRE(wizard_prev_step(3, skips) == 0);
}

TEST_CASE("wizard_prev_step(3, wifi+language+touch_cal=true) returns -1", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    skips.language = true;
    skips.touch_cal = true;
    REQUIRE(wizard_prev_step(3, skips) == -1);
}

TEST_CASE("Navigation backward skips all disabled steps correctly", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    skips.ams = true;
    skips.led = true;

    // Backward from step 9: should skip 8 (led) and 7 (ams) -> 6
    REQUIRE(wizard_prev_step(9, skips) == 6);
}

TEST_CASE("wizard_prev_step returns -1 at beginning", "[android][wizard]") {
    WizardSkipFlags no_skips{};
    REQUIRE(wizard_prev_step(0, no_skips) == -1);
}

// ============================================================================
// Multiple Skips -- Display Step Verification
// ============================================================================

TEST_CASE("Multiple skips: wifi + ams + led, display_step at step 9", "[android][wizard]") {
    WizardSkipFlags skips{};
    skips.wifi = true;
    skips.ams = true;
    skips.led = true;

    // Steps before 9: 0,1,2,3,4,5,6,7,8
    // Skipped: 2 (wifi), 7 (ams), 8 (led) = 3 skipped
    // Non-skipped before step 9: 6
    // Display step = 1 + 6 = 7
    REQUIRE(wizard_calculate_display_step(9, skips) == 7);

    // Total: 13 - 3 = 10
    REQUIRE(wizard_calculate_display_total(skips) == 10);
}

// ============================================================================
// Combined Android Scenario
// ============================================================================

TEST_CASE("Android scenario: wifi skipped when platform is Android",
          "[android][platform][wizard]") {
    // Simulate Android platform
    set_platform_override(1);
    REQUIRE(is_android_platform() == true);

    // On Android, wifi step should be skipped
    WizardSkipFlags android_skips{};
    android_skips.wifi = is_android_platform();

    REQUIRE(android_skips.wifi == true);
    REQUIRE(wizard_calculate_display_total(android_skips) == 12);

    // Step after language (1) should be connection (3), not wifi (2)
    REQUIRE(wizard_next_step(1, android_skips) == 3);

    // Clean up
    set_platform_override(-1);
}
