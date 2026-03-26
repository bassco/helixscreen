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

std::vector<uint8_t> brother_pt_build_status_request() {
    std::vector<uint8_t> cmd;
    // 1. Invalidate — 100 bytes of 0x00 (PT uses fewer than QL's 200)
    cmd.insert(cmd.end(), 100, 0x00);
    // 2. Initialize — ESC @
    cmd.push_back(0x1B);
    cmd.push_back(0x40);
    // 3. Request status — ESC i S
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x53);
    return cmd;
}

BrotherPTMedia brother_pt_parse_status(const uint8_t* data, size_t len) {
    BrotherPTMedia media{};
    if (!data || len < 32)
        return media;
    if (data[0] != 0x80)
        return media;
    media.error_info_1 = data[8];
    media.error_info_2 = data[9];
    media.width_mm = data[10];
    media.media_type = data[11];
    media.status_type = data[18];
    media.valid = true;
    return media;
}

std::string brother_pt_error_string(const BrotherPTMedia& media) {
    if (media.error_info_1 & 0x01) return "No media installed";
    if (media.error_info_1 & 0x04) return "Cutter jam";
    if (media.error_info_1 & 0x08) return "Weak battery";
    if (media.error_info_2 & 0x01) return "Wrong media";
    if (media.error_info_2 & 0x10) return "Cover open";
    if (media.error_info_2 & 0x20) return "Overheating";
    return "";
}

std::vector<uint8_t> brother_pt_packbits_compress(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < len) {
        // Count repeated bytes
        size_t run = 1;
        while (i + run < len && run < 128 && data[i + run] == data[i])
            run++;

        if (run >= 2) {
            // Repeat run: control = -(run-1), then single byte
            out.push_back(static_cast<uint8_t>(-(static_cast<int>(run) - 1)));
            out.push_back(data[i]);
            i += run;
        } else {
            // Literal run: collect non-repeating bytes
            size_t lit_start = i;
            size_t lit_len = 1;
            i++;
            while (i < len && lit_len < 128) {
                if (i + 1 < len && data[i] == data[i + 1])
                    break;
                lit_len++;
                i++;
            }
            out.push_back(static_cast<uint8_t>(lit_len - 1));
            out.insert(out.end(), data + lit_start, data + lit_start + lit_len);
        }
    }
    return out;
}

// Stub for function implemented in a later task
std::vector<uint8_t> brother_pt_build_raster(const LabelBitmap&, int) { return {}; }

}  // namespace helix::label
