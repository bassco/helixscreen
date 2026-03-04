// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"

#include <string>

namespace helix {

/**
 * @brief Domain-specific manager for label printer settings
 *
 * Owns label printer configuration and persistence:
 * - printer_address (IP/hostname of Brother QL printer)
 * - printer_port (raw socket port, default 9100)
 * - label_size_index (index into supported label sizes)
 * - label_preset (0=Standard, 1=Compact, 2=Minimal)
 * - printer_configured subject (1 if address non-empty)
 *
 * Thread safety: Single-threaded, main LVGL thread only.
 */
class LabelPrinterSettingsManager {
  public:
    static LabelPrinterSettingsManager& instance();

    // Non-copyable
    LabelPrinterSettingsManager(const LabelPrinterSettingsManager&) = delete;
    LabelPrinterSettingsManager& operator=(const LabelPrinterSettingsManager&) = delete;

    /** @brief Initialize LVGL subjects and load from Config */
    void init_subjects();

    /** @brief Deinitialize LVGL subjects (called by StaticSubjectRegistry) */
    void deinit_subjects();

    // =========================================================================
    // GETTERS / SETTERS
    // =========================================================================

    /** @brief Get printer IP address or hostname */
    std::string get_printer_address() const;

    /** @brief Set printer address (updates configured subject + persists) */
    void set_printer_address(const std::string& addr);

    /** @brief Get raw socket port (default 9100) */
    int get_printer_port() const;

    /** @brief Set raw socket port (persists to config) */
    void set_printer_port(int port);

    /** @brief Get label size index (into supported sizes list) */
    int get_label_size_index() const;

    /** @brief Set label size index (persists to config) */
    void set_label_size_index(int index);

    /** @brief Get label preset (0=Standard, 1=Compact, 2=Minimal) */
    int get_label_preset() const;

    /** @brief Set label preset (persists to config) */
    void set_label_preset(int preset);

    /** @brief True if printer address is non-empty */
    bool is_configured() const;

    // =========================================================================
    // SUBJECT ACCESSORS (for XML binding)
    // =========================================================================

    /** @brief Printer configured subject (integer: 0=not configured, 1=configured) */
    lv_subject_t* subject_printer_configured() {
        return &printer_configured_subject_;
    }

  private:
    LabelPrinterSettingsManager();
    ~LabelPrinterSettingsManager() = default;

    SubjectManager subjects_;

    lv_subject_t printer_configured_subject_; // int: 0/1

    bool subjects_initialized_ = false;
};

} // namespace helix
