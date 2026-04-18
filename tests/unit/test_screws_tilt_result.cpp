// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "calibration_types.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("flip_screws_tilt_direction swaps CW and CCW tokens", "[calibration][screws_tilt]") {
    SECTION("CW becomes CCW") {
        std::string s = "CW 01:15";
        flip_screws_tilt_direction(s);
        REQUIRE(s == "CCW 01:15");
    }

    SECTION("CCW becomes CW") {
        std::string s = "CCW 00:30";
        flip_screws_tilt_direction(s);
        REQUIRE(s == "CW 00:30");
    }

    SECTION("Zero-turn CW preserves magnitude") {
        std::string s = "CW 00:00";
        flip_screws_tilt_direction(s);
        REQUIRE(s == "CCW 00:00");
    }

    SECTION("Non-matching prefix is untouched") {
        std::string s = "XW 01:00";
        flip_screws_tilt_direction(s);
        REQUIRE(s == "XW 01:00");
    }

    SECTION("Empty string is a no-op") {
        std::string s;
        flip_screws_tilt_direction(s);
        REQUIRE(s.empty());
    }

    SECTION("String without direction token is a no-op") {
        std::string s = "00:00";
        flip_screws_tilt_direction(s);
        REQUIRE(s == "00:00");
    }

    SECTION("Mid-string CW is not flipped") {
        std::string s = "foo CW bar";
        flip_screws_tilt_direction(s);
        REQUIRE(s == "foo CW bar");
    }

    SECTION("Round-trip flip restores original") {
        std::string s = "CW 02:45";
        flip_screws_tilt_direction(s);
        flip_screws_tilt_direction(s);
        REQUIRE(s == "CW 02:45");
    }
}

TEST_CASE("ScrewTiltResult::adjustment_minutes parses arc-minute totals",
          "[calibration][screws_tilt]") {
    ScrewTiltResult r;
    r.is_reference = false;

    SECTION("CW 00:15 is 15 minutes") {
        r.adjustment = "CW 00:15";
        REQUIRE(r.adjustment_minutes() == 15);
    }

    SECTION("CCW 01:30 is 90 minutes") {
        r.adjustment = "CCW 01:30";
        REQUIRE(r.adjustment_minutes() == 90);
    }

    SECTION("Multi-turn CW 02:45 is 165 minutes") {
        r.adjustment = "CW 02:45";
        REQUIRE(r.adjustment_minutes() == 165);
    }

    SECTION("Reference screw returns 0") {
        r.is_reference = true;
        r.adjustment = "CW 01:15"; // Should be ignored for reference
        REQUIRE(r.adjustment_minutes() == 0);
    }

    SECTION("Empty adjustment returns 0") {
        r.adjustment = "";
        REQUIRE(r.adjustment_minutes() == 0);
    }

    SECTION("Malformed adjustment returns 0") {
        r.adjustment = "garbage";
        REQUIRE(r.adjustment_minutes() == 0);
    }
}

TEST_CASE("ScrewTiltResult::is_within_tolerance treats ≤5min screws as level",
          "[calibration][screws_tilt]") {
    ScrewTiltResult r;

    SECTION("Reference screw is always within tolerance") {
        r.is_reference = true;
        r.adjustment = "CW 05:00"; // Ignored because is_reference
        REQUIRE(r.is_within_tolerance());
    }

    SECTION("CW 00:00 is within tolerance") {
        r.adjustment = "CW 00:00";
        REQUIRE(r.is_within_tolerance());
    }

    SECTION("CW 00:05 is within tolerance (boundary)") {
        r.adjustment = "CW 00:05";
        REQUIRE(r.is_within_tolerance());
    }

    SECTION("CW 00:06 is out of tolerance") {
        r.adjustment = "CW 00:06";
        REQUIRE_FALSE(r.is_within_tolerance());
    }

    SECTION("CCW 00:04 is within tolerance") {
        r.adjustment = "CCW 00:04";
        REQUIRE(r.is_within_tolerance());
    }

    SECTION("Empty adjustment treated as level") {
        r.adjustment = "";
        REQUIRE(r.is_within_tolerance());
    }
}

TEST_CASE("ScrewTiltResult::friendly_adjustment maps direction to verb",
          "[calibration][screws_tilt]") {
    ScrewTiltResult r;
    r.is_reference = false;

    SECTION("CW output becomes Tighten") {
        r.adjustment = "CW 00:18";
        REQUIRE(r.friendly_adjustment() == "Tighten 1/4 turn");
    }

    SECTION("CCW output becomes Loosen") {
        r.adjustment = "CCW 00:18";
        REQUIRE(r.friendly_adjustment() == "Loosen 1/4 turn");
    }

    SECTION("Within-tolerance screw reports Level") {
        r.adjustment = "CW 00:03";
        REQUIRE(r.friendly_adjustment() == "Level");
    }

    SECTION("Reference screw reports Reference") {
        r.is_reference = true;
        REQUIRE(r.friendly_adjustment() == "Reference");
    }

    SECTION("Multi-turn magnitudes are described") {
        r.adjustment = "CW 02:30";
        REQUIRE(r.friendly_adjustment() == "Tighten 3 turns");
    }
}
