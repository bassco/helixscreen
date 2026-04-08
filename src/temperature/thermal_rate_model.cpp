// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thermal_rate_model.h"

#include <algorithm>

void ThermalRateModel::record_sample(float temp_c, uint32_t tick_ms) {
    // Initialize timing on first sample
    if (start_tick_ == 0) {
        start_tick_ = tick_ms;
        last_tick_ = tick_ms;
        last_temp_ = temp_c;
        return;
    }

    float delta_from_last = temp_c - last_temp_;

    if (delta_from_last >= MIN_DELTA_FROM_LAST && tick_ms > last_tick_) {
        float inst_rate = static_cast<float>(tick_ms - last_tick_) / 1000.0f / delta_from_last;

        if (has_measured_heat_rate_) {
            // EMA: blend new instantaneous rate with running estimate
            measured_heat_rate_ = EMA_NEW_WEIGHT * inst_rate + EMA_OLD_WEIGHT * measured_heat_rate_;
        } else if (temp_c - start_temp_ >= MIN_TOTAL_MOVEMENT) {
            // First usable measurement — seed from cumulative rate
            float elapsed_s = static_cast<float>(tick_ms - start_tick_) / 1000.0f;
            measured_heat_rate_ = elapsed_s / (temp_c - start_temp_);
            has_measured_heat_rate_ = true;
        }

        // Update last sample point when we have a significant delta
        last_temp_ = temp_c;
        last_tick_ = tick_ms;
    }
}

float ThermalRateModel::estimate_seconds(float current, float target) const {
    if (current >= target)
        return 0.0f;

    float remaining_degrees = target - current;
    return std::max(0.0f, remaining_degrees * best_rate());
}

std::optional<float> ThermalRateModel::measured_rate() const {
    if (!has_measured_heat_rate_)
        return std::nullopt;
    return measured_heat_rate_;
}

float ThermalRateModel::best_rate() const {
    if (has_measured_heat_rate_)
        return measured_heat_rate_;
    if (has_history_ && hist_heat_rate_ > 0)
        return hist_heat_rate_;
    return default_rate_;
}

void ThermalRateModel::load_history(float rate_s_per_deg) {
    hist_heat_rate_ = rate_s_per_deg;
    has_history_ = true;
}

float ThermalRateModel::blended_rate_for_save() const {
    if (!has_measured_heat_rate_)
        return 0.0f;
    if (has_history_ && hist_heat_rate_ > 0)
        return SAVE_NEW_WEIGHT * measured_heat_rate_ + SAVE_OLD_WEIGHT * hist_heat_rate_;
    return measured_heat_rate_;
}

void ThermalRateModel::set_default_rate(float rate_s_per_deg) {
    default_rate_ = rate_s_per_deg;
}

void ThermalRateModel::reset(float start_temp) {
    measured_heat_rate_ = 0.0f;
    has_measured_heat_rate_ = false;
    start_temp_ = start_temp;
    last_temp_ = start_temp;
    last_tick_ = 0;
    start_tick_ = 0;
}
