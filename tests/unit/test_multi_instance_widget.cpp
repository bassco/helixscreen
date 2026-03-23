// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "panel_widget.h"
#include "panel_widget_config.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("find_widget_def resolves multi-instance IDs", "[panel_widget][multi_instance]") {
    SECTION("Exact match still works for single-instance") {
        REQUIRE(find_widget_def("power") != nullptr);
        REQUIRE(std::string(find_widget_def("power")->id) == "power");
    }

    SECTION("Colon on non-multi_instance def returns nullptr") {
        // "power" exists but is not multi_instance, so "power:1" should fail
        auto* power_def = find_widget_def("power");
        REQUIRE(power_def != nullptr);
        REQUIRE(power_def->multi_instance == false);

        REQUIRE(find_widget_def("power:1") == nullptr);
        REQUIRE(find_widget_def("power:42") == nullptr);
    }

    SECTION("Non-existent base returns nullptr") {
        REQUIRE(find_widget_def("nonexistent:1") == nullptr);
        REQUIRE(find_widget_def("nonexistent") == nullptr);
    }

    SECTION("Plain colon without digits returns nullptr for non-multi_instance") {
        REQUIRE(find_widget_def("power:") == nullptr);
        REQUIRE(find_widget_def("power:abc") == nullptr);
    }

    SECTION("Multi-instance base ID resolves") {
        const auto* fav = find_widget_def("favorite_macro");
        REQUIRE(fav != nullptr);
        REQUIRE(fav->multi_instance == true);
    }

    SECTION("Colon-suffixed ID resolves to base def") {
        const auto* fav_inst = find_widget_def("favorite_macro:1");
        REQUIRE(fav_inst != nullptr);
        REQUIRE(std::string(fav_inst->id) == "favorite_macro");
        REQUIRE(fav_inst->multi_instance == true);
    }
}

TEST_CASE("WidgetFactory receives instance ID", "[panel_widget][multi_instance]") {
    init_widget_registrations();

    const auto* def = find_widget_def("favorite_macro");
    REQUIRE(def != nullptr);
    REQUIRE(def->multi_instance == true);
    REQUIRE(def->factory != nullptr);

    auto widget = def->factory("favorite_macro:7");
    REQUIRE(widget != nullptr);
    REQUIRE(std::string(widget->id()) == "favorite_macro:7");
}

// ============================================================================
// Config migration fixture
// ============================================================================

namespace helix {
class MultiInstanceMigrationFixture {
  protected:
    Config config;

    void setup_with_widgets(const json& widgets_json, const std::string& panel_id = "home") {
        config.data = json::object();
        config.data["printers"]["default"]["panel_widgets"][panel_id] = widgets_json;
    }
};
} // namespace helix

TEST_CASE_METHOD(helix::MultiInstanceMigrationFixture,
                 "Config migration: favorite_macro_N to favorite_macro:N",
                 "[panel_widget][multi_instance]") {
    // Old-style config with favorite_macro_1 and favorite_macro_3
    json widgets = json::array();
    widgets.push_back({{"id", "favorite_macro_1"},
                       {"enabled", true},
                       {"config", {{"macro_name", "CLEAN_NOZZLE"}}},
                       {"col", 0},
                       {"row", 0},
                       {"colspan", 1},
                       {"rowspan", 1}});
    widgets.push_back({{"id", "favorite_macro_3"},
                       {"enabled", false},
                       {"config", {{"macro_name", "HOME_ALL"}}},
                       {"col", 1},
                       {"row", 0},
                       {"colspan", 1},
                       {"rowspan", 1}});

    setup_with_widgets(widgets);

    PanelWidgetConfig pwc("home", config);
    pwc.load();

    const auto& entries = pwc.entries();

    // Find migrated entries
    auto find_entry = [&](const std::string& id) -> const PanelWidgetEntry* {
        for (const auto& e : entries) {
            if (e.id == id)
                return &e;
        }
        return nullptr;
    };

    // Old IDs should not exist
    REQUIRE(find_entry("favorite_macro_1") == nullptr);
    REQUIRE(find_entry("favorite_macro_3") == nullptr);

    // New IDs should exist with preserved config
    const auto* e1 = find_entry("favorite_macro:1");
    REQUIRE(e1 != nullptr);
    REQUIRE(e1->enabled == true);
    REQUIRE(e1->config.value("macro_name", "") == "CLEAN_NOZZLE");

    const auto* e3 = find_entry("favorite_macro:3");
    REQUIRE(e3 != nullptr);
    REQUIRE(e3->enabled == false);
    REQUIRE(e3->config.value("macro_name", "") == "HOME_ALL");
}

