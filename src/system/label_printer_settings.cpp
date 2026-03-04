// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "label_printer_settings.h"

#include "config.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"

using namespace helix;

LabelPrinterSettingsManager& LabelPrinterSettingsManager::instance() {
    static LabelPrinterSettingsManager instance;
    return instance;
}

LabelPrinterSettingsManager::LabelPrinterSettingsManager() {
    spdlog::trace("[LabelPrinterSettings] Constructor");
}

void LabelPrinterSettingsManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[LabelPrinterSettings] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[LabelPrinterSettings] Initializing subjects");

    Config* config = Config::get_instance();

    // Configured flag: 1 if address is non-empty
    std::string addr = config->get<std::string>("/label_printer/address", "");
    int configured = addr.empty() ? 0 : 1;
    UI_MANAGED_SUBJECT_INT(printer_configured_subject_, configured, "label_printer_configured",
                           subjects_);

    subjects_initialized_ = true;

    StaticSubjectRegistry::instance().register_deinit(
        "LabelPrinterSettingsManager",
        []() { LabelPrinterSettingsManager::instance().deinit_subjects(); });

    spdlog::debug("[LabelPrinterSettings] Subjects initialized: addr='{}', configured={}",
                  addr, configured);
}

void LabelPrinterSettingsManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[LabelPrinterSettings] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[LabelPrinterSettings] Subjects deinitialized");
}

// =============================================================================
// GETTERS / SETTERS
// =============================================================================

std::string LabelPrinterSettingsManager::get_printer_address() const {
    Config* config = Config::get_instance();
    return config->get<std::string>("/label_printer/address", "");
}

void LabelPrinterSettingsManager::set_printer_address(const std::string& addr) {
    spdlog::info("[LabelPrinterSettings] set_printer_address('{}')", addr);

    Config* config = Config::get_instance();
    config->set<std::string>("/label_printer/address", addr);
    config->save();

    // Update configured subject
    if (subjects_initialized_) {
        lv_subject_set_int(&printer_configured_subject_, addr.empty() ? 0 : 1);
    }
}

int LabelPrinterSettingsManager::get_printer_port() const {
    Config* config = Config::get_instance();
    return config->get<int>("/label_printer/port", 9100);
}

void LabelPrinterSettingsManager::set_printer_port(int port) {
    spdlog::info("[LabelPrinterSettings] set_printer_port({})", port);

    Config* config = Config::get_instance();
    config->set<int>("/label_printer/port", port);
    config->save();
}

int LabelPrinterSettingsManager::get_label_size_index() const {
    Config* config = Config::get_instance();
    return config->get<int>("/label_printer/label_size", 0);
}

void LabelPrinterSettingsManager::set_label_size_index(int index) {
    spdlog::info("[LabelPrinterSettings] set_label_size_index({})", index);

    Config* config = Config::get_instance();
    config->set<int>("/label_printer/label_size", index);
    config->save();
}

int LabelPrinterSettingsManager::get_label_preset() const {
    Config* config = Config::get_instance();
    return config->get<int>("/label_printer/preset", 0);
}

void LabelPrinterSettingsManager::set_label_preset(int preset) {
    spdlog::info("[LabelPrinterSettings] set_label_preset({})", preset);

    Config* config = Config::get_instance();
    config->set<int>("/label_printer/preset", preset);
    config->save();
}

bool LabelPrinterSettingsManager::is_configured() const {
    return !get_printer_address().empty();
}
