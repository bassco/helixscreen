// SPDX-License-Identifier: GPL-3.0-or-later

#include "../catch_amalgamated.hpp"
#include "brother_pt_protocol.h"

using namespace helix::label;

TEST_CASE("Brother PT tape info - valid widths", "[label][brother-pt]") {
    auto* info = brother_pt_get_tape_info(4);
    REQUIRE(info != nullptr);
    REQUIRE(info->printable_pins == 24);
    REQUIRE(info->left_margin_pins == 52);

    info = brother_pt_get_tape_info(12);
    REQUIRE(info != nullptr);
    REQUIRE(info->printable_pins == 70);
    REQUIRE(info->left_margin_pins == 29);

    info = brother_pt_get_tape_info(24);
    REQUIRE(info != nullptr);
    REQUIRE(info->printable_pins == 128);
    REQUIRE(info->left_margin_pins == 0);
}

TEST_CASE("Brother PT tape info - all supported widths", "[label][brother-pt]") {
    for (int w : {4, 6, 9, 12, 18, 24}) {
        auto* info = brother_pt_get_tape_info(w);
        REQUIRE(info != nullptr);
        REQUIRE(info->width_mm == w);
        // Margins + printable = 128 pins total
        REQUIRE(info->left_margin_pins * 2 + info->printable_pins <= 128);
    }
}

TEST_CASE("Brother PT tape info - invalid width", "[label][brother-pt]") {
    REQUIRE(brother_pt_get_tape_info(0) == nullptr);
    REQUIRE(brother_pt_get_tape_info(15) == nullptr);
    REQUIRE(brother_pt_get_tape_info(36) == nullptr);
}

TEST_CASE("Brother PT label size for tape", "[label][brother-pt]") {
    auto size = brother_pt_label_size_for_tape(12);
    REQUIRE(size.has_value());
    REQUIRE(size->width_px == 70);
    REQUIRE(size->height_px == 0);
    REQUIRE(size->dpi == 180);
    REQUIRE(size->width_mm == 12);

    REQUIRE_FALSE(brother_pt_label_size_for_tape(15).has_value());
}

TEST_CASE("Brother PT label size for 3.5mm tape", "[label][brother-pt]") {
    auto size = brother_pt_label_size_for_tape(4);
    REQUIRE(size.has_value());
    REQUIRE(size->name == "3.5mm");
    REQUIRE(size->width_px == 24);
    REQUIRE(size->dpi == 180);
    REQUIRE(size->width_mm == 4);
}
