// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "overlay_base.h"

namespace helix::settings {

class LabelPrinterSettingsOverlay : public OverlayBase {
  public:
    LabelPrinterSettingsOverlay();
    ~LabelPrinterSettingsOverlay() override;

    // OverlayBase interface
    void init_subjects() override;
    void register_callbacks() override;
    const char* get_name() const override { return "Label Printer"; }
    void on_activate() override;
    void on_deactivate() override;

    // UI creation
    lv_obj_t* create(lv_obj_t* parent) override;
    void show(lv_obj_t* parent_screen);
    bool is_created() const { return overlay_root_ != nullptr; }

    // Event handlers (public for static callbacks)
    void handle_address_changed();
    void handle_port_changed();
    void handle_label_size_changed(int index);
    void handle_preset_changed(int index);
    void handle_test_print();

  private:
    void init_address_input();
    void init_port_input();
    void init_label_size_dropdown();
    void init_preset_dropdown();

    bool inputs_initialized_ = false; ///< Guard against stacking duplicate event callbacks

    // Static callbacks
    static void on_address_done(lv_event_t* e);
    static void on_port_done(lv_event_t* e);
    static void on_label_size_changed(lv_event_t* e);
    static void on_preset_changed(lv_event_t* e);
    static void on_test_print(lv_event_t* e);
};

LabelPrinterSettingsOverlay& get_label_printer_settings_overlay();

} // namespace helix::settings
