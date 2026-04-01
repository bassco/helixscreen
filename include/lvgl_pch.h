// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Precompiled header for LVGL and common includes
// This header is precompiled to speed up build times (30-50% faster clean builds)
//
// Only include headers that are:
// 1. Used frequently across many translation units
// 2. Rarely change (external libraries, stable APIs)
// 3. Heavy to parse (LVGL, STL containers)

#pragma once

// LVGL core headers (processed 200+ times without PCH)
#include "lvgl/lvgl.h"

// Helix XML engine (extracted from LVGL, standalone since v9.5 removed XML)
#include "helix-xml/helix_xml.h"

// Common STL headers used throughout the project
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// spdlog (used in nearly every file)
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

// nlohmann JSON (via libhv) — included transitively by 100+ files through
// printer_state.h, tool_state.h, sensor_state.h, etc. Heavy template library
// that benefits significantly from precompilation.
#include "hv/json.hpp"
