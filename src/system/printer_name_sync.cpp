// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_name_sync.h"

#include "ui_update_queue.h"

#include "app_globals.h"
#include "config.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterNameSync::resolve(MoonrakerAPI* api, const std::string& hostname) {
    Config* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[PrinterNameSync] No config instance, skipping resolve");
        return;
    }

    // Check local config first — if set, we're done (local wins)
    std::string local_name = config->get<std::string>(config->df() + wizard::PRINTER_NAME, "");
    if (!local_name.empty()) {
        spdlog::debug("[PrinterNameSync] Local name already set: '{}'", local_name);
        return;
    }

    if (!api) {
        spdlog::debug("[PrinterNameSync] No API, falling back to hostname");
        if (!hostname.empty() && hostname != "unknown") {
            config->set<std::string>(config->df() + wizard::PRINTER_NAME, hostname);
            config->save();
            get_printer_state().set_active_printer_name(hostname);
            spdlog::info("[PrinterNameSync] Seeded name from hostname: '{}'", hostname);
        }
        return;
    }

    // Try Mainsail first, then Fluidd, then hostname
    api->database_get_item(
        MAINSAIL_NAMESPACE, MAINSAIL_KEY,
        [hostname](const nlohmann::json& value) {
            std::string name;
            if (value.is_string()) {
                name = value.get<std::string>();
            }

            if (!name.empty()) {
                // Got a name from Mainsail — seed it
                helix::ui::queue_update("PrinterNameSync::mainsail", [name]() {
                    Config* cfg = Config::get_instance();
                    if (cfg) {
                        cfg->set<std::string>(cfg->df() + wizard::PRINTER_NAME, name);
                        cfg->save();
                    }
                    get_printer_state().set_active_printer_name(name);
                    spdlog::info("[PrinterNameSync] Seeded name from Mainsail: '{}'", name);
                });
                return;
            }

            // Mainsail key exists but empty — fall through to Fluidd
            MoonrakerAPI* api2 = get_moonraker_api();
            if (!api2)
                return;

            api2->database_get_item(
                FLUIDD_NAMESPACE, FLUIDD_KEY,
                [hostname](const nlohmann::json& fluidd_value) {
                    std::string fluidd_name;
                    if (fluidd_value.is_string()) {
                        fluidd_name = fluidd_value.get<std::string>();
                    }

                    std::string final_name = !fluidd_name.empty() ? fluidd_name : "";

                    if (final_name.empty()) {
                        if (!hostname.empty() && hostname != "unknown") {
                            final_name = hostname;
                        }
                    }

                    if (final_name.empty())
                        return;

                    helix::ui::queue_update("PrinterNameSync::fluidd", [final_name]() {
                        Config* cfg = Config::get_instance();
                        if (cfg) {
                            cfg->set<std::string>(cfg->df() + wizard::PRINTER_NAME, final_name);
                            cfg->save();
                        }
                        get_printer_state().set_active_printer_name(final_name);
                        spdlog::info("[PrinterNameSync] Seeded name from {}: '{}'",
                                     final_name.find("unknown") == std::string::npos ? "Fluidd"
                                                                                     : "hostname",
                                     final_name);
                    });
                },
                [hostname](const MoonrakerError&) {
                    if (hostname.empty() || hostname == "unknown")
                        return;

                    helix::ui::queue_update("PrinterNameSync::hostname", [hostname]() {
                        Config* cfg = Config::get_instance();
                        if (cfg) {
                            cfg->set<std::string>(cfg->df() + wizard::PRINTER_NAME, hostname);
                            cfg->save();
                        }
                        get_printer_state().set_active_printer_name(hostname);
                        spdlog::info("[PrinterNameSync] Seeded name from hostname: '{}'", hostname);
                    });
                });
        },
        [api, hostname](const MoonrakerError&) {
            // Mainsail namespace doesn't exist — try Fluidd
            api->database_get_item(
                FLUIDD_NAMESPACE, FLUIDD_KEY,
                [hostname](const nlohmann::json& fluidd_value) {
                    std::string name;
                    if (fluidd_value.is_string()) {
                        name = fluidd_value.get<std::string>();
                    }

                    if (name.empty()) {
                        if (!hostname.empty() && hostname != "unknown") {
                            name = hostname;
                        }
                    }

                    if (name.empty())
                        return;

                    helix::ui::queue_update("PrinterNameSync::fluidd_fallback", [name]() {
                        Config* cfg = Config::get_instance();
                        if (cfg) {
                            cfg->set<std::string>(cfg->df() + wizard::PRINTER_NAME, name);
                            cfg->save();
                        }
                        get_printer_state().set_active_printer_name(name);
                        spdlog::info("[PrinterNameSync] Seeded name from Fluidd: '{}'", name);
                    });
                },
                [hostname](const MoonrakerError&) {
                    if (hostname.empty() || hostname == "unknown")
                        return;

                    helix::ui::queue_update("PrinterNameSync::hostname_last", [hostname]() {
                        Config* cfg = Config::get_instance();
                        if (cfg) {
                            cfg->set<std::string>(cfg->df() + wizard::PRINTER_NAME, hostname);
                            cfg->save();
                        }
                        get_printer_state().set_active_printer_name(hostname);
                        spdlog::info("[PrinterNameSync] Seeded name from hostname: '{}'", hostname);
                    });
                });
        });
}

void PrinterNameSync::write_back(MoonrakerAPI* api, const std::string& name) {
    if (!api || name.empty())
        return;

    api->database_post_item(
        MAINSAIL_NAMESPACE, MAINSAIL_KEY, name,
        [name]() { spdlog::debug("[PrinterNameSync] Wrote '{}' to Mainsail DB", name); },
        [](const MoonrakerError& err) {
            spdlog::warn("[PrinterNameSync] Failed to write to Mainsail DB: {}",
                         err.user_message());
        });

    api->database_post_item(
        FLUIDD_NAMESPACE, FLUIDD_KEY, name,
        [name]() { spdlog::debug("[PrinterNameSync] Wrote '{}' to Fluidd DB", name); },
        [](const MoonrakerError& err) {
            spdlog::warn("[PrinterNameSync] Failed to write to Fluidd DB: {}", err.user_message());
        });
}

} // namespace helix
