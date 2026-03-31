// SPDX-License-Identifier: GPL-3.0-or-later

#include "macro_fan_analyzer.h"

#include <regex>
#include <spdlog/spdlog.h>

namespace helix {

MacroFanAnalysis MacroFanAnalyzer::analyze(const nlohmann::json& config_settings) const {
    MacroFanAnalysis result;

    // Parse M106 macro for SET_PIN PIN=fanN patterns
    if (config_settings.contains("gcode_macro m106")) {
        const auto& m106 = config_settings["gcode_macro m106"];
        if (m106.contains("gcode") && m106["gcode"].is_string()) {
            extract_set_pin_fans(m106["gcode"].get<std::string>(), result);
        }
    }

    // Parse M107 macro for additional SET_PIN PIN=fanN patterns
    if (config_settings.contains("gcode_macro m107")) {
        const auto& m107 = config_settings["gcode_macro m107"];
        if (m107.contains("gcode") && m107["gcode"].is_string()) {
            extract_set_pin_fans(m107["gcode"].get<std::string>(), result);
        }
    }

    // Parse M141 macro for chamber circulation fan hints
    if (config_settings.contains("gcode_macro m141")) {
        const auto& m141 = config_settings["gcode_macro m141"];
        if (m141.contains("gcode") && m141["gcode"].is_string()) {
            extract_m141_roles(m141["gcode"].get<std::string>(), result);
        }
    }

    if (!result.fan_indices.empty()) {
        spdlog::info("[MacroFanAnalyzer] Detected {} output_pin fans from macros",
                     result.fan_indices.size());
        for (const auto& [name, index] : result.fan_indices) {
            spdlog::debug("[MacroFanAnalyzer]   {} -> M106 P{}", name, index);
        }
    }
    if (!result.role_hints.empty()) {
        for (const auto& [name, role] : result.role_hints) {
            spdlog::debug("[MacroFanAnalyzer]   {} -> role hint: {}", name, role);
        }
    }

    return result;
}

void MacroFanAnalyzer::extract_set_pin_fans(const std::string& gcode,
                                            MacroFanAnalysis& result) const {
    // Match SET_PIN PIN=fanN where N is one or more digits
    std::regex pattern(R"(SET_PIN\s+PIN=fan(\d+))");
    auto begin = std::sregex_iterator(gcode.begin(), gcode.end(), pattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        int index = std::stoi((*it)[1].str());
        std::string obj_name = "output_pin fan" + std::to_string(index);
        result.fan_indices[obj_name] = index;
    }
}

void MacroFanAnalyzer::extract_m141_roles(const std::string& gcode,
                                          MacroFanAnalysis& result) const {
    // M141 is the chamber temperature command. Any SET_PIN PIN=fanN in M141
    // indicates that fanN is used for chamber circulation/ventilation.
    std::regex pattern(R"(SET_PIN\s+PIN=fan(\d+))");
    auto begin = std::sregex_iterator(gcode.begin(), gcode.end(), pattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        int index = std::stoi((*it)[1].str());
        std::string obj_name = "output_pin fan" + std::to_string(index);
        result.role_hints[obj_name] = "Chamber Circulation";
    }
}

} // namespace helix
