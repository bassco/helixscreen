// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_runout_guidance_modal.h"

#include "panel_widget.h"
#include "print_history_manager.h"

#include <atomic>
#include <memory>
#include <string>

namespace helix {

class PrinterState;
enum class PrintJobState;

class PrintStatusWidget : public PanelWidget {
  public:
    PrintStatusWidget();
    ~PrintStatusWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    const char* id() const override {
        return "print_status";
    }

    /// Re-check runout condition after wizard completion
    void trigger_idle_runout_check();

    /// XML event callback — opens print status panel or file browser
    static void print_card_clicked_cb(lv_event_t* e);

    /// Library row callbacks
    static void library_files_cb(lv_event_t* e);
    static void library_last_cb(lv_event_t* e);
    static void library_recent_cb(lv_event_t* e);

  private:
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;

    // Cached widget references (looked up after XML creation)
    lv_obj_t* print_card_thumb_ = nullptr;        // Idle state thumbnail
    lv_obj_t* print_card_active_thumb_ = nullptr; // Active print thumbnail
    lv_obj_t* print_card_layout_ = nullptr;       // Row/column layout container
    lv_obj_t* print_card_thumb_wrap_ = nullptr;   // Thumbnail wrapper
    lv_obj_t* print_card_info_ = nullptr;         // Info section (filename/progress)

    // Library idle state widgets
    lv_obj_t* print_card_idle_ = nullptr;         // Full library idle card
    lv_obj_t* print_card_idle_compact_ = nullptr; // Compact idle card (1x2)
    lv_obj_t* print_card_thumb_compact_ = nullptr; // Compact thumbnail
    lv_obj_t* library_row_last_ = nullptr;        // Print Last row (for graying out)
    lv_obj_t* compact_row_last_ = nullptr;        // Compact Print Last row (for graying out)

    // Compact mode and state tracking
    bool is_compact_ = false;
    bool last_print_available_ = false;

    // PrinterState reference for subject access
    PrinterState& printer_state_;

    // Observers (RAII cleanup via ObserverGuard)
    ObserverGuard print_state_observer_;
    ObserverGuard print_progress_observer_;
    ObserverGuard print_time_left_observer_;
    ObserverGuard print_thumbnail_path_observer_;
    ObserverGuard filament_runout_observer_;

    // Alive guard for async thumbnail callbacks and history observer [L072]
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(false);

    // History observer for updating idle thumbnail when history loads
    helix::HistoryChangedCallback history_changed_cb_;

    // Filament runout modal
    RunoutGuidanceModal runout_modal_;
    bool runout_modal_shown_ = false;

    // Print card update methods
    [[nodiscard]] std::string get_last_print_thumbnail_path() const;
    void handle_print_card_clicked();
    void on_print_state_changed(PrintJobState state);
    void on_print_progress_or_time_changed();
    void on_print_thumbnail_path_changed(const char* path);
    void update_print_card_from_state();
    void update_print_card_label(int progress, int time_left_secs);
    void reset_print_card_to_idle();
    void update_idle_compact_mode();
    void update_last_print_availability();

    // Library action handlers
    void handle_library_files();
    void handle_library_last();
    void handle_library_recent();

    // Filament runout handling
    void check_and_show_idle_runout_modal();
    void show_idle_runout_modal();

};

} // namespace helix
