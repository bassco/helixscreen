// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "brother_ql_printer.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("BrotherQLPrinter::build_raster_commands init sequence", "[label]") {
    auto bitmap = helix::LabelBitmap::create(306, 10, 300);
    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    auto& size = sizes[0]; // 29mm continuous

    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, size);

    // 200 bytes of 0x00 (invalidate) + ESC @ (initialize)
    REQUIRE(commands.size() > 202);
    for (int i = 0; i < 200; i++) {
        REQUIRE(commands[i] == 0x00);
    }
    REQUIRE(commands[200] == 0x1B);
    REQUIRE(commands[201] == 0x40);
}

TEST_CASE("BrotherQLPrinter::build_raster_commands media info", "[label]") {
    auto bitmap = helix::LabelBitmap::create(306, 10, 300);
    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    auto& size = sizes[0]; // 29mm continuous

    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, size);

    // Media info starts at offset 202: ESC i z
    REQUIRE(commands[202] == 0x1B);
    REQUIRE(commands[203] == 0x69);
    REQUIRE(commands[204] == 0x7A);
    REQUIRE(commands[205] == 0x86); // Validity flags
    REQUIRE(commands[206] == 0x0A); // Media type: continuous
    REQUIRE(commands[207] == 29);   // Width mm
    REQUIRE(commands[208] == 0);    // Length mm (0 = continuous)
}

TEST_CASE("BrotherQLPrinter::build_raster_commands blank row optimization", "[label]") {
    // All-white bitmap produces 0x5A blank line markers
    auto bitmap = helix::LabelBitmap::create(306, 5, 300);
    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, sizes[0]);

    // Count 0x5A bytes after the header section
    // Header: 200 (invalidate) + 2 (init) + 13 (media) + 4 (auto-cut) + 4 (cut-every) + 5 (margins) = 228
    int blank_count = 0;
    for (size_t i = 228; i < commands.size() - 1; i++) {
        if (commands[i] == 0x5A) blank_count++;
    }
    REQUIRE(blank_count == 5);
}

TEST_CASE("BrotherQLPrinter::build_raster_commands ends with print", "[label]") {
    auto bitmap = helix::LabelBitmap::create(306, 5, 300);
    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, sizes[0]);

    REQUIRE(commands.back() == 0x1A);
}

TEST_CASE("BrotherQLPrinter::build_raster_commands raster row format", "[label]") {
    auto bitmap = helix::LabelBitmap::create(306, 3, 300);
    bitmap.set_pixel(0, 1, true); // Set a pixel in row 1

    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, sizes[0]);

    // Should contain at least one raster data row (0x67 0x00 ...)
    bool found_raster = false;
    for (size_t i = 0; i + 2 < commands.size(); i++) {
        if (commands[i] == 0x67 && commands[i + 1] == 0x00) {
            // Byte count should be 90 (RASTER_ROW_BYTES)
            REQUIRE(commands[i + 2] == 90);
            found_raster = true;
            break;
        }
    }
    REQUIRE(found_raster);
}

TEST_CASE("BrotherQLPrinter::build_raster_commands row right-alignment", "[label]") {
    // 29mm label (306px = 39 bytes) should be right-justified in 90-byte row
    auto bitmap = helix::LabelBitmap::create(306, 1, 300);
    // Set a pixel so we get a raster row, not a blank
    bitmap.set_pixel(0, 0, true);

    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, sizes[0]);

    // Find the raster data row
    for (size_t i = 0; i + 2 < commands.size(); i++) {
        if (commands[i] == 0x67 && commands[i + 1] == 0x00) {
            // Left padding: 90 - 39 = 51 bytes of 0x00
            for (int p = 0; p < 51; p++) {
                REQUIRE(commands[i + 3 + p] == 0x00);
            }
            break;
        }
    }
}

TEST_CASE("BrotherQLPrinter::build_raster_commands 62mm full width", "[label]") {
    // 62mm label (696px = 87 bytes) — nearly fills the 90-byte row
    auto bitmap = helix::LabelBitmap::create(696, 2, 300);
    bitmap.set_pixel(0, 0, true);

    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    // Find 62mm continuous
    helix::LabelSize size_62;
    for (auto& s : sizes) {
        if (s.width_mm == 62 && s.height_px == 0) {
            size_62 = s;
            break;
        }
    }

    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, size_62);

    // Left padding should be 90 - 87 = 3 bytes
    for (size_t i = 0; i + 2 < commands.size(); i++) {
        if (commands[i] == 0x67 && commands[i + 1] == 0x00) {
            REQUIRE(commands[i + 2] == 90);
            // First 3 bytes should be padding
            REQUIRE(commands[i + 3] == 0x00);
            REQUIRE(commands[i + 4] == 0x00);
            REQUIRE(commands[i + 5] == 0x00);
            break;
        }
    }
}

TEST_CASE("BrotherQLPrinter::supported_sizes returns known sizes", "[label]") {
    auto sizes = helix::BrotherQLPrinter::supported_sizes();
    REQUIRE(sizes.size() >= 3);

    // Verify 29mm continuous
    REQUIRE(sizes[0].width_mm == 29);
    REQUIRE(sizes[0].width_px == 306);
    REQUIRE(sizes[0].height_px == 0);
    REQUIRE(sizes[0].media_type == 0x0A);

    // Verify 62mm continuous exists
    bool found_62 = false;
    for (auto& s : sizes) {
        if (s.width_mm == 62 && s.height_px == 0) {
            found_62 = true;
            REQUIRE(s.width_px == 696);
            REQUIRE(s.media_type == 0x0A);
        }
    }
    REQUIRE(found_62);
}

TEST_CASE("BrotherQLPrinter::build_raster_commands die-cut media type", "[label]") {
    auto bitmap = helix::LabelBitmap::create(306, 10, 300);
    auto sizes = helix::BrotherQLPrinter::supported_sizes();

    // Find 29x90mm die-cut
    helix::LabelSize die_cut;
    for (auto& s : sizes) {
        if (s.width_mm == 29 && s.length_mm == 90) {
            die_cut = s;
            break;
        }
    }
    REQUIRE(die_cut.media_type == 0x0B);

    auto commands = helix::BrotherQLPrinter::build_raster_commands(bitmap, die_cut);
    // Verify media type byte in command buffer
    REQUIRE(commands[206] == 0x0B);
    REQUIRE(commands[208] == 90); // Length mm
}

TEST_CASE("label_preset_name returns correct names", "[label]") {
    REQUIRE(std::string(helix::label_preset_name(helix::LabelPreset::STANDARD)) == "Standard");
    REQUIRE(std::string(helix::label_preset_name(helix::LabelPreset::COMPACT)) == "Compact");
    REQUIRE(std::string(helix::label_preset_name(helix::LabelPreset::MINIMAL)) == "Minimal");
}

TEST_CASE("label_preset_options returns newline-separated list", "[label]") {
    std::string opts = helix::label_preset_options();
    REQUIRE(opts.find("Standard") != std::string::npos);
    REQUIRE(opts.find("Compact") != std::string::npos);
    REQUIRE(opts.find("Minimal") != std::string::npos);
    REQUIRE(opts.find('\n') != std::string::npos);
}
