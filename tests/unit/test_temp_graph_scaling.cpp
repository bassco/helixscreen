// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/ui_temp_graph_scaling.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Basic Behavior Tests
// ============================================================================

TEST_CASE("Y-axis scaling returns unchanged value when no scaling needed", "[scaling][basic]") {
    SECTION("Room temperature - stays at 150") {
        float result = calculate_mini_graph_y_max(150.0f, 25.0f, 25.0f);
        REQUIRE(result == 150.0f);
    }

    SECTION("Mid-range temps - stays at current max") {
        float result = calculate_mini_graph_y_max(200.0f, 100.0f, 60.0f);
        REQUIRE(result == 200.0f);
    }

    SECTION("High temps but below threshold - stays at current max") {
        // 80% of 200 = 160, so 155 shouldn't trigger expansion
        float result = calculate_mini_graph_y_max(200.0f, 155.0f, 60.0f);
        REQUIRE(result == 200.0f);
    }
}

// ============================================================================
// Expansion Tests
// ============================================================================

TEST_CASE("Y-axis expands when nozzle approaches max", "[scaling][expand]") {
    SECTION("Expand from 150 to 200 at 80% threshold") {
        // 80% of 150 = 120, so 121 should trigger expansion
        float result = calculate_mini_graph_y_max(150.0f, 121.0f, 25.0f);
        REQUIRE(result == 200.0f);
    }

    SECTION("Expand from 200 to 250") {
        // 80% of 200 = 160
        float result = calculate_mini_graph_y_max(200.0f, 165.0f, 60.0f);
        REQUIRE(result == 250.0f);
    }

    SECTION("Expand from 250 to 300") {
        // 80% of 250 = 200
        float result = calculate_mini_graph_y_max(250.0f, 205.0f, 60.0f);
        REQUIRE(result == 300.0f);
    }

    SECTION("Does not expand beyond 300") {
        float result = calculate_mini_graph_y_max(300.0f, 280.0f, 60.0f);
        REQUIRE(result == 300.0f);
    }

    SECTION("Expansion triggered by nozzle, not bed") {
        // High bed temp should NOT trigger expansion
        float result = calculate_mini_graph_y_max(150.0f, 25.0f, 140.0f);
        REQUIRE(result == 150.0f);
    }
}

// ============================================================================
// Shrink Tests
// ============================================================================

TEST_CASE("Y-axis shrinks when temps drop below threshold", "[scaling][shrink]") {
    SECTION("Shrink from 200 to 150 when temps low") {
        // Threshold: 60% of (200-50) = 60% of 150 = 90
        // Both temps must be below 90
        float result = calculate_mini_graph_y_max(200.0f, 25.0f, 25.0f);
        REQUIRE(result == 150.0f);
    }

    SECTION("Shrink from 250 to 200 when temps low") {
        // Threshold: 60% of (250-50) = 60% of 200 = 120
        float result = calculate_mini_graph_y_max(250.0f, 50.0f, 60.0f);
        REQUIRE(result == 200.0f);
    }

    SECTION("Shrink from 300 to 250 when temps low") {
        // Threshold: 60% of (300-50) = 60% of 250 = 150
        float result = calculate_mini_graph_y_max(300.0f, 100.0f, 80.0f);
        REQUIRE(result == 250.0f);
    }

    SECTION("Does not shrink below 150") {
        float result = calculate_mini_graph_y_max(150.0f, 10.0f, 10.0f);
        REQUIRE(result == 150.0f);
    }

    SECTION("Does not shrink if bed is still hot") {
        // Threshold for 200: 90°C - nozzle is cold but bed is hot
        float result = calculate_mini_graph_y_max(200.0f, 25.0f, 95.0f);
        REQUIRE(result == 200.0f);
    }

    SECTION("Does not shrink if nozzle is still hot") {
        // Threshold for 200: 90°C - bed is cold but nozzle is hot
        float result = calculate_mini_graph_y_max(200.0f, 95.0f, 25.0f);
        REQUIRE(result == 200.0f);
    }
}

// ============================================================================
// Hysteresis Tests (prevent oscillation)
// ============================================================================

