// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_error.h"

#include "../catch_amalgamated.hpp"

#include "hv/json.hpp"

using nlohmann::json;

// ============================================================================
// extract_friendly_message() tests
// ============================================================================

TEST_CASE("extract_friendly_message parses Klipper error strings",
          "[moonraker][error]") {
    SECTION("extracts message from Python dict repr with single quotes") {
        auto result = MoonrakerError::extract_friendly_message(
            "{'error': 'WebRequestError', 'message': 'Must home axis first'}");
        REQUIRE(result == "Must home axis first");
    }

    SECTION("extracts message from JSON with double quotes and space") {
        auto result = MoonrakerError::extract_friendly_message(
            R"({"error": "WebRequestError", "message": "Must home axis first"})");
        REQUIRE(result == "Must home axis first");
    }

    SECTION("extracts message from JSON without space after colon") {
        auto result = MoonrakerError::extract_friendly_message(
            R"({"error":"WebRequestError","message":"Must home axis first"})");
        REQUIRE(result == "Must home axis first");
    }

    SECTION("returns raw string when no message key found") {
        auto result = MoonrakerError::extract_friendly_message("Some plain error text");
        REQUIRE(result == "Some plain error text");
    }

    SECTION("returns raw string for empty input") {
        auto result = MoonrakerError::extract_friendly_message("");
        REQUIRE(result == "");
    }

    SECTION("handles message with spaces and punctuation") {
        auto result = MoonrakerError::extract_friendly_message(
            "{'error': 'CommandError', 'message': 'Probe triggered prior to movement'}");
        REQUIRE(result == "Probe triggered prior to movement");
    }

    SECTION("handles message-only dict") {
        auto result = MoonrakerError::extract_friendly_message(
            "{'message': 'Timer too close'}");
        REQUIRE(result == "Timer too close");
    }
}

// ============================================================================
// from_json_rpc() integration — error message gets cleaned up
// ============================================================================

TEST_CASE("from_json_rpc extracts friendly message from Klipper errors",
          "[moonraker][error]") {
    SECTION("Klipper homing error gets cleaned up") {
        json error_obj = {
            {"code", -32603},
            {"message",
             "{'error': 'WebRequestError', 'message': 'Must home axis first'}"}};

        auto err = MoonrakerError::from_json_rpc(error_obj, "printer.gcode.script");
        REQUIRE(err.message == "Must home axis first");
        REQUIRE(err.method == "printer.gcode.script");
        REQUIRE(err.code == -32603);
    }

    SECTION("plain message passes through unchanged") {
        json error_obj = {{"code", -32601}, {"message", "Method not found"}};

        auto err = MoonrakerError::from_json_rpc(error_obj, "some.method");
        REQUIRE(err.message == "Method not found");
    }
}
