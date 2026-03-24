// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "panel_widget.h"
#include "ui_observer_guard.h"

#include <memory>
#include <string>

class MoonrakerAPI;

namespace helix {
class PrinterState;

class PreheatWidget : public PanelWidget {
  public:
    explicit PreheatWidget(PrinterState& printer_state);
    ~PreheatWidget() override;

    void set_config(const nlohmann::json& config) override;
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override { return "preheat"; }

  private:
    PrinterState& printer_state_;
    nlohmann::json config_;

    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    lv_obj_t* split_btn_ = nullptr;

    int selected_material_ = 0; // 0=PLA, 1=PETG, 2=ABS, 3=TPU
    bool heaters_active_ = false;

    // Observers for heater target temperatures
    ObserverGuard extruder_target_obs_;
    ObserverGuard bed_target_obs_;
    int cached_extruder_target_ = 0;
    int cached_bed_target_ = 0;

    void handle_apply();
    void handle_cooldown();
    void handle_selection_changed();
    void update_button_label();
    void update_heater_state();
    void set_temperatures(MoonrakerAPI* api, int nozzle, int bed);

    void handle_nozzle_tap();
    void handle_bed_tap();

  public:
    static void preheat_apply_cb(lv_event_t* e);
    static void preheat_changed_cb(lv_event_t* e);
    static void nozzle_tap_cb(lv_event_t* e);
    static void bed_tap_cb(lv_event_t* e);
};

} // namespace helix