TEST_CASE("Hysteresis prevents oscillation near thresholds", "[scaling][hysteresis]") {
    SECTION("Dead zone between expand and shrink thresholds") {
        // At max=200: expand threshold = 160, shrink threshold = 90
        // Temps between 90 and 160 should not change anything

        float result1 = calculate_mini_graph_y_max(200.0f, 100.0f, 60.0f);
        REQUIRE(result1 == 200.0f);

        float result2 = calculate_mini_graph_y_max(200.0f, 150.0f, 60.0f);
        REQUIRE(result2 == 200.0f);

        float result3 = calculate_mini_graph_y_max(200.0f, 155.0f, 60.0f);
        REQUIRE(result3 == 200.0f);
    }

    SECTION("After expansion, won't immediately shrink") {
        // Expand from 150 to 200 at 121°C
        float after_expand = calculate_mini_graph_y_max(150.0f, 121.0f, 25.0f);
        REQUIRE(after_expand == 200.0f);

        // Now at 200, with nozzle at 121 - should NOT shrink
        // Shrink threshold for 200 is 90°C
        float next = calculate_mini_graph_y_max(200.0f, 121.0f, 25.0f);
        REQUIRE(next == 200.0f);
    }

    SECTION("After shrink, won't immediately expand") {
        // At 200, shrink to 150 when temps at 25°C
        float after_shrink = calculate_mini_graph_y_max(200.0f, 25.0f, 25.0f);
        REQUIRE(after_shrink == 150.0f);

        // Now at 150, temp rises to 100 - should NOT expand yet
        // Expand threshold for 150 is 120°C
        float next = calculate_mini_graph_y_max(150.0f, 100.0f, 25.0f);
        REQUIRE(next == 150.0f);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Edge cases for Y-axis scaling", "[scaling][edge]") {
    SECTION("Zero temperatures") {
        float result = calculate_mini_graph_y_max(150.0f, 0.0f, 0.0f);
        REQUIRE(result == 150.0f);
    }

    SECTION("Negative temperatures (cold environment)") {
        float result = calculate_mini_graph_y_max(150.0f, -10.0f, -5.0f);
        REQUIRE(result == 150.0f);
    }

    SECTION("Exactly at expand threshold") {
        // 80% of 150 = 120 exactly
        float result = calculate_mini_graph_y_max(150.0f, 120.0f, 25.0f);
        REQUIRE(result == 150.0f); // Should NOT expand (need > threshold)
    }

    SECTION("Just above expand threshold") {
        float result = calculate_mini_graph_y_max(150.0f, 120.1f, 25.0f);
        REQUIRE(result == 200.0f); // Should expand
    }

    SECTION("Exactly at shrink threshold") {
        // 60% of (200-50) = 90 exactly
        float result = calculate_mini_graph_y_max(200.0f, 90.0f, 25.0f);
        REQUIRE(result == 200.0f); // Should NOT shrink (need < threshold)
    }

    SECTION("Just below shrink threshold") {
        float result = calculate_mini_graph_y_max(200.0f, 89.9f, 25.0f);
        REQUIRE(result == 150.0f); // Should shrink
    }

    SECTION("Very high temperature") {
        float result = calculate_mini_graph_y_max(300.0f, 500.0f, 100.0f);
        REQUIRE(result == 300.0f); // Capped at 300
    }
}

// ============================================================================
// Multi-step Scaling Tests
// ============================================================================

TEST_CASE("Multi-step scaling scenarios", "[scaling][integration]") {
    SECTION("Full heat cycle: room temp -> 300°C -> cool down") {
        float y_max = 150.0f;

        // Start at room temp
        y_max = calculate_mini_graph_y_max(y_max, 25.0f, 25.0f);
        REQUIRE(y_max == 150.0f);

        // Heat up - trigger first expansion at 121°C (80% of 150 = 120)
        y_max = calculate_mini_graph_y_max(y_max, 125.0f, 50.0f);
        REQUIRE(y_max == 200.0f);

        // Continue heating - trigger second expansion at 161°C (80% of 200 = 160)
        y_max = calculate_mini_graph_y_max(y_max, 165.0f, 60.0f);
        REQUIRE(y_max == 250.0f);

        // Continue to high temp - trigger third expansion at 201°C (80% of 250 = 200)
        y_max = calculate_mini_graph_y_max(y_max, 205.0f, 60.0f);
        REQUIRE(y_max == 300.0f);

        // At max, stabilize (can't expand further)
        y_max = calculate_mini_graph_y_max(y_max, 280.0f, 60.0f);
        REQUIRE(y_max == 300.0f);

        // Cool down to 140°C - below shrink threshold for 300 (150°C), triggers shrink to 250
        // Shrink threshold: 60% of (300-50) = 150°C
        y_max = calculate_mini_graph_y_max(y_max, 140.0f, 60.0f);
        REQUIRE(y_max == 250.0f);

        // Cool to 100°C - below shrink threshold for 250 (120°C), triggers shrink to 200
        // Shrink threshold: 60% of (250-50) = 120°C
        y_max = calculate_mini_graph_y_max(y_max, 100.0f, 40.0f);
        REQUIRE(y_max == 200.0f);

        // Cool to 50°C - below shrink threshold for 200 (90°C), triggers shrink to 150
        // Shrink threshold: 60% of (200-50) = 90°C
        y_max = calculate_mini_graph_y_max(y_max, 50.0f, 30.0f);
        REQUIRE(y_max == 150.0f);

        // Back to room temp - stays at 150
        y_max = calculate_mini_graph_y_max(y_max, 25.0f, 25.0f);
        REQUIRE(y_max == 150.0f);
    }
}
