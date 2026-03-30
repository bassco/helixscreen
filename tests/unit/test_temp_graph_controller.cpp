// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/temp_graph_controller.h"
#include "../../include/ui_temp_graph.h"
#include "../ui_test_utils.h"
#include "lvgl/lvgl.h"

#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Test Fixture
// ============================================================================

class TempGraphControllerFixture {
  public:
    TempGraphControllerFixture() {
        lv_init_safe();

        lv_display_t* disp = lv_display_create(800, 480);
        alignas(64) static lv_color_t buf1[800 * 10];
        lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

        screen = lv_obj_create(NULL);

        // Initialize PrinterState subjects (needed by controller's setup_observers)
        get_printer_state().init_subjects(false);
    }

    ~TempGraphControllerFixture() {}

    lv_obj_t* screen;
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller creates graph with minimal config",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;
    cfg.series.clear(); // No series — avoids printer state lookups for sensors

    auto controller = std::make_unique<TempGraphController>(screen, cfg);

    REQUIRE(controller->is_valid());
    REQUIRE(controller->graph() != nullptr);
}

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller with series specs returns valid IDs",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;
    cfg.series = {
        {"extruder", lv_color_hex(0xFF4444), true},
        {"heater_bed", lv_color_hex(0x88C0D0), true},
    };

    auto controller = std::make_unique<TempGraphController>(screen, cfg);

    REQUIRE(controller->is_valid());

    // series_id_for should return valid (>= 0) IDs for added series
    int extruder_id = controller->series_id_for("extruder");
    int bed_id = controller->series_id_for("heater_bed");
    REQUIRE(extruder_id >= 0);
    REQUIRE(bed_id >= 0);
    REQUIRE(extruder_id != bed_id);

    // Nonexistent series returns -1
    REQUIRE(controller->series_id_for("nonexistent_sensor") == -1);
}

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller pause and resume do not crash",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;

    auto controller = std::make_unique<TempGraphController>(screen, cfg);
    REQUIRE(controller->is_valid());

    REQUIRE_NOTHROW(controller->pause());
    REQUIRE_NOTHROW(controller->resume());
    REQUIRE_NOTHROW(controller->pause());
    REQUIRE_NOTHROW(controller->resume());
}

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller set_features applies feature flags",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;

    auto controller = std::make_unique<TempGraphController>(screen, cfg);
    REQUIRE(controller->is_valid());

    // Set a reduced feature set (lines are always forced on)
    uint32_t features = TEMP_GRAPH_FEATURE_LINES | TEMP_GRAPH_FEATURE_Y_AXIS;
    controller->set_features(features);

    uint32_t active = ui_temp_graph_get_features(controller->graph());
    // LINES is always forced on
    REQUIRE((active & TEMP_GRAPH_FEATURE_LINES) != 0);
    REQUIRE((active & TEMP_GRAPH_FEATURE_Y_AXIS) != 0);
    // Features we did NOT set should be off
    REQUIRE((active & TEMP_GRAPH_FEATURE_X_AXIS) == 0);
    REQUIRE((active & TEMP_GRAPH_FEATURE_GRADIENTS) == 0);
}

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller rebuild keeps graph valid and series intact",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;
    cfg.series = {
        {"extruder", lv_color_hex(0xFF4444), true},
        {"heater_bed", lv_color_hex(0x88C0D0), true},
    };

    auto controller = std::make_unique<TempGraphController>(screen, cfg);
    REQUIRE(controller->is_valid());
    REQUIRE(controller->series_id_for("extruder") >= 0);

    // Rebuild should recreate graph and series without crash
    REQUIRE_NOTHROW(controller->rebuild());

    REQUIRE(controller->is_valid());
    REQUIRE(controller->graph() != nullptr);
    REQUIRE(controller->series_id_for("extruder") >= 0);
    REQUIRE(controller->series_id_for("heater_bed") >= 0);
}

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller with custom scale params does not crash",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;
    cfg.scale_params.step = 25.0f;
    cfg.scale_params.floor = 100.0f;
    cfg.scale_params.ceiling = 400.0f;
    cfg.scale_params.expand_threshold = 0.85f;
    cfg.scale_params.shrink_threshold = 0.55f;

    auto controller = std::make_unique<TempGraphController>(screen, cfg);
    REQUIRE(controller->is_valid());
    REQUIRE(controller->graph() != nullptr);
}

TEST_CASE_METHOD(TempGraphControllerFixture,
                 "Controller destruction with series is safe",
                 "[controller][temp_graph_controller]") {
    TempGraphControllerConfig cfg;
    cfg.series = {
        {"extruder", lv_color_hex(0xFF4444), true},
        {"heater_bed", lv_color_hex(0x88C0D0), true},
    };

    // Create with active series and observers, then immediately destroy
    auto controller = std::make_unique<TempGraphController>(screen, cfg);
    REQUIRE(controller->is_valid());

    // Destruction tears down observers, drains queue, destroys graph
    REQUIRE_NOTHROW(controller.reset());
    REQUIRE(controller == nullptr);
}
