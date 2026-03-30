// SPDX-License-Identifier: GPL-3.0-or-later

#include "preprint_predictor.h"

#include "config.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace helix {

void PreprintPredictor::load_entries(const std::vector<PreprintEntry>& entries, int temp_bucket) {
    entries_.clear();

    if (temp_bucket > 0) {
        // Filter to matching bucket (or legacy entries with bucket 0)
        for (const auto& e : entries) {
            if (e.temp_bucket == temp_bucket || e.temp_bucket == 0) {
                entries_.push_back(e);
            }
        }
    } else {
        entries_ = entries;
    }

    // Keep only the last MAX_ENTRIES
    while (entries_.size() > static_cast<size_t>(MAX_ENTRIES)) {
        entries_.erase(entries_.begin());
    }
}

void PreprintPredictor::add_entry(const PreprintEntry& entry) {
    // Reject anomalous entries
    if (entry.total_seconds > MAX_TOTAL_SECONDS) {
        return;
    }

    entries_.push_back(entry);

    // FIFO trim
    while (entries_.size() > static_cast<size_t>(MAX_ENTRIES)) {
        entries_.erase(entries_.begin());
    }
}

std::vector<PreprintEntry> PreprintPredictor::get_entries() const {
    return entries_;
}

bool PreprintPredictor::has_predictions() const {
    return !entries_.empty();
}

std::map<int, int> PreprintPredictor::predicted_phases() const {
    if (entries_.empty()) {
        return {};
    }

    // Collect all phases that appear in any entry
    std::set<int> all_phases;
    for (const auto& entry : entries_) {
        for (const auto& [phase, _] : entry.phase_durations) {
            all_phases.insert(phase);
        }
    }

    // Weights based on entry count (newest last in vector)
    // 1 entry: [1.0]
    // 2 entries: [0.4, 0.6]
    // 3 entries: [0.2, 0.3, 0.5]
    std::vector<double> weights;
    switch (entries_.size()) {
    case 1:
        weights = {1.0};
        break;
    case 2:
        weights = {0.4, 0.6};
        break;
    default: // 3+
        weights = {0.2, 0.3, 0.5};
        break;
    }

    std::map<int, int> result;
    for (int phase : all_phases) {
        // Find which entries have this phase and redistribute weights
        double total_weight = 0.0;
        double weighted_sum = 0.0;

        for (size_t i = 0; i < entries_.size(); ++i) {
            auto it = entries_[i].phase_durations.find(phase);
            if (it != entries_[i].phase_durations.end()) {
                total_weight += weights[i];
                weighted_sum += weights[i] * it->second;
            }
        }

        if (total_weight > 0.0) {
            result[phase] = static_cast<int>(std::round(weighted_sum / total_weight));
        }
    }

    return result;
}

int PreprintPredictor::predicted_total() const {
    auto phases = predicted_phases();
    int total = 0;
    for (const auto& [_, duration] : phases) {
        total += duration;
    }
    return total;
}

int PreprintPredictor::remaining_seconds(const std::set<int>& completed_phases, int current_phase,
                                         int elapsed_in_current_phase_seconds) const {
    if (entries_.empty()) {
        return 0;
    }

    auto phases = predicted_phases();
    int remaining = 0;

    for (const auto& [phase, predicted_duration] : phases) {
        if (completed_phases.count(phase)) {
            // Already done, actual time was spent (not predicted)
            continue;
        }

        if (phase == current_phase && current_phase != 0) {
            // Currently in this phase - subtract elapsed
            remaining += std::max(0, predicted_duration - elapsed_in_current_phase_seconds);
        } else if (!completed_phases.count(phase) && phase != current_phase) {
            // Future phase
            remaining += predicted_duration;
        }
    }

    return remaining;
}

std::vector<PreprintEntry> PreprintPredictor::load_entries_from_config() {
    auto* cfg = Config::get_instance();
    if (!cfg) {
        return {};
    }

    try {
        auto entries_json =
            cfg->get<nlohmann::json>("/print_start_history/entries", nlohmann::json::array());
        if (!entries_json.is_array() || entries_json.empty()) {
            return {};
        }

        std::vector<PreprintEntry> entries;
        for (const auto& ej : entries_json) {
            PreprintEntry entry;
            entry.total_seconds = ej.value("total", 0);
            entry.timestamp = ej.value("timestamp", static_cast<int64_t>(0));
            entry.temp_bucket = ej.value("temp_bucket", 0);
            if (ej.contains("phases") && ej["phases"].is_object()) {
                for (auto& [key, val] : ej["phases"].items()) {
                    entry.phase_durations[std::stoi(key)] = val.get<int>();
                }
            }
            entries.push_back(std::move(entry));
        }
        return entries;
    } catch (...) {
        return {};
    }
}

int PreprintPredictor::predicted_total_from_config() {
    // Cache result for 60s to avoid re-parsing config on every file in a list
    static std::atomic<int> cached_value{-1};
    static std::atomic<int64_t> cached_at{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    if (cached_value.load() >= 0 && (now_sec - cached_at.load()) < 60) {
        return cached_value.load();
    }

    auto entries = load_entries_from_config();
    int result = 0;
    if (!entries.empty()) {
        PreprintPredictor predictor;
        predictor.load_entries(entries);
        result = predictor.predicted_total();
    }

    cached_value.store(result);
    cached_at.store(now_sec);
    return result;
}

} // namespace helix
