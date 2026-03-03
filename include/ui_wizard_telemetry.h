// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

/**
 * @class WizardTelemetryStep
 * @brief Dedicated telemetry opt-in step for preset wizard mode
 *
 * Provides a prominent, centered telemetry opt-in screen shown only
 * in preset mode where the summary step is skipped. Reuses the same
 * telemetry toggle and info modal as the summary step.
 */
class WizardTelemetryStep {
  public:
    void init_subjects();
    void register_callbacks();
    lv_obj_t* create(lv_obj_t* parent);
    void cleanup();
    bool is_validated() const { return true; }
    const char* get_name() const { return "WizardTelemetry"; }
    bool should_skip() const;

  private:
    lv_obj_t* root_ = nullptr;

    // Telemetry info modal text subject
    lv_subject_t telemetry_info_text_;
    char telemetry_info_text_buffer_[2048];
    bool subjects_initialized_ = false;

    static void on_wizard_telemetry_changed(lv_event_t* e);
    static void on_wizard_telemetry_info(lv_event_t* e);
};

WizardTelemetryStep* get_wizard_telemetry_step();
void destroy_wizard_telemetry_step();
