// SPDX-License-Identifier: GPL-3.0-or-later
//
// No-op stub for helix_lvgl_anomaly() — used by helix-splash and
// helix-watchdog. Both link against the patched LVGL (lv_obj_tree.c references
// helix_lvgl_anomaly for double-schedule / UAF telemetry) but do not link the
// telemetry manager. The real implementation lives in
// src/system/helix_lvgl_anomaly.cpp.

#include "helix_lvgl_anomaly.h"

extern "C" void helix_lvgl_anomaly(const char* /*code*/, const char* /*context*/) {
    // splash/watchdog have no telemetry pipeline; drop the report on the floor.
}
