// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_database.h"

#include "../catch_amalgamated.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("length_to_weight_g: 1.75mm PLA", "[filament][conversion]") {
    // 1.75mm PLA at 1.24 g/cm^3 is the canonical ~2.98 g/m.
    float grams = filament::length_to_weight_g(1000.0f, 1.24f, 1.75f);
    REQUIRE_THAT(grams, WithinAbs(2.982f, 0.01f));
}

TEST_CASE("length_to_weight_g: 2.85mm PLA", "[filament][conversion]") {
    float grams = filament::length_to_weight_g(1000.0f, 1.24f, 2.85f);
    // 2.85mm cross-section is (2.85/1.75)^2 ≈ 2.65x bigger.
    REQUIRE_THAT(grams, WithinAbs(7.91f, 0.05f));
}

TEST_CASE("length_to_weight_g: zero length", "[filament][conversion]") {
    REQUIRE(filament::length_to_weight_g(0.0f, 1.24f, 1.75f) == 0.0f);
}

TEST_CASE("length_to_weight_g: zero density returns zero", "[filament][conversion]") {
    // Callers must pre-check density; the function returns 0 as a safe default
    // instead of propagating NaN/Inf.
    REQUIRE(filament::length_to_weight_g(100.0f, 0.0f, 1.75f) == 0.0f);
}
