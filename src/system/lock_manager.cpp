// SPDX-License-Identifier: GPL-3.0-or-later
#include "lock_manager.h"
#include "config.h"
#include "picosha2.h"

#include <spdlog/spdlog.h>

namespace helix {

LockManager& LockManager::instance() {
    static LockManager inst;
    return inst;
}

LockManager::LockManager() {
    load_from_config();
}

bool LockManager::has_pin() const {
    return !pin_hash_.empty();
}

bool LockManager::set_pin(const std::string& pin) {
    if (static_cast<int>(pin.length()) < kMinPinLength ||
        static_cast<int>(pin.length()) > kMaxPinLength) {
        return false;
    }
    pin_hash_ = hash_pin(pin);
    save_to_config();
    spdlog::info("[LockManager] PIN set");
    return true;
}

void LockManager::remove_pin() {
    pin_hash_.clear();
    locked_ = false;
    save_to_config();
    spdlog::info("[LockManager] PIN removed, lock disabled");
}

bool LockManager::verify_pin(const std::string& pin) const {
    if (pin_hash_.empty() || pin.empty()) return false;
    return hash_pin(pin) == pin_hash_;
}

bool LockManager::is_locked() const {
    return locked_;
}

void LockManager::lock() {
    if (!has_pin()) return;
    locked_ = true;
    spdlog::info("[LockManager] Screen locked");
}

bool LockManager::try_unlock(const std::string& pin) {
    if (verify_pin(pin)) {
        locked_ = false;
        spdlog::info("[LockManager] Screen unlocked");
        return true;
    }
    spdlog::debug("[LockManager] Unlock failed — wrong PIN");
    return false;
}

bool LockManager::auto_lock_enabled() const {
    return auto_lock_;
}

void LockManager::set_auto_lock(bool enabled) {
    auto_lock_ = enabled;
    save_to_config();
}

std::string LockManager::hash_pin(const std::string& pin) const {
    return picosha2::hash256_hex_string(pin);
}

void LockManager::load_from_config() {
    auto* config = Config::get_instance();
    if (!config) return;
    pin_hash_ = config->get<std::string>("/security/pin_hash", "");
    auto_lock_ = config->get<bool>("/security/auto_lock", false);
}

void LockManager::save_to_config() {
    auto* config = Config::get_instance();
    if (!config) return;
    config->set<std::string>("/security/pin_hash", pin_hash_);
    config->set<bool>("/security/auto_lock", auto_lock_);
    config->save();
}

void LockManager::init_subjects() {
    // Placeholder for future LVGL subject bindings (e.g., pin_set_subject_,
    // locked_subject_). Subjects will be declared as static locals here once
    // UI panels need reactive binding to lock state.
    if (subjects_initialized_) return;
    subjects_initialized_ = true;
}

} // namespace helix