TEST_CASE_METHOD(helix::MultiInstanceMigrationFixture,
                 "mint_instance_id generates monotonically increasing IDs",
                 "[panel_widget][multi_instance]") {
    SECTION("Empty config mints :1") {
        json widgets = json::array();
        // Add a non-multi-instance widget so config isn't totally empty
        widgets.push_back({{"id", "power"},
                           {"enabled", true},
                           {"col", 0},
                           {"row", 0},
                           {"colspan", 1},
                           {"rowspan", 1}});
        setup_with_widgets(widgets);
        PanelWidgetConfig pwc("home", config);
        pwc.load();

        REQUIRE(pwc.mint_instance_id("favorite_macro") == "favorite_macro:1");
    }

    SECTION("With :1 existing, mints :2") {
        json widgets = json::array();
        widgets.push_back({{"id", "favorite_macro:1"},
                           {"enabled", true},
                           {"col", 0},
                           {"row", 0},
                           {"colspan", 1},
                           {"rowspan", 1}});
        setup_with_widgets(widgets);
        PanelWidgetConfig pwc("home", config);
        pwc.load();

        REQUIRE(pwc.mint_instance_id("favorite_macro") == "favorite_macro:2");
    }

    SECTION("With gap (:1 and :5), mints :6 (monotonic)") {
        json widgets = json::array();
        widgets.push_back({{"id", "favorite_macro:1"},
                           {"enabled", true},
                           {"col", 0},
                           {"row", 0},
                           {"colspan", 1},
                           {"rowspan", 1}});
        widgets.push_back({{"id", "favorite_macro:5"},
                           {"enabled", true},
                           {"col", 1},
                           {"row", 0},
                           {"colspan", 1},
                           {"rowspan", 1}});
        setup_with_widgets(widgets);
        PanelWidgetConfig pwc("home", config);
        pwc.load();

        REQUIRE(pwc.mint_instance_id("favorite_macro") == "favorite_macro:6");
    }
}

TEST_CASE_METHOD(helix::MultiInstanceMigrationFixture, "delete_entry removes entry entirely",
                 "[panel_widget][multi_instance]") {
    json widgets = json::array();
    widgets.push_back({{"id", "favorite_macro:1"},
                       {"enabled", true},
                       {"col", 0},
                       {"row", 0},
                       {"colspan", 1},
                       {"rowspan", 1}});
    widgets.push_back({{"id", "favorite_macro:2"},
                       {"enabled", true},
                       {"col", 1},
                       {"row", 0},
                       {"colspan", 1},
                       {"rowspan", 1}});
    widgets.push_back({{"id", "power"},
                       {"enabled", true},
                       {"col", 2},
                       {"row", 0},
                       {"colspan", 1},
                       {"rowspan", 1}});
    setup_with_widgets(widgets);
    PanelWidgetConfig pwc("home", config);
    pwc.load();

    auto find_entry = [&](const std::string& id) -> const PanelWidgetEntry* {
        for (const auto& e : pwc.entries()) {
            if (e.id == id)
                return &e;
        }
        return nullptr;
    };

    // Precondition: all three exist
    REQUIRE(find_entry("favorite_macro:1") != nullptr);
    REQUIRE(find_entry("favorite_macro:2") != nullptr);
    REQUIRE(find_entry("power") != nullptr);

    pwc.delete_entry("favorite_macro:1");

    // :1 is gone entirely
    REQUIRE(find_entry("favorite_macro:1") == nullptr);
    // Others still present
    REQUIRE(find_entry("favorite_macro:2") != nullptr);
    REQUIRE(find_entry("power") != nullptr);
}
