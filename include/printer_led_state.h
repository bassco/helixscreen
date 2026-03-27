// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>
#include <string>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Manages LED-related subjects for printer state
 *
 * Tracks a single LED object (neopixel, dotstar, etc.) and provides
 * RGBW color values plus derived brightness and on/off state.
 * Extracted from PrinterState as part of god class decomposition.
 */
class PrinterLedState {
  public:
    PrinterLedState() = default;
    ~PrinterLedState() = default;

    // Non-copyable
    PrinterLedState(const PrinterLedState&) = delete;
    PrinterLedState& operator=(const PrinterLedState&) = delete;

#if HELIX_HAS_LED
    void init_subjects(bool register_xml = true);
    void deinit_subjects();
    void update_from_status(const nlohmann::json& status);
    void set_tracked_led(const std::string& led_name);
    std::string get_tracked_led() const { return tracked_led_name_; }
    bool has_tracked_led() const { return !tracked_led_name_.empty(); }

    lv_subject_t* get_led_state_subject() { return &led_state_; }
    lv_subject_t* get_led_r_subject() { return &led_r_; }
    lv_subject_t* get_led_g_subject() { return &led_g_; }
    lv_subject_t* get_led_b_subject() { return &led_b_; }
    lv_subject_t* get_led_w_subject() { return &led_w_; }
    lv_subject_t* get_led_brightness_subject() { return &led_brightness_; }

  private:
    friend class PrinterLedStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;
    std::string tracked_led_name_;

    lv_subject_t led_state_{};
    lv_subject_t led_r_{};
    lv_subject_t led_g_{};
    lv_subject_t led_b_{};
    lv_subject_t led_w_{};
    lv_subject_t led_brightness_{};

#else // !HELIX_HAS_LED — no-op stubs when LED subsystem is excluded
    void init_subjects(bool /*register_xml*/ = true) {}
    void deinit_subjects() {}
    void update_from_status(const nlohmann::json& /*status*/) {}
    void set_tracked_led(const std::string& /*led_name*/) {}
    std::string get_tracked_led() const { return {}; }
    bool has_tracked_led() const { return false; }
    lv_subject_t* get_led_state_subject() { return nullptr; }
    lv_subject_t* get_led_r_subject() { return nullptr; }
    lv_subject_t* get_led_g_subject() { return nullptr; }
    lv_subject_t* get_led_b_subject() { return nullptr; }
    lv_subject_t* get_led_w_subject() { return nullptr; }
    lv_subject_t* get_led_brightness_subject() { return nullptr; }

  private:
#endif // HELIX_HAS_LED
};

} // namespace helix
