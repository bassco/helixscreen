// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system_power.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <spdlog/spdlog.h>

#ifdef HELIX_HAS_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace helix {
namespace {

#ifdef HELIX_HAS_SYSTEMD
bool logind_call(const char* method) {
    sd_bus* bus = nullptr;
    const int open_rc = sd_bus_open_system(&bus);
    if (open_rc < 0 || !bus) {
        spdlog::warn("[SystemPower] sd_bus_open_system failed: {}", std::strerror(-open_rc));
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_call_method(bus,
                                     "org.freedesktop.login1",
                                     "/org/freedesktop/login1",
                                     "org.freedesktop.login1.Manager",
                                     method,
                                     &err,
                                     &reply,
                                     "b",
                                     0 /* interactive=false */);
    const bool ok = r >= 0;
    if (!ok) {
        spdlog::warn("[SystemPower] logind {} failed: {} ({})",
                     method,
                     err.message ? err.message : "no message",
                     err.name ? err.name : "");
    }
    sd_bus_error_free(&err);
    if (reply) sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return ok;
}
#endif

bool systemctl_fallback(const char* verb) {
    std::string cmd = "systemctl ";
    cmd += verb;
    spdlog::info("[SystemPower] fallback: {}", cmd);
    const int rc = std::system(cmd.c_str());
    if (rc == -1) {
        spdlog::warn("[SystemPower] fallback fork/exec failed");
        return false;
    }
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

} // namespace

bool SystemPower::reboot_local() {
    spdlog::info("[SystemPower] reboot_local");
#ifdef HELIX_HAS_SYSTEMD
    if (logind_call("Reboot")) return true;
#endif
    return systemctl_fallback("reboot");
}

bool SystemPower::shutdown_local() {
    spdlog::info("[SystemPower] shutdown_local");
#ifdef HELIX_HAS_SYSTEMD
    if (logind_call("PowerOff")) return true;
#endif
    return systemctl_fallback("poweroff");
}

} // namespace helix
