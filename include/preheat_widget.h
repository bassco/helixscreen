// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "panel_widget.h"

#include <memory>
#include <string>

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

    void handle_apply();
    void handle_selection_changed();
    void update_button_label();

    void handle_nozzle_tap();
    void handle_bed_tap();

  public:
    static void preheat_apply_cb(lv_event_t* e);
    static void preheat_changed_cb(lv_event_t* e);
    static void nozzle_tap_cb(lv_event_t* e);
    static void bed_tap_cb(lv_event_t* e);
};

} // namespace helix
