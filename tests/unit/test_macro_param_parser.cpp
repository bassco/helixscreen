// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "favorite_macro_widget.h"

#include "../catch_amalgamated.hpp"

using helix::MacroParam;
using helix::parse_macro_params;

// ============================================================================
// parse_macro_params Tests
// ============================================================================

TEST_CASE("parse_macro_params - no params", "[macro_params]") {
    auto result = parse_macro_params("G28\nG1 X0 Y0 Z5");
    REQUIRE(result.empty());
}

TEST_CASE("parse_macro_params - empty string", "[macro_params]") {
    auto result = parse_macro_params("");
    REQUIRE(result.empty());
}

TEST_CASE("parse_macro_params - dot access", "[macro_params]") {
    auto result = parse_macro_params("{% set extruder_temp = params.EXTRUDER_TEMP %}\n"
                                     "{% set bed_temp = params.BED_TEMP %}");

    REQUIRE(result.size() == 2);
    CHECK(result[0].name == "EXTRUDER_TEMP");
    CHECK(result[1].name == "BED_TEMP");
}

TEST_CASE("parse_macro_params - bracket access single quotes", "[macro_params]") {
    auto result = parse_macro_params("{% set temp = params['EXTRUDER'] %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "EXTRUDER");
}

TEST_CASE("parse_macro_params - bracket access double quotes", "[macro_params]") {
    auto result = parse_macro_params(R"({% set temp = params["BED_TEMP"] %})");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "BED_TEMP");
}

TEST_CASE("parse_macro_params - with default values", "[macro_params]") {
    auto result = parse_macro_params("{% set extruder_temp = params.EXTRUDER_TEMP|default(220) %}\n"
                                     "{% set bed_temp = params.BED_TEMP|default(60) %}");

    REQUIRE(result.size() == 2);
    CHECK(result[0].name == "EXTRUDER_TEMP");
    CHECK(result[0].default_value == "220");
    CHECK(result[1].name == "BED_TEMP");
    CHECK(result[1].default_value == "60");
}

TEST_CASE("parse_macro_params - default with space before pipe", "[macro_params]") {
    auto result = parse_macro_params("{% set speed = params.SPEED | default(100) %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "SPEED");
    CHECK(result[0].default_value == "100");
}

TEST_CASE("parse_macro_params - string default with quotes", "[macro_params]") {
    auto result = parse_macro_params(R"({% set material = params.MATERIAL|default('PLA') %})");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "MATERIAL");
    CHECK(result[0].default_value == "PLA");
}

TEST_CASE("parse_macro_params - deduplication", "[macro_params]") {
    auto result = parse_macro_params("{% set temp = params.TEMP %}\n"
                                     "{% if params.TEMP > 200 %}\n"
                                     "M104 S{params.TEMP}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "TEMP");
}

TEST_CASE("parse_macro_params - mixed dot and bracket access", "[macro_params]") {
    auto result = parse_macro_params("{% set temp = params.EXTRUDER_TEMP|default(200) %}\n"
                                     "{% set bed = params['BED_TEMP']|default(60) %}\n"
                                     R"({% set material = params["MATERIAL"] %})");

    REQUIRE(result.size() == 3);

    // Check all names present (order may vary between dot and bracket)
    std::set<std::string> names;
    for (const auto& p : result) {
        names.insert(p.name);
    }
    CHECK(names.count("EXTRUDER_TEMP") == 1);
    CHECK(names.count("BED_TEMP") == 1);
    CHECK(names.count("MATERIAL") == 1);
}

TEST_CASE("parse_macro_params - cross-syntax dedup", "[macro_params]") {
    // Same param referenced via both dot and bracket
    auto result = parse_macro_params("{% set t = params.TEMP %}\n"
                                     "{% set t2 = params['TEMP'] %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "TEMP");
}

TEST_CASE("parse_macro_params - case normalization", "[macro_params]") {
    auto result = parse_macro_params("{% set t = params.temp %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "TEMP");
}

TEST_CASE("parse_macro_params - real-world PRINT_START", "[macro_params]") {
    std::string gcode = R"(
{% set extruder_temp = params.EXTRUDER_TEMP|default(200)|float %}
{% set bed_temp = params.BED_TEMP|default(60)|float %}
{% set chamber_temp = params.CHAMBER_TEMP|default(0)|float %}
{% set filament_type = params.FILAMENT_TYPE|default('PLA') %}
M140 S{bed_temp}
M104 S{extruder_temp}
{% if chamber_temp > 0 %}
  M141 S{chamber_temp}
{% endif %}
)";

    auto result = parse_macro_params(gcode);
    REQUIRE(result.size() == 4);

    // Find specific params
    std::map<std::string, std::string> param_map;
    for (const auto& p : result) {
        param_map[p.name] = p.default_value;
    }

    CHECK(param_map["EXTRUDER_TEMP"] == "200");
    CHECK(param_map["BED_TEMP"] == "60");
    CHECK(param_map["CHAMBER_TEMP"] == "0");
    CHECK(param_map["FILAMENT_TYPE"] == "PLA");
}

// ============================================================================
// 'in params' conditional pattern tests
// ============================================================================

TEST_CASE("parse_macro_params - single-quoted in params", "[macro_params]") {
    auto result = parse_macro_params("{% if 'PURGE_LINE' in params %}\n"
                                     "  PURGE\n"
                                     "{% endif %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "PURGE_LINE");
    CHECK(result[0].default_value.empty());
}

TEST_CASE("parse_macro_params - double-quoted in params", "[macro_params]") {
    auto result = parse_macro_params(R"({% if "BED_TEMP" in params %})");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "BED_TEMP");
}

TEST_CASE("parse_macro_params - not in params", "[macro_params]") {
    auto result = parse_macro_params("{% if 'SKIP_HEAT' not in params %}\n"
                                     "  M104 S200\n"
                                     "{% endif %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "SKIP_HEAT");
}

TEST_CASE("parse_macro_params - in params dedup with dot access", "[macro_params]") {
    // Dot access finds BED_TEMP with default; conditional should not add duplicate
    auto result = parse_macro_params("{% set bed = params.BED_TEMP|default(60) %}\n"
                                     "{% if 'BED_TEMP' in params %}\n"
                                     "  ; override\n"
                                     "{% endif %}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "BED_TEMP");
    CHECK(result[0].default_value == "60"); // Keeps dot-access default, not clobbered
}

TEST_CASE("parse_macro_params - in params mixed with dot access", "[macro_params]") {
    std::string gcode = R"(
{% set bed_temp = params.BED_TEMP|default(60)|float %}
{% if 'PURGE_LINE' in params %}
  PURGE
{% endif %}
{% if "CHAMBER_TEMP" in params %}
  M141 S{params.CHAMBER_TEMP}
{% endif %}
)";

    auto result = parse_macro_params(gcode);
    REQUIRE(result.size() == 3);

    std::map<std::string, std::string> param_map;
    for (const auto& p : result) {
        param_map[p.name] = p.default_value;
    }

    CHECK(param_map.count("BED_TEMP") == 1);
    CHECK(param_map["BED_TEMP"] == "60");
    CHECK(param_map.count("PURGE_LINE") == 1);
    CHECK(param_map["PURGE_LINE"].empty());
    CHECK(param_map.count("CHAMBER_TEMP") == 1);
}

TEST_CASE("parse_macro_params - no default value", "[macro_params]") {
    auto result = parse_macro_params("{% set temp = params.TEMP %}\n"
                                     "M104 S{temp}");

    REQUIRE(result.size() == 1);
    CHECK(result[0].name == "TEMP");
    CHECK(result[0].default_value.empty());
}
