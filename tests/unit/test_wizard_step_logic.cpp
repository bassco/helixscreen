// SPDX-License-Identifier: GPL-3.0-or-later
#include "wizard_step_logic.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Default flags (no skips) — baseline behavior
// ============================================================================

TEST_CASE("Default flags: all 13 steps shown", "[wizard][step_logic]") {
    helix::WizardSkipFlags flags{};
    REQUIRE(helix::wizard_calculate_display_total(flags) == 13);
}

TEST_CASE("Default flags: display step numbering is 1-based sequential",
          "[wizard][step_logic]") {
    helix::WizardSkipFlags flags{};
    for (int i = 0; i < 13; ++i) {
        REQUIRE(helix::wizard_calculate_display_step(i, flags) == i + 1);
    }
}

TEST_CASE("Default flags: next_step walks all steps", "[wizard][step_logic]") {
    helix::WizardSkipFlags flags{};
    for (int i = 0; i < 12; ++i) {
        REQUIRE(helix::wizard_next_step(i, flags) == i + 1);
    }
    REQUIRE(helix::wizard_next_step(12, flags) == -1);
}

TEST_CASE("Default flags: prev_step walks all steps backward",
          "[wizard][step_logic]") {
    helix::WizardSkipFlags flags{};
    for (int i = 12; i > 0; --i) {
        REQUIRE(helix::wizard_prev_step(i, flags) == i - 1);
    }
    REQUIRE(helix::wizard_prev_step(0, flags) == -1);
}

// ============================================================================
// Preset mode: skip hardware steps
// ============================================================================

TEST_CASE("Preset mode: skip hardware steps", "[wizard][step_logic][preset]") {
    helix::WizardSkipFlags flags{};
    flags.wifi = true;
    flags.printer_identify = true;
    flags.heater_select = true;
    flags.fan_select = true;
    flags.ams = true;
    flags.led = true;
    flags.filament = true;
    flags.input_shaper = true;
    flags.summary = true;
    // telemetry NOT skipped (shown in preset mode)

    // Steps shown: 0(touch), 1(lang), 3(conn), 12(telemetry) = 4
    REQUIRE(helix::wizard_calculate_display_total(flags) == 4);
    REQUIRE(helix::wizard_calculate_display_step(0, flags) == 1);
    REQUIRE(helix::wizard_calculate_display_step(1, flags) == 2);
    REQUIRE(helix::wizard_calculate_display_step(3, flags) == 3);
    REQUIRE(helix::wizard_calculate_display_step(12, flags) == 4);
}

TEST_CASE("Preset mode: next_step skips hardware",
          "[wizard][step_logic][preset]") {
    helix::WizardSkipFlags flags{};
    flags.wifi = true;
    flags.printer_identify = true;
    flags.heater_select = true;
    flags.fan_select = true;
    flags.ams = true;
    flags.led = true;
    flags.filament = true;
    flags.input_shaper = true;
    flags.summary = true;

    REQUIRE(helix::wizard_next_step(0, flags) == 1);
    REQUIRE(helix::wizard_next_step(1, flags) == 3);
    REQUIRE(helix::wizard_next_step(3, flags) == 12);
    REQUIRE(helix::wizard_next_step(12, flags) == -1);
}

TEST_CASE("Normal mode: telemetry skipped by default",
          "[wizard][step_logic]") {
    helix::WizardSkipFlags flags{};
    flags.telemetry = true;

    // 12 steps shown (0-11, telemetry skipped)
    REQUIRE(helix::wizard_calculate_display_total(flags) == 12);
    REQUIRE(helix::wizard_next_step(11, flags) == -1); // Summary is last
}

TEST_CASE("Preset mode: prev_step works", "[wizard][step_logic][preset]") {
    helix::WizardSkipFlags flags{};
    flags.wifi = true;
    flags.printer_identify = true;
    flags.heater_select = true;
    flags.fan_select = true;
    flags.ams = true;
    flags.led = true;
    flags.filament = true;
    flags.input_shaper = true;
    flags.summary = true;

    REQUIRE(helix::wizard_prev_step(12, flags) == 3);
    REQUIRE(helix::wizard_prev_step(3, flags) == 1);
    REQUIRE(helix::wizard_prev_step(1, flags) == 0);
    REQUIRE(helix::wizard_prev_step(0, flags) == -1);
}

TEST_CASE("Preset mode: connection also skipped", "[wizard][step_logic][preset]") {
    helix::WizardSkipFlags flags{};
    flags.wifi = true;
    flags.connection = true;  // auto-validated
    flags.printer_identify = true;
    flags.heater_select = true;
    flags.fan_select = true;
    flags.ams = true;
    flags.led = true;
    flags.filament = true;
    flags.input_shaper = true;
    flags.summary = true;
    // telemetry NOT skipped

    // Steps: 0(touch), 1(lang), 12(telemetry) = 3
    REQUIRE(helix::wizard_calculate_display_total(flags) == 3);
    REQUIRE(helix::wizard_next_step(1, flags) == 12);  // lang -> telemetry (skip conn + all hw)
}
