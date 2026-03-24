// SPDX-License-Identifier: GPL-3.0-or-later

#include "z_offset_utils.h"

#include "ui_emergency_stop.h"
#include "ui_toast_manager.h"

#include "app_globals.h"
#include "klipper_config_editor.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "probe_sensor_manager.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <lvgl.h>

namespace helix::zoffset {

bool is_auto_saved(ZOffsetCalibrationStrategy strategy) {
    if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
        // Printers with a probe section need explicit save (e.g., K1C prtouch_v2).
        // Printers without a probe (e.g., FlashForge) auto-persist via firmware.
        if (get_printer_state().has_probe()) {
            return false;
        }
        spdlog::debug("[ZOffsetUtils] Z-offset auto-saved by firmware (gcode_offset strategy)");
        ToastManager::instance().show(ToastSeverity::INFO,
                                      lv_tr("Z-offset is auto-saved by firmware"), 3000);
        return true;
    }
    return false;
}

void format_delta(int microns, char* buf, size_t buf_size) {
    if (microns == 0) {
        buf[0] = '\0';
        return;
    }
    double mm = static_cast<double>(microns) / 1000.0;
    std::snprintf(buf, buf_size, "%+.3fmm", mm);
}

void format_offset(int microns, char* buf, size_t buf_size) {
    double mm = static_cast<double>(microns) / 1000.0;
    std::snprintf(buf, buf_size, "%+.3fmm", mm);
}

void apply_and_save(MoonrakerAPI* api, ZOffsetCalibrationStrategy strategy,
                    std::function<void()> on_success,
                    std::function<void(const std::string& error)> on_error) {
    if (!api) {
        spdlog::error("[ZOffsetUtils] apply_and_save called with null API");
        if (on_error)
            on_error("No printer connection");
        return;
    }

    if (strategy == ZOffsetCalibrationStrategy::GCODE_OFFSET) {
        if (!get_printer_state().has_probe()) {
            // Firmware auto-persists (e.g., FlashForge)
            spdlog::debug("[ZOffsetUtils] apply_and_save: gcode_offset with no probe — auto-saved");
            if (on_success)
                on_success();
            return;
        }

        // Probe printers using gcode_offset (e.g., K1C prtouch_v2):
        // Read current gcode Z offset and persist to probe config section
        int offset_microns = 0;
        if (auto* subj = get_printer_state().get_gcode_z_offset_subject()) {
            offset_microns = lv_subject_get_int(subj);
        }
        double offset_mm = offset_microns / 1000.0;

        // Suppress disconnect modal — config edit triggers firmware restart
        EmergencyStopOverlay::instance().suppress_recovery_dialog(RecoverySuppression::LONG);

        persist_gcode_offset_to_config(api, offset_mm, on_success, on_error);
        return;
    }

    const char* apply_cmd = (strategy == ZOffsetCalibrationStrategy::PROBE_CALIBRATE)
                                ? "Z_OFFSET_APPLY_PROBE"
                                : "Z_OFFSET_APPLY_ENDSTOP";

    const char* strategy_name =
        (strategy == ZOffsetCalibrationStrategy::PROBE_CALIBRATE) ? "probe_calibrate" : "endstop";

    spdlog::info("[ZOffsetUtils] Applying Z-offset with {} strategy (cmd: {})", strategy_name,
                 apply_cmd);

    api->execute_gcode(
        apply_cmd,
        [api, apply_cmd, on_success, on_error]() {
            spdlog::info("[ZOffsetUtils] {} success, executing SAVE_CONFIG", apply_cmd);

            // Suppress disconnect modal — SAVE_CONFIG triggers a Klipper restart
            EmergencyStopOverlay::instance().suppress_recovery_dialog(
                RecoverySuppression::LONG);

            api->execute_gcode(
                "SAVE_CONFIG",
                [on_success]() {
                    spdlog::info("[ZOffsetUtils] SAVE_CONFIG success — Klipper restarting");
                    if (on_success)
                        on_success();
                },
                [on_error](const MoonrakerError& err) {
                    std::string msg = fmt::format(
                        "SAVE_CONFIG failed: {}. Z-offset was applied but not saved. "
                        "Run SAVE_CONFIG manually or the offset will be lost on restart.",
                        err.user_message());
                    spdlog::error("[ZOffsetUtils] {}", msg);
                    if (on_error)
                        on_error(msg);
                });
        },
        [apply_cmd, on_error](const MoonrakerError& err) {
            std::string msg = fmt::format("{} failed: {}", apply_cmd, err.user_message());
            spdlog::error("[ZOffsetUtils] {}", msg);
            if (on_error)
                on_error(msg);
        });
}

void persist_gcode_offset_to_config(MoonrakerAPI* api, double offset_mm,
                                     std::function<void()> on_success,
                                     std::function<void(const std::string& error)> on_error) {
    if (!api) {
        if (on_error)
            on_error("No printer connection");
        return;
    }

    // Determine the probe config section name
    auto& mgr = helix::sensors::ProbeSensorManager::instance();
    auto sensors = mgr.get_sensors();
    std::string section = "probe";
    if (!sensors.empty()) {
        switch (sensors[0].type) {
        case helix::sensors::ProbeSensorType::PRTOUCH_V2:
            section = "prtouch_v2";
            break;
        case helix::sensors::ProbeSensorType::BLTOUCH:
            section = "bltouch";
            break;
        case helix::sensors::ProbeSensorType::SMART_EFFECTOR:
            section = "smart_effector";
            break;
        default:
            break;
        }
    }

    char value_buf[32];
    std::snprintf(value_buf, sizeof(value_buf), "%.3f", offset_mm);
    std::string value_str = value_buf;

    spdlog::info("[ZOffsetUtils] Persisting gcode Z-offset {:.3f}mm to [{}] z_offset", offset_mm,
                 section);

    // Static config editor to survive the async chain
    static helix::system::KlipperConfigEditor config_editor;

    config_editor.load_config_files(
        *api,
        [api, section, value_str, on_success,
         on_error](std::map<std::string, helix::system::SectionLocation> /*section_map*/) {
            config_editor.safe_edit_value(
                *api, section, "z_offset", value_str,
                [on_success]() {
                    spdlog::info("[ZOffsetUtils] Config edit saved, firmware restarting");
                    if (on_success)
                        on_success();
                },
                [on_error](const std::string& err) {
                    spdlog::error("[ZOffsetUtils] Config edit failed: {}", err);
                    if (on_error)
                        on_error(err);
                });
        },
        [on_error](const std::string& err) {
            spdlog::error("[ZOffsetUtils] Failed to load config files: {}", err);
            if (on_error)
                on_error("Failed to load config: " + err);
        });
}

} // namespace helix::zoffset
