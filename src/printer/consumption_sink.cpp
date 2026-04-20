// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "consumption_sink.h"

#include "ams_state.h"
#include "ams_types.h"
#include "filament_database.h"

#include "lvgl/lvgl.h"

#include <cmath>
#include <spdlog/spdlog.h>

namespace helix {

namespace {
constexpr uint32_t kPersistIntervalMs = 60'000;
constexpr float kDeltaWriteThresholdG = 0.05f;
constexpr float kRebaselineThresholdG = 0.5f;
} // namespace

std::string_view ExternalSpoolSink::name() const {
    return "external";
}

bool ExternalSpoolSink::is_trackable() const {
    return active_;
}

uint32_t ExternalSpoolSink::persist_interval_ms() const {
    return persist_interval_override_ms_ ? persist_interval_override_ms_
                                         : kPersistIntervalMs;
}

void ExternalSpoolSink::snapshot(float filament_used_mm) {
    active_ = false;
    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) {
        spdlog::debug("[ConsumptionSink:external] No external spool; skipping");
        return;
    }
    const auto& info = *info_opt;
    if (info.remaining_weight_g <= 0.0f) {
        spdlog::debug(
            "[ConsumptionSink:external] Unknown/empty remaining weight; skipping");
        return;
    }
    auto material = filament::find_material(info.material);
    if (!material.has_value() || material->density_g_cm3 <= 0.0f) {
        spdlog::warn(
            "[ConsumptionSink:external] Cannot resolve density for material '{}'; "
            "consumption tracking disabled for this print",
            info.material);
        return;
    }
    density_g_cm3_ = material->density_g_cm3;
    diameter_mm_ = 1.75f;
    snapshot_mm_ = filament_used_mm;
    snapshot_weight_g_ = info.remaining_weight_g;
    last_written_weight_g_ = info.remaining_weight_g;
    last_persist_tick_ms_ = lv_tick_get();
    active_ = true;
    spdlog::info(
        "[ConsumptionSink:external] Snapshot: material={}, density={} g/cm3, "
        "weight={} g, filament_used_mm={}",
        info.material, density_g_cm3_, snapshot_weight_g_, snapshot_mm_);
}

void ExternalSpoolSink::apply_delta(float filament_used_mm) {
    if (!active_) {
        return;
    }

    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) {
        return;
    }
    SlotInfo info = *info_opt;

    // External-write detection: someone other than us updated remaining_weight_g.
    // Treat as authoritative and rebase our snapshot from it.
    if (std::abs(info.remaining_weight_g - last_written_weight_g_) >
        kRebaselineThresholdG) {
        spdlog::info(
            "[ConsumptionSink:external] External write detected (was {} g, now "
            "{} g); rebaselining",
            last_written_weight_g_, info.remaining_weight_g);
        rebaseline(filament_used_mm);
        return;
    }

    float consumed_mm = filament_used_mm - snapshot_mm_;
    if (consumed_mm < 0.0f) {
        // filament_used was reset under us (e.g. new print). Rebase.
        snapshot_mm_ = filament_used_mm;
        snapshot_weight_g_ = info.remaining_weight_g;
        last_written_weight_g_ = info.remaining_weight_g;
        return;
    }

    float consumed_g =
        filament::length_to_weight_g(consumed_mm, density_g_cm3_, diameter_mm_);
    float new_remaining_g = snapshot_weight_g_ - consumed_g;
    if (new_remaining_g < 0.0f) {
        new_remaining_g = 0.0f;
    }

    // Avoid noise writes for sub-gram changes the UI can't show anyway.
    if (std::abs(new_remaining_g - info.remaining_weight_g) <
        kDeltaWriteThresholdG) {
        return;
    }

    info.remaining_weight_g = new_remaining_g;
    AmsState::instance().set_external_spool_info_in_memory(info);

    // Throttled disk persist (crash-safety).
    if (lv_tick_elaps(last_persist_tick_ms_) >= persist_interval_ms()) {
        AmsState::instance().set_external_spool_info(info);
        last_persist_tick_ms_ = lv_tick_get();
    }
    last_written_weight_g_ = new_remaining_g;
}

void ExternalSpoolSink::flush() {
    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) {
        return;
    }
    // Full write updates settings.json via SettingsManager and re-fires the subject.
    AmsState::instance().set_external_spool_info(*info_opt);
    last_written_weight_g_ = info_opt->remaining_weight_g;
    last_persist_tick_ms_ = lv_tick_get();
}

void ExternalSpoolSink::rebaseline(float filament_used_mm) {
    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) {
        active_ = false;
        return;
    }
    snapshot_mm_ = filament_used_mm;
    snapshot_weight_g_ = info_opt->remaining_weight_g;
    last_written_weight_g_ = info_opt->remaining_weight_g;
}

} // namespace helix
