// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"
#include "overlay_base.h"

/**
 * @file ui_overlay_console_settings.h
 * @brief Settings overlay for the gcode console panel.
 *
 * Hosts the user-facing toggles that control console output filtering:
 *   - "Hide Temperature Reports" — drops periodic `T:.../B:...` status lines.
 *   - "Hide Firmware Noise"      — drops raw debug output emitted by Creality
 *                                   K2 / FlashForge / similar firmware modules.
 *
 * Toggle state is owned by `helix::SettingsManager` (see its
 * `console_filter_*_subject_` fields) — this class is a thin XML-bound view
 * that forwards switch events to the manager.
 */
class ConsoleSettingsOverlay : public OverlayBase {
  public:
    ConsoleSettingsOverlay();
    ~ConsoleSettingsOverlay() override = default;

    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    [[nodiscard]] const char* get_name() const override {
        return "Console Settings";
    }

    [[nodiscard]] const char* get_xml_component_name() const {
        return "console_settings_overlay";
    }
};

ConsoleSettingsOverlay& get_global_console_settings();
void init_global_console_settings();
