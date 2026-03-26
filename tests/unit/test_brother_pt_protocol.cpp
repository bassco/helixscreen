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

// --- Task 2: Status request/parse/error ---

TEST_CASE("Brother PT status request command", "[label][brother-pt]") {
    auto cmd = brother_pt_build_status_request();
    REQUIRE(cmd.size() == 105);
    for (int i = 0; i < 100; i++) {
        REQUIRE(cmd[i] == 0x00);
    }
    REQUIRE(cmd[100] == 0x1B);
    REQUIRE(cmd[101] == 0x40);
    REQUIRE(cmd[102] == 0x1B);
    REQUIRE(cmd[103] == 0x69);
    REQUIRE(cmd[104] == 0x53);
}

TEST_CASE("Brother PT parse status - valid 12mm laminated", "[label][brother-pt]") {
    uint8_t response[32] = {};
    response[0] = 0x80;
    response[10] = 12;
    response[11] = 0x01;
    response[18] = 0x00;

    auto media = brother_pt_parse_status(response, 32);
    REQUIRE(media.valid);
    REQUIRE(media.width_mm == 12);
    REQUIRE(media.media_type == 0x01);
    REQUIRE(media.status_type == 0x00);
    REQUIRE(media.error_info_1 == 0);
    REQUIRE(media.error_info_2 == 0);
}

TEST_CASE("Brother PT parse status - no tape", "[label][brother-pt]") {
    uint8_t response[32] = {};
    response[0] = 0x80;
    response[8] = 0x01;
    response[10] = 0;
    response[18] = 0x02;

    auto media = brother_pt_parse_status(response, 32);
    REQUIRE(media.valid);
    REQUIRE(media.width_mm == 0);
    REQUIRE(media.error_info_1 == 0x01);
    REQUIRE(media.status_type == 0x02);
}

TEST_CASE("Brother PT parse status - truncated data", "[label][brother-pt]") {
    uint8_t response[10] = {};
    auto media = brother_pt_parse_status(response, 10);
    REQUIRE_FALSE(media.valid);
}

TEST_CASE("Brother PT parse status - wrong header", "[label][brother-pt]") {
    uint8_t response[32] = {};
    response[0] = 0x00;
    auto media = brother_pt_parse_status(response, 32);
    REQUIRE_FALSE(media.valid);
}

TEST_CASE("Brother PT error string", "[label][brother-pt]") {
    BrotherPTMedia media{};
    media.valid = true;

    REQUIRE(brother_pt_error_string(media).empty());

    media.error_info_1 = 0x01;
    REQUIRE(brother_pt_error_string(media).find("media") != std::string::npos);

    media.error_info_1 = 0x04;
    REQUIRE(brother_pt_error_string(media).find("utter") != std::string::npos);

    media.error_info_1 = 0x08;
    REQUIRE(brother_pt_error_string(media).find("battery") != std::string::npos);

    media.error_info_1 = 0;
    media.error_info_2 = 0x01;
    REQUIRE(brother_pt_error_string(media).find("media") != std::string::npos);

    media.error_info_2 = 0x10;
    REQUIRE(brother_pt_error_string(media).find("over") != std::string::npos);

    media.error_info_2 = 0x20;
    REQUIRE(brother_pt_error_string(media).find("ver") != std::string::npos);
}

// --- Task 3: PackBits compression ---

TEST_CASE("Brother PT PackBits - all zeros", "[label][brother-pt]") {
    std::vector<uint8_t> row(16, 0x00);
    auto compressed = brother_pt_packbits_compress(row.data(), row.size());
    REQUIRE(compressed.size() == 2);
    REQUIRE(compressed[0] == 0xF1);  // -(16-1) = -15 = 0xF1
    REQUIRE(compressed[1] == 0x00);
}

TEST_CASE("Brother PT PackBits - all unique bytes", "[label][brother-pt]") {
    std::vector<uint8_t> row;
    for (int i = 0; i < 16; i++) row.push_back(static_cast<uint8_t>(i));
    auto compressed = brother_pt_packbits_compress(row.data(), row.size());
    REQUIRE(compressed.size() == 17);
    REQUIRE(compressed[0] == 15);
}

TEST_CASE("Brother PT PackBits - mixed run", "[label][brother-pt]") {
    std::vector<uint8_t> row = {0x01, 0x02, 0x03, 0x04};
    row.insert(row.end(), 12, 0xFF);
    auto compressed = brother_pt_packbits_compress(row.data(), row.size());
    REQUIRE(compressed.size() > 2);
    REQUIRE(compressed.size() < 16);
}
