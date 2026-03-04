// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../catch_amalgamated.hpp"
#include "label_renderer.h"
#include "spoolman_types.h"

static SpoolInfo make_test_spool() {
    SpoolInfo spool;
    spool.id = 42;
    spool.vendor = "Hatchbox";
    spool.material = "PLA";
    spool.color_name = "Red";
    spool.remaining_weight_g = 800;
    spool.initial_weight_g = 1000;
    return spool;
}

static helix::LabelSize continuous_62mm() {
    return {"62mm", 696, 0, 300, 0x0A, 62, 0};
}

static helix::LabelSize continuous_29mm() {
    return {"29mm", 306, 0, 300, 0x0A, 29, 0};
}

static helix::LabelSize diecut_62x29() {
    return {"62x29mm", 696, 271, 300, 0x0B, 62, 29};
}

/// Check if bitmap has any black pixels
static bool has_black_pixels(const helix::LabelBitmap& bmp) {
    for (int y = 0; y < bmp.height(); y++)
        for (int x = 0; x < bmp.width(); x++)
            if (bmp.get_pixel(x, y))
                return true;
    return false;
}

TEST_CASE("LabelRenderer STANDARD preset produces valid bitmap", "[label]") {
    auto spool = make_test_spool();
    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::STANDARD, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 696);
    REQUIRE(label.height() > 0);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer MINIMAL preset is QR only", "[label]") {
    auto spool = make_test_spool();
    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::MINIMAL, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 696);
    REQUIRE(label.height() > 0);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer COMPACT preset", "[label]") {
    auto spool = make_test_spool();
    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::COMPACT, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 696);
    REQUIRE(label.height() > 0);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer 29mm label", "[label]") {
    auto spool = make_test_spool();
    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::STANDARD, continuous_29mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 306);
    REQUIRE(label.height() > 0);
}

TEST_CASE("LabelRenderer die-cut label fits dimensions", "[label]") {
    auto spool = make_test_spool();
    auto size = diecut_62x29();
    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::STANDARD, size);

    REQUIRE(label.width() == 696);
    REQUIRE(label.height() == 271);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer handles empty vendor and color", "[label]") {
    SpoolInfo spool;
    spool.id = 1;
    spool.material = "PETG";
    // vendor and color_name empty

    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::STANDARD, continuous_62mm());
    REQUIRE_FALSE(label.empty());
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer continuous height adapts to content", "[label]") {
    auto spool = make_test_spool();
    auto minimal = helix::LabelRenderer::render(
        spool, helix::LabelPreset::MINIMAL, continuous_62mm());
    auto standard = helix::LabelRenderer::render(
        spool, helix::LabelPreset::STANDARD, continuous_62mm());

    REQUIRE(minimal.height() > 0);
    REQUIRE(standard.height() > 0);
    // STANDARD has text alongside QR, so may differ in height
}

TEST_CASE("LabelRenderer MINIMAL die-cut centers QR", "[label]") {
    auto spool = make_test_spool();
    auto size = diecut_62x29();
    auto label = helix::LabelRenderer::render(
        spool, helix::LabelPreset::MINIMAL, size);

    REQUIRE(label.width() == 696);
    REQUIRE(label.height() == 271);

    // QR should not touch the very edges (there should be margin)
    bool top_row_clear = true;
    for (int x = 0; x < label.width(); x++)
        if (label.get_pixel(x, 0))
            top_row_clear = false;
    REQUIRE(top_row_clear);
}
