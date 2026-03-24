// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_loading_error_modal.h"

#include "ui_callback_helpers.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
bool AmsLoadingErrorModal::callbacks_registered_ = false;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsLoadingErrorModal::AmsLoadingErrorModal() {
    spdlog::debug("[AmsLoadingErrorModal] Constructed");
}

AmsLoadingErrorModal::~AmsLoadingErrorModal() {
    spdlog::trace("[AmsLoadingErrorModal] Destroyed");
}

// ============================================================================
// Public API
// ============================================================================

bool AmsLoadingErrorModal::show(lv_obj_t* parent, const std::string& error_message,
                                RetryCallback retry_callback) {
    return show(parent, error_message, "Check the filament path and try again.", retry_callback);
}

bool AmsLoadingErrorModal::show(lv_obj_t* parent, const std::string& error_message,
                                const std::string& hint_message, RetryCallback retry_callback) {
    // Register callbacks once (idempotent)
    register_callbacks();

    // Store state
    error_message_ = error_message;
    hint_message_ = hint_message;
    retry_callback_ = std::move(retry_callback);

    // Show the modal via Modal base class
    if (!Modal::show(parent)) {
        return false;
    }

    // Store 'this' in modal's user_data for callback traversal
    lv_obj_set_user_data(dialog_, this);

    spdlog::info("[AmsLoadingErrorModal] Shown with message: {}", error_message_);
    return true;
}

// ============================================================================
// Modal Hooks
// ============================================================================

void AmsLoadingErrorModal::on_show() {
    // Update error message label
    lv_obj_t* message_label = find_widget("error_message");
    if (message_label) {
        lv_label_set_text(message_label, error_message_.c_str());
    }

    // Update hint message label
    lv_obj_t* hint_label = find_widget("error_hint");
    if (hint_label) {
        lv_label_set_text(hint_label, hint_message_.c_str());
    }
}

void AmsLoadingErrorModal::on_hide() {
    // Fire dismiss callback for all close paths EXCEPT Retry.
    // This catches: Close button, X button, Cancel, backdrop click, ESC key.
    // The dismiss callback clears AFC error state (RESET_FAILURE + AFC_CLEAR_MESSAGE)
    // so the error dialog doesn't reappear immediately.
    if (!retry_in_progress_ && dismiss_callback_) {
        spdlog::debug("[AmsLoadingErrorModal] Firing dismiss callback");
        dismiss_callback_();
    }
    retry_in_progress_ = false;
    spdlog::debug("[AmsLoadingErrorModal] on_hide()");
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsLoadingErrorModal::handle_close() {
    spdlog::debug("[AmsLoadingErrorModal] Close requested");
    hide();
}

void AmsLoadingErrorModal::handle_cancel() {
    spdlog::debug("[AmsLoadingErrorModal] Cancel requested");
    hide();
}

void AmsLoadingErrorModal::handle_retry() {
    spdlog::info("[AmsLoadingErrorModal] Retry requested");

    // Mark retry so on_hide() doesn't fire the dismiss callback
    retry_in_progress_ = true;

    // Invoke retry callback
    if (retry_callback_) {
        retry_callback_();
    }

    hide();
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsLoadingErrorModal::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    register_xml_callbacks({
        {"ams_loading_error_close_cb", on_close_cb},
        {"ams_loading_error_cancel_cb", on_cancel_cb},
        {"ams_loading_error_retry_cb", on_retry_cb},
    });

    callbacks_registered_ = true;
    spdlog::debug("[AmsLoadingErrorModal] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via User Data)
// ============================================================================

AmsLoadingErrorModal* AmsLoadingErrorModal::get_instance_from_event(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Find modal root by name, then get instance from its user_data.
    // Cannot walk parents checking any user_data — ui_button and other widgets
    // set their own user_data, which would be miscast (prestonbrown/helixscreen#195).
    lv_obj_t* obj = target;
    while (obj) {
        const char* name = lv_obj_get_name(obj);
        if (name && strcmp(name, "ams_loading_error_modal") == 0) {
            void* user_data = lv_obj_get_user_data(obj);
            if (user_data) {
                return static_cast<AmsLoadingErrorModal*>(user_data);
            }
        }
        obj = lv_obj_get_parent(obj);
    }

    spdlog::warn("[AmsLoadingErrorModal] Could not find instance from event target");
    return nullptr;
}

void AmsLoadingErrorModal::on_close_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_close();
    }
}

void AmsLoadingErrorModal::on_cancel_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_cancel();
    }
}

void AmsLoadingErrorModal::on_retry_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_retry();
    }
}

} // namespace helix::ui
