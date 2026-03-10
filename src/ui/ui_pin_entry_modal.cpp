// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_pin_entry_modal.cpp
 * @brief PIN entry modal implementation — numeric keypad for PIN set/change/remove flows.
 */

#include "ui_pin_entry_modal.h"

#include "theme_manager.h"
#include "ui_callback_helpers.h"
#include "ui_event_safety.h"

#include <lvgl.h>
#include <spdlog/spdlog.h>

// ============================================================================
// Static state
// ============================================================================

PinEntryModal* PinEntryModal::g_active_modal = nullptr;

// ============================================================================
// Public static API
// ============================================================================

void PinEntryModal::show_pin_entry(const std::string& heading, PinCallback on_complete) {
    spdlog::debug("[PinEntryModal] show_pin_entry: heading='{}'", heading);

    // Dismiss any active modal first
    if (g_active_modal) {
        spdlog::warn("[PinEntryModal] Replacing active modal without callback");
        g_active_modal->destroy();
        delete g_active_modal;
        g_active_modal = nullptr;
    }

    g_active_modal = new PinEntryModal(heading, std::move(on_complete));
    g_active_modal->create();
}

void PinEntryModal::dismiss() {
    if (!g_active_modal) {
        return;
    }
    spdlog::debug("[PinEntryModal] dismiss()");
    auto* modal = g_active_modal;
    g_active_modal = nullptr;
    if (modal->on_complete_) {
        modal->on_complete_("");
    }
    modal->destroy();
    delete modal;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

PinEntryModal::PinEntryModal(const std::string& heading, PinCallback on_complete)
    : heading_(heading), on_complete_(std::move(on_complete)) {
    spdlog::trace("[PinEntryModal] Created for '{}'", heading_);
}

PinEntryModal::~PinEntryModal() {
    spdlog::trace("[PinEntryModal] Destroyed");
}

// ============================================================================
// UI Creation / Destruction
// ============================================================================

void PinEntryModal::create() {
    lv_obj_t* screen = lv_screen_active();
    if (!screen) {
        spdlog::error("[PinEntryModal] No active screen, cannot create modal");
        return;
    }

    // Semi-transparent backdrop (blocks touches to underlying UI)
    backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(backdrop_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(backdrop_, theme_manager_get_color("screen_bg"), 0);
    lv_obj_set_style_bg_opa(backdrop_, 100, 0);
    lv_obj_set_style_border_width(backdrop_, 0, 0);
    lv_obj_set_style_pad_all(backdrop_, 0, 0);
    lv_obj_set_style_radius(backdrop_, 0, 0);
    lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(backdrop_);

    // Create the dialog from XML component
    dialog_ = static_cast<lv_obj_t*>(lv_xml_create(backdrop_, "pin_entry_modal", nullptr));
    if (!dialog_) {
        spdlog::error("[PinEntryModal] Failed to create pin_entry_modal from XML");
        lv_obj_delete(backdrop_);
        backdrop_ = nullptr;
        return;
    }

    // Set heading text
    lv_obj_t* heading_lbl = lv_obj_find_by_name(dialog_, "pin_heading");
    if (heading_lbl) {
        lv_label_set_text(heading_lbl, heading_.c_str());
    }

    // Reset state
    clear_digits();

    spdlog::debug("[PinEntryModal] Created for '{}'", heading_);
}

void PinEntryModal::destroy() {
    if (backdrop_) {
        lv_obj_delete(backdrop_);
        backdrop_ = nullptr;
        dialog_ = nullptr;
    }
    digit_buffer_.clear();
    spdlog::debug("[PinEntryModal] Destroyed");
}

// ============================================================================
// Digit input handling
// ============================================================================

void PinEntryModal::on_digit(int digit) {
    if (!dialog_) {
        return;
    }
    if (static_cast<int>(digit_buffer_.size()) >= kMaxDigits) {
        spdlog::debug("[PinEntryModal] Max digits reached, ignoring");
        return;
    }

    digit_buffer_ += static_cast<char>('0' + digit);
    spdlog::debug("[PinEntryModal] Digit entered, buffer length={}", digit_buffer_.size());
    update_dots();
    hide_error();

    // Auto-submit at max digits
    if (static_cast<int>(digit_buffer_.size()) == kMaxDigits) {
        on_confirm();
    }
}

void PinEntryModal::on_backspace() {
    if (!dialog_ || digit_buffer_.empty()) {
        return;
    }
    digit_buffer_.pop_back();
    spdlog::debug("[PinEntryModal] Backspace, buffer length={}", digit_buffer_.size());
    update_dots();
    hide_error();
}

void PinEntryModal::on_confirm() {
    if (!dialog_) {
        return;
    }
    if (static_cast<int>(digit_buffer_.size()) < kMinDigits) {
        spdlog::debug("[PinEntryModal] PIN too short ({} of {} required), ignoring",
                      digit_buffer_.size(), kMinDigits);
        return;
    }

    std::string pin = digit_buffer_;
    spdlog::debug("[PinEntryModal] Confirm with {} digit PIN", pin.size());

    // Complete — hand off to caller
    auto callback = std::move(on_complete_);
    on_complete_ = nullptr;

    auto* modal = this;
    g_active_modal = nullptr;
    modal->destroy();
    delete modal;

    if (callback) {
        callback(pin);
    }
}

void PinEntryModal::on_cancel() {
    spdlog::debug("[PinEntryModal] Cancel");

    auto callback = std::move(on_complete_);
    on_complete_ = nullptr;

    auto* modal = this;
    g_active_modal = nullptr;
    modal->destroy();
    delete modal;

    if (callback) {
        callback("");
    }
}

// ============================================================================
// Dot indicator updates
// ============================================================================

void PinEntryModal::update_dots() {
    if (!dialog_) {
        return;
    }

    lv_color_t primary_color = theme_manager_get_color("primary_color");
    lv_color_t muted_color = theme_manager_get_color("text_muted");

    for (int i = 0; i < kMaxDigits; i++) {
        char name[16];
        snprintf(name, sizeof(name), "pin_dot_%d", i);
        lv_obj_t* dot = lv_obj_find_by_name(dialog_, name);
        if (!dot) {
            continue;
        }
        if (i < static_cast<int>(digit_buffer_.size())) {
            lv_obj_set_style_bg_color(dot, primary_color, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        } else {
            lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_color(dot, muted_color, 0);
            lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
        }
    }
}

void PinEntryModal::clear_digits() {
    digit_buffer_.clear();
    update_dots();
    hide_error();
}

// ============================================================================
// Error label
// ============================================================================

void PinEntryModal::show_error(const char* text) {
    if (!dialog_) {
        return;
    }
    lv_obj_t* label = lv_obj_find_by_name(dialog_, "pin_error_label");
    if (label) {
        if (text) {
            lv_label_set_text(label, text);
        }
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void PinEntryModal::hide_error() {
    if (!dialog_) {
        return;
    }
    lv_obj_t* label = lv_obj_find_by_name(dialog_, "pin_error_label");
    if (label) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================================================
// Static XML event callbacks
// ============================================================================

void PinEntryModal::on_digit_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("pin_modal_digit_clicked");
    if (!g_active_modal) {
        return;
    }

    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!btn) {
        return;
    }

    // Button names are "pin_digit_0" through "pin_digit_9"
    const char* name = lv_obj_get_name(btn);
    if (!name) {
        return;
    }

    // Parse digit from trailing character: "pin_digit_5" → 5
    size_t len = strlen(name);
    if (len >= 1 && name[len - 1] >= '0' && name[len - 1] <= '9') {
        int digit = name[len - 1] - '0';
        g_active_modal->on_digit(digit);
    } else {
        spdlog::warn("[PinEntryModal] Could not parse digit from button name '{}'", name);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PinEntryModal::on_backspace_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("pin_modal_backspace_clicked");
    if (g_active_modal) {
        g_active_modal->on_backspace();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PinEntryModal::on_confirm_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("pin_modal_confirm_clicked");
    if (g_active_modal) {
        g_active_modal->on_confirm();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PinEntryModal::on_cancel_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("pin_modal_cancel_clicked");
    if (g_active_modal) {
        g_active_modal->on_cancel();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Callback registration
// ============================================================================

void PinEntryModal::register_callbacks() {
    register_xml_callbacks({
        {"pin_modal_digit_clicked",     PinEntryModal::on_digit_clicked},
        {"pin_modal_backspace_clicked", PinEntryModal::on_backspace_clicked},
        {"pin_modal_confirm_clicked",   PinEntryModal::on_confirm_clicked},
        {"pin_modal_cancel_clicked",    PinEntryModal::on_cancel_clicked},
    });
    spdlog::debug("[PinEntryModal] Callbacks registered");
}
