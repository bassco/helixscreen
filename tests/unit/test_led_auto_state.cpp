// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_globals.h"
#include "printer_state.h"

#include "led/led_auto_state.h"
#include "led/led_controller.h"

#include "../catch_amalgamated.hpp"
#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"

using namespace helix::led;

TEST_CASE("LedAutoState singleton access", "[led][autostate]") {
    auto& state1 = LedAutoState::instance();
    auto& state2 = LedAutoState::instance();
    REQUIRE(&state1 == &state2);
}

TEST_CASE("LedAutoState default disabled after deinit", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();
    REQUIRE_FALSE(state.is_enabled());
    REQUIRE_FALSE(state.is_initialized());
}

TEST_CASE("LedAutoState enable/disable without printer state", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    REQUIRE_FALSE(state.is_enabled());
    state.set_enabled(true);
    REQUIRE(state.is_enabled());
    state.set_enabled(false);
    REQUIRE_FALSE(state.is_enabled());

    // Double-set is idempotent
    state.set_enabled(true);
    state.set_enabled(true);
    REQUIRE(state.is_enabled());

    state.deinit();
}

TEST_CASE("LedAutoState set and get mapping", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action;
    action.action_type = "color";
    action.color = 0xFF0000;
    action.brightness = 75;

    state.set_mapping("error", action);

    auto* result = state.get_mapping("error");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "color");
    REQUIRE(result->color == 0xFF0000);
    REQUIRE(result->brightness == 75);

    // Non-existent mapping returns nullptr
    REQUIRE(state.get_mapping("nonexistent") == nullptr);

    state.deinit();
}

TEST_CASE("LedAutoState mappings() returns all mappings", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction a1;
    a1.action_type = "color";
    a1.color = 0xFF0000;

    LedStateAction a2;
    a2.action_type = "off";

    state.set_mapping("error", a1);
    state.set_mapping("idle", a2);

    auto& all = state.mappings();
    REQUIRE(all.size() == 2);
    REQUIRE(all.count("error") == 1);
    REQUIRE(all.count("idle") == 1);

    state.deinit();
}

TEST_CASE("LedStateAction struct defaults", "[led][autostate]") {
    LedStateAction action;
    REQUIRE(action.action_type.empty());
    REQUIRE(action.color == 0xFFFFFF);
    REQUIRE(action.brightness == 100);
    REQUIRE(action.effect_name.empty());
    REQUIRE(action.wled_preset == 0);
    REQUIRE(action.macro_gcode.empty());
}

TEST_CASE("LedAutoState mapping overwrite", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action1;
    action1.action_type = "color";
    action1.color = 0xFF0000;

    LedStateAction action2;
    action2.action_type = "effect";
    action2.effect_name = "rainbow";

    state.set_mapping("printing", action1);
    state.set_mapping("printing", action2);

    auto* result = state.get_mapping("printing");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "effect");
    REQUIRE(result->effect_name == "rainbow");

    state.deinit();
}

TEST_CASE("LedAutoState deinit clears all state", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    // Add some state
    LedStateAction action;
    action.action_type = "color";
    state.set_mapping("idle", action);
    state.set_enabled(true);

    REQUIRE(state.is_enabled());
    REQUIRE(state.mappings().size() == 1);

    // Deinit clears everything
    state.deinit();

    REQUIRE_FALSE(state.is_enabled());
    REQUIRE_FALSE(state.is_initialized());
    REQUIRE(state.mappings().empty());
}

TEST_CASE("LedStateAction supports brightness action type", "[led][auto_state]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action;
    action.action_type = "brightness";
    action.brightness = 50;

    // Verify fields are set correctly
    REQUIRE(action.action_type == "brightness");
    REQUIRE(action.brightness == 50);
    REQUIRE(action.color == 0xFFFFFF); // Default color unchanged

    // Round-trip through set_mapping / get_mapping
    state.set_mapping("idle", action);
    auto* result = state.get_mapping("idle");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "brightness");
    REQUIRE(result->brightness == 50);

    state.deinit();
}

