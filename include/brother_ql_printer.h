// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_bitmap.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace helix {

/// Label media size definition for Brother QL printers
struct LabelSize {
    std::string name;    // Human-readable: "29mm", "62mm", "29x90mm"
    int width_px;        // Print width in pixels at 300 DPI
    int height_px;       // 0 = continuous (auto-size based on content)
    int dpi = 300;
    uint8_t media_type;  // Brother protocol media type byte (0x0A=continuous, 0x0B=die-cut)
    uint8_t width_mm;    // Physical width in mm (for protocol)
    uint8_t length_mm;   // Physical length in mm (0 for continuous)
};

/// Label layout preset
enum class LabelPreset { STANDARD, COMPACT, MINIMAL };

/// Get human-readable name for a preset
const char* label_preset_name(LabelPreset preset);

/// Get all preset names as newline-separated string (for dropdown)
const char* label_preset_options();

/**
 * @brief Brother QL label printer backend
 *
 * Implements the Brother QL raster protocol over TCP (port 9100).
 * Supports QL-800, QL-810W, QL-820NWB and similar models.
 *
 * Thread safety: print_label() runs async on a detached thread. Callbacks
 * are dispatched to the UI thread via async_call().
 */
class BrotherQLPrinter {
  public:
    BrotherQLPrinter();
    ~BrotherQLPrinter();

    BrotherQLPrinter(const BrotherQLPrinter&) = delete;
    BrotherQLPrinter& operator=(const BrotherQLPrinter&) = delete;

    using PrintCallback = std::function<void(bool success, const std::string& error)>;

    /// Print a label bitmap. Connects, sends, disconnects.
    /// Callback fires on UI thread via async_call().
    void print_label(const std::string& host, int port,
                     const LabelBitmap& bitmap, const LabelSize& size,
                     PrintCallback callback);

    /// Get supported label sizes for Brother QL printers
    static std::vector<LabelSize> supported_sizes();

    /// Build the raw raster command buffer (public for testing)
    static std::vector<uint8_t> build_raster_commands(const LabelBitmap& bitmap,
                                                       const LabelSize& size);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace helix
