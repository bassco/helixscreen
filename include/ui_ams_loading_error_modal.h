// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"

#include <functional>
#include <string>

namespace helix::ui {

/**
 * @file ui_ams_loading_error_modal.h
 * @brief Modal dialog for displaying AMS loading errors with retry option
 *
 * Shows an error message with Retry and Close buttons. Used when filament
 * loading operations fail (e.g., jam, runout, sensor errors).
 *
 * Uses Modal base class wire_ok_button/wire_cancel_button for safe callback
 * dispatch via per-callback user_data (prestonbrown/helixscreen#735).
 */
class AmsLoadingErrorModal : public Modal {
  public:
    using RetryCallback = std::function<void()>;
    using DismissCallback = std::function<void()>;

    AmsLoadingErrorModal();
    ~AmsLoadingErrorModal() override;

    // Non-copyable
    AmsLoadingErrorModal(const AmsLoadingErrorModal&) = delete;
    AmsLoadingErrorModal& operator=(const AmsLoadingErrorModal&) = delete;

    bool show(lv_obj_t* parent, const std::string& error_message, RetryCallback retry_callback);

    bool show(lv_obj_t* parent, const std::string& error_message, const std::string& hint_message,
              RetryCallback retry_callback);

    /**
     * @brief Set callback for when the modal is dismissed (Close/X/Cancel/backdrop)
     *
     * Called on all close paths EXCEPT Retry (which has its own callback).
     * Use this to clear the backend error state (e.g., send RESET_FAILURE).
     */
    void set_dismiss_callback(DismissCallback callback) {
        dismiss_callback_ = std::move(callback);
    }

    // Modal interface
    [[nodiscard]] const char* get_name() const override {
        return "AMS Loading Error Modal";
    }
    [[nodiscard]] const char* component_name() const override {
        return "ams_loading_error_modal";
    }

  protected:
    void on_show() override;
    void on_hide() override;
    void on_ok() override;
    void on_cancel() override;

  private:
    std::string error_message_;
    std::string hint_message_;
    RetryCallback retry_callback_;
    DismissCallback dismiss_callback_;
    bool retry_in_progress_ = false; ///< Suppresses dismiss callback during retry
};

} // namespace helix::ui