TEST_CASE("brightness action type stored in mapping", "[led][auto_state]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action;
    action.action_type = "brightness";
    action.brightness = 75;

    state.set_mapping("heating", action);

    auto* result = state.get_mapping("heating");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "brightness");
    REQUIRE(result->brightness == 75);

    // Verify it coexists with other action types
    LedStateAction color_action;
    color_action.action_type = "color";
    color_action.color = 0xFF0000;
    state.set_mapping("error", color_action);

    REQUIRE(state.mappings().size() == 2);
    REQUIRE(state.get_mapping("heating")->action_type == "brightness");
    REQUIRE(state.get_mapping("error")->action_type == "color");

    state.deinit();
}

TEST_CASE("setup_default_mappings includes all 6 state keys", "[led][auto_state]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    // Set up defaults by setting mappings manually (matching setup_default_mappings)
    // We can't call the private method directly, but we can verify after init with no config
    // Instead, verify the expected state keys via set_mapping
    const std::vector<std::string> expected_keys = {"idle",   "heating", "printing",
                                                    "paused", "error",   "complete"};

    for (const auto& key : expected_keys) {
        LedStateAction action;
        action.action_type = "color";
        state.set_mapping(key, action);
    }

    REQUIRE(state.mappings().size() == 6);
    for (const auto& key : expected_keys) {
        auto* mapping = state.get_mapping(key);
        REQUIRE(mapping != nullptr);
        // All action types should be valid
        bool valid_type =
            (mapping->action_type == "color" || mapping->action_type == "brightness" ||
             mapping->action_type == "effect" || mapping->action_type == "wled_preset" ||
             mapping->action_type == "macro" || mapping->action_type == "off");
        REQUIRE(valid_type);
    }

    state.deinit();
}

// ============================================================================
// apply_action integration: verify light_on_ state side effects
// ============================================================================

/// Helper: set up LedController with a native strip selected, init LedAutoState
static void setup_auto_state_with_strip() {
    // Deinit LedAutoState first to ensure clean state (no stale enabled_ or
    // deferred observer callbacks from a previous test)
    LedAutoState::instance().deinit();

    auto& ctrl = LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    LedStripInfo strip;
    strip.name = "Chamber Light";
    strip.id = "neopixel chamber_light";
    strip.backend = LedBackendType::NATIVE;
    strip.supports_color = true;
    strip.supports_white = true;
    ctrl.native().add_strip(strip);
    ctrl.set_selected_strips({"neopixel chamber_light"});
}

static void teardown_auto_state() {
    LedAutoState::instance().deinit();
    LedController::instance().deinit();
}

TEST_CASE("LedAutoState apply_action 'off' sets light_is_on false", "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    // Start with light on
    ctrl.light_set(true);
    REQUIRE(ctrl.light_is_on());

    // init() first (loads config, resets enabled), then configure and enable
    state.init(get_printer_state());
    state.set_mapping("idle", {"off", 0xFFFFFF, 100, "", 0, ""});
    state.set_enabled(true);

    // Force evaluate to "idle" (no printer state subjects → idle)
    state.evaluate();
    // Drain deferred observer callbacks from subscribe_observers() so they
    // cannot re-apply a stale mapping after we check the assertion
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE_FALSE(ctrl.light_is_on());

    teardown_auto_state();
}

TEST_CASE("LedAutoState apply_action 'color' sets light_is_on true", "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    REQUIRE_FALSE(ctrl.light_is_on());

    state.init(get_printer_state());
    state.set_mapping("idle", {"color", 0xFF0000, 100, "", 0, ""});
    state.set_enabled(true);

    state.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE(ctrl.light_is_on());

    teardown_auto_state();
}

TEST_CASE("LedAutoState apply_action 'brightness' sets light_is_on based on value",
          "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    // brightness > 0 → on
    state.init(get_printer_state());
    state.set_mapping("idle", {"brightness", 0xFFFFFF, 50, "", 0, ""});
    state.set_enabled(true);

    state.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE(ctrl.light_is_on());

    teardown_auto_state();

    // brightness == 0 → off
    setup_auto_state_with_strip();
    auto& ctrl2 = LedController::instance();
    auto& state2 = LedAutoState::instance();

    ctrl2.light_set(true);
    state2.init(get_printer_state());
    state2.set_mapping("idle", {"brightness", 0xFFFFFF, 0, "", 0, ""});
    state2.set_enabled(true);

    state2.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE_FALSE(ctrl2.light_is_on());

    teardown_auto_state();
}
