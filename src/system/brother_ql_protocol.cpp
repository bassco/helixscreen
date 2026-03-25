// SPDX-License-Identifier: GPL-3.0-or-later

#include "brother_ql_protocol.h"

#include <algorithm>

namespace helix::label {

std::vector<uint8_t> brother_ql_build_raster(const LabelBitmap& bitmap,
                                              const LabelSize& size) {
    std::vector<uint8_t> cmd;
    // Pre-allocate: header ~220 bytes + per-row worst case (3 + 90) * rows + footer
    cmd.reserve(256 + static_cast<size_t>(bitmap.height()) * (3 + BROTHER_QL_RASTER_ROW_BYTES) + 1);

    // 1. Invalidate — 200 bytes of 0x00
    cmd.insert(cmd.end(), 200, 0x00);

    // 2. Initialize — ESC @
    cmd.push_back(0x1B);
    cmd.push_back(0x40);

    // 3. Switch to raster mode — ESC i a 01
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x61);
    cmd.push_back(0x01);

    // 4. Set media and quality — ESC i z
    int page_length = bitmap.height();
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x7A);
    // Validity flags: bit 7=PI, bit 2=width, bit 1=media_type, bit 3=length (die-cut only)
    uint8_t valid_flags = 0x80 | 0x04 | 0x02;  // PI + width + media type
    if (size.media_type == 0x0B) {
        valid_flags |= 0x08;  // Die-cut: mark length field as valid
    }
    cmd.push_back(valid_flags);
    cmd.push_back(size.media_type);  // Media type
    cmd.push_back(size.width_mm);    // Width in mm
    cmd.push_back(size.length_mm);   // Length in mm
    // Page length as little-endian 32-bit
    cmd.push_back(static_cast<uint8_t>(page_length & 0xFF));
    cmd.push_back(static_cast<uint8_t>((page_length >> 8) & 0xFF));
    cmd.push_back(static_cast<uint8_t>((page_length >> 16) & 0xFF));
    cmd.push_back(static_cast<uint8_t>((page_length >> 24) & 0xFF));
    cmd.push_back(0x00); // Page number = 0
    cmd.push_back(0x00); // Auto-cut flag (set separately)

    // 5. Set auto-cut on — ESC i M
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x4D);
    cmd.push_back(0x40);

    // 5a. Set cut-every = 1 label — ESC i A
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x41);
    cmd.push_back(0x01);

    // 6. Expanded mode — ESC i K (auto-cut, no 2-color, 300dpi)
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x4B);
    cmd.push_back(0x08); // bit 3 = auto-cut

    // 7. Set margins = 0 (no margins for die-cut)
    cmd.push_back(0x1B);
    cmd.push_back(0x69);
    cmd.push_back(0x64);
    cmd.push_back(0x00);
    cmd.push_back(0x00);

    // 8. Disable compression — M 0x00
    cmd.push_back(0x4D);
    cmd.push_back(0x00);

    // 9. Raster data rows
    // Bitmap data is right-justified in the 90-byte row (left-padded with zeros)
    // matching the Brother QL printhead layout where narrow labels are on the right edge.
    // No horizontal flip — the bitmap is already in the correct pixel order (MSB first,
    // left-to-right), matching the Brother QL raster format.
    int label_byte_width = (size.width_px + 7) / 8;
    int left_pad = BROTHER_QL_RASTER_ROW_BYTES - label_byte_width;
    if (left_pad < 0) left_pad = 0;

    // Number of bytes to copy from the bitmap per row (may differ if bitmap is
    // narrower or wider than label_byte_width)
    int copy_bytes = std::min(label_byte_width, bitmap.row_byte_width());

    for (int y = 0; y < bitmap.height(); y++) {
        const uint8_t* row = bitmap.row_data(y);

        // Check if row is all white
        bool all_white = true;
        for (int b = 0; b < copy_bytes; b++) {
            if (row[b] != 0x00) {
                all_white = false;
                break;
            }
        }

        if (all_white) {
            // Blank line marker
            cmd.push_back(0x5A);
        } else {
            // Raster data: 0x67 0x00 [byte_count] [data...]
            cmd.push_back(0x67);
            cmd.push_back(0x00);
            cmd.push_back(static_cast<uint8_t>(BROTHER_QL_RASTER_ROW_BYTES));

            // Left padding (for narrow labels right-justified in 90-byte row)
            cmd.insert(cmd.end(), left_pad, 0x00);

            // Copy bitmap pixel data directly
            cmd.insert(cmd.end(), row, row + copy_bytes);

            // Right padding if bitmap is narrower than label
            int right_pad = label_byte_width - copy_bytes;
            if (right_pad > 0) {
                cmd.insert(cmd.end(), right_pad, 0x00);
            }
        }
    }

    // 10. Print command
    cmd.push_back(0x1A);

    return cmd;
}

}  // namespace helix::label
