// SPDX-License-Identifier: GPL-3.0-or-later

#include "brother_pt_protocol.h"

#include <algorithm>
#include <array>

namespace helix::label {

static constexpr std::array<BrotherPTTapeInfo, 6> TAPE_TABLE = {{
    {4,  24, 52},   // 3.5mm (status byte = 4)
    {6,  32, 48},   // 6mm
    {9,  50, 39},   // 9mm
    {12, 70, 29},   // 12mm
    {18, 112, 8},   // 18mm
    {24, 128, 0},   // 24mm
}};

const BrotherPTTapeInfo* brother_pt_get_tape_info(int width_mm) {
    for (const auto& t : TAPE_TABLE) {
        if (t.width_mm == width_mm)
            return &t;
    }
    return nullptr;
}

std::optional<LabelSize> brother_pt_label_size_for_tape(int width_mm) {
    const auto* info = brother_pt_get_tape_info(width_mm);
    if (!info)
        return std::nullopt;
    std::string name = (width_mm == 4) ? "3.5mm" : std::to_string(width_mm) + "mm";
    return LabelSize{name, info->printable_pins, 0, 180,
                     0x01,  // media_type: laminated TZe (default)
                     static_cast<uint8_t>(width_mm), 0};
}

// Stubs for functions implemented in later tasks
std::vector<uint8_t> brother_pt_build_status_request() { return {}; }
BrotherPTMedia brother_pt_parse_status(const uint8_t*, size_t) { return {}; }
std::string brother_pt_error_string(const BrotherPTMedia&) { return ""; }
std::vector<uint8_t> brother_pt_packbits_compress(const uint8_t*, size_t) { return {}; }
std::vector<uint8_t> brother_pt_build_raster(const LabelBitmap&, int) { return {}; }

}  // namespace helix::label
