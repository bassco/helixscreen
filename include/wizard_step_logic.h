// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

/// Flags indicating which wizard steps are skipped
struct WizardSkipFlags {
    bool touch_cal = false;        // step 0
    bool language = false;         // step 1
    bool wifi = false;             // step 2
    bool connection = false;       // step 3
    bool printer_identify = false; // step 4
    bool heater_select = false;    // step 5
    bool fan_select = false;       // step 6
    bool ams = false;              // step 7
    bool led = false;              // step 8
    bool filament = false;         // step 9
    bool input_shaper = false;     // step 10
    bool summary = false;          // step 11
    bool telemetry = false;        // step 12
};

/// Calculate display step number from internal step, accounting for skips.
/// Returns the 1-based display step number.
int wizard_calculate_display_step(int internal_step, const WizardSkipFlags& skips);

/// Calculate total display steps, accounting for skips.
int wizard_calculate_display_total(const WizardSkipFlags& skips);

/// Find the next non-skipped step going forward from current.
/// Returns the next valid internal step, or -1 if at end.
int wizard_next_step(int current, const WizardSkipFlags& skips);

/// Find the previous non-skipped step going backward from current.
/// Returns the previous valid internal step, or -1 if at beginning.
int wizard_prev_step(int current, const WizardSkipFlags& skips);

} // namespace helix
