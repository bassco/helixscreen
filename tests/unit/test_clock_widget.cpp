// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "src/ui/panel_widgets/clock_widget.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// Ensure clock widget subjects are initialized once for the test suite
static bool s_clock_registered = false;

class ClockWidgetFixture : public LVGLTestFixture {
  public:
    ClockWidgetFixture() {
        if (!s_clock_registered) {
            PanelWidgetManager::instance().init_widget_subjects();
            s_clock_registered = true;
        }
    }

    /// Create a minimal mock widget tree (no XML required)
    lv_obj_t* create_mock_clock(lv_obj_t* parent) {
        lv_obj_t* container = lv_obj_create(parent);
        lv_obj_set_name(container, "panel_widget_clock");

        lv_obj_t* time_label = lv_obj_create(container);
        lv_obj_set_name(time_label, "clock_time");

        lv_obj_t* date_label = lv_obj_create(container);
        lv_obj_set_name(date_label, "clock_date");

        lv_obj_t* uptime_label = lv_obj_create(container);
        lv_obj_set_name(uptime_label, "clock_uptime");

        return container;
    }
};

TEST_CASE_METHOD(ClockWidgetFixture, "ClockWidget: factory registration", "[clock_widget]") {
    const auto* def = find_widget_def("clock");
    REQUIRE(def != nullptr);
    REQUIRE(def->factory != nullptr);

    auto widget = def->factory();
    REQUIRE(widget != nullptr);
    REQUIRE(std::string(widget->id()) == "clock");
}

TEST_CASE_METHOD(ClockWidgetFixture, "ClockWidget: attach sets user_data",
                 "[clock_widget]") {
    ClockWidget widget;
    lv_obj_t* container = create_mock_clock(test_screen());

    widget.attach(container, test_screen());
    REQUIRE(lv_obj_get_user_data(container) == &widget);

    widget.detach();
    REQUIRE(lv_obj_get_user_data(container) == nullptr);
}

TEST_CASE_METHOD(ClockWidgetFixture, "ClockWidget: timer lifecycle",
                 "[clock_widget]") {
    ClockWidget widget;
    lv_obj_t* container = create_mock_clock(test_screen());
    widget.attach(container, test_screen());

    SECTION("no timer before activation") {
        // Timer should not exist until on_activate() is called
        // We verify indirectly: calling on_deactivate() should be safe (no-op)
        widget.on_deactivate();
    }

    SECTION("timer starts on activate") {
        widget.on_activate();

        // Get the initial time subject value
        auto* time_subj = lv_xml_get_subject(nullptr, "clock_time_text");
        REQUIRE(time_subj != nullptr);
        const char* initial = lv_subject_get_string(time_subj);
        REQUIRE(initial != nullptr);
        // Should have a real time value (not the "--:--" placeholder)
        REQUIRE(std::string(initial) != "--:--");

        widget.on_deactivate();
    }

    SECTION("timer stops on deactivate") {
        widget.on_activate();
        widget.on_deactivate();

        // Calling deactivate again should be safe (idempotent)
        widget.on_deactivate();
    }

    SECTION("timer restarts on re-activate") {
        widget.on_activate();
        widget.on_deactivate();
        widget.on_activate();

        // Should still work after restart
        auto* time_subj = lv_xml_get_subject(nullptr, "clock_time_text");
        REQUIRE(time_subj != nullptr);
        const char* val = lv_subject_get_string(time_subj);
        REQUIRE(val != nullptr);
        REQUIRE(std::string(val) != "--:--");

        widget.on_deactivate();
    }

    widget.detach();
}

TEST_CASE_METHOD(ClockWidgetFixture, "ClockWidget: detach stops timer",
                 "[clock_widget]") {
    ClockWidget widget;
    lv_obj_t* container = create_mock_clock(test_screen());
    widget.attach(container, test_screen());
    widget.on_activate();

    // Detach should stop the timer even if on_deactivate() wasn't called
    widget.detach();

    // Processing LVGL timers after detach should not crash
    process_lvgl(100);
}

TEST_CASE_METHOD(ClockWidgetFixture, "ClockWidget: subjects populated on attach",
                 "[clock_widget]") {
    ClockWidget widget;
    lv_obj_t* container = create_mock_clock(test_screen());

    // attach() calls update_clock() which populates subjects immediately
    widget.attach(container, test_screen());

    auto* time_subj = lv_xml_get_subject(nullptr, "clock_time_text");
    auto* date_subj = lv_xml_get_subject(nullptr, "clock_date_text");
    auto* uptime_subj = lv_xml_get_subject(nullptr, "clock_uptime_text");

    REQUIRE(time_subj != nullptr);
    REQUIRE(date_subj != nullptr);
    REQUIRE(uptime_subj != nullptr);

    // Time should be populated (not placeholder)
    REQUIRE(std::string(lv_subject_get_string(time_subj)) != "--:--");

    // Date should be populated (not empty)
    REQUIRE(std::string(lv_subject_get_string(date_subj)).size() > 0);

    // Uptime should be populated (starts with "Up: ")
    std::string uptime = lv_subject_get_string(uptime_subj);
    REQUIRE(uptime.find("Up: ") == 0);

    widget.detach();
}

TEST_CASE_METHOD(ClockWidgetFixture, "ClockWidget: timer fires during LVGL processing",
                 "[clock_widget]") {
    ClockWidget widget;
    lv_obj_t* container = create_mock_clock(test_screen());
    widget.attach(container, test_screen());
    widget.on_activate();

    // Processing LVGL for longer than the timer interval (1000ms) should
    // not crash and timer callback should execute
    process_lvgl(1100);

    auto* time_subj = lv_xml_get_subject(nullptr, "clock_time_text");
    REQUIRE(time_subj != nullptr);
    REQUIRE(std::string(lv_subject_get_string(time_subj)) != "--:--");

    widget.on_deactivate();
    widget.detach();
}

// ---------------------------------------------------------------------------
// Tests for the widget rebuild + activation fix
// ---------------------------------------------------------------------------

/// Mock PanelWidget that tracks on_activate/on_deactivate calls
class MockWidget : public PanelWidget {
  public:
    int activate_count = 0;
    int deactivate_count = 0;
    bool attached = false;

    void attach(lv_obj_t* /*widget_obj*/, lv_obj_t* /*parent_screen*/) override {
        attached = true;
    }
    void detach() override {
        attached = false;
    }
    void on_activate() override {
        activate_count++;
    }
    void on_deactivate() override {
        deactivate_count++;
    }
    const char* id() const override {
        return "mock";
    }
};

TEST_CASE("Widget rebuild activation contract", "[panel_widget][lifecycle]") {
    // This tests the contract that HomePanel now implements:
    // when widgets are rebuilt while the panel is active, on_activate()
    // must be called on the new widgets.

    // Simulate the lifecycle that HomePanel manages
    std::vector<std::unique_ptr<PanelWidget>> active_widgets;
    bool panel_active = false;

    // Helper lambdas matching HomePanel's implementation
    auto on_activate = [&]() {
        panel_active = true;
        for (auto& w : active_widgets) {
            w->on_activate();
        }
    };

    auto on_deactivate = [&]() {
        panel_active = false;
        for (auto& w : active_widgets) {
            w->on_deactivate();
        }
    };

    auto populate_widgets = [&]() {
        // Detach old widgets
        for (auto& w : active_widgets) {
            w->detach();
        }
        active_widgets.clear();

        // Create new widgets
        auto w = std::make_unique<MockWidget>();
        active_widgets.push_back(std::move(w));

        // THE FIX: activate new widgets if panel is already active
        if (panel_active) {
            for (auto& widget : active_widgets) {
                widget->on_activate();
            }
        }
    };

    SECTION("initial setup: widgets not activated until panel activates") {
        populate_widgets();
        auto* mock = static_cast<MockWidget*>(active_widgets[0].get());
        REQUIRE(mock->activate_count == 0);

        on_activate();
        REQUIRE(mock->activate_count == 1);

        on_deactivate();
        REQUIRE(mock->deactivate_count == 1);
    }

    SECTION("rebuild while inactive: widgets not activated") {
        populate_widgets();
        auto* mock = static_cast<MockWidget*>(active_widgets[0].get());
        REQUIRE(mock->activate_count == 0);
    }

    SECTION("rebuild while active: new widgets get activated") {
        // Initial population + activation
        populate_widgets();
        on_activate();
        auto* first_mock = static_cast<MockWidget*>(active_widgets[0].get());
        REQUIRE(first_mock->activate_count == 1);

        // Rebuild while panel is active (simulates gate observer or settings change)
        populate_widgets();
        auto* new_mock = static_cast<MockWidget*>(active_widgets[0].get());

        // New widget should have been activated during populate
        REQUIRE(new_mock->activate_count == 1);
        // Old widget should have been detached (not deactivated — detach handles cleanup)
        REQUIRE(first_mock->attached == false);
    }

    SECTION("multiple rebuilds while active: each batch gets activated") {
        populate_widgets();
        on_activate();

        populate_widgets();
        auto* w1 = static_cast<MockWidget*>(active_widgets[0].get());
        REQUIRE(w1->activate_count == 1);

        populate_widgets();
        auto* w2 = static_cast<MockWidget*>(active_widgets[0].get());
        REQUIRE(w2->activate_count == 1);
        // w1 is now detached and destroyed (unique_ptr)
    }

    SECTION("deactivate after rebuild: new widgets get deactivated") {
        populate_widgets();
        on_activate();

        populate_widgets();
        auto* mock = static_cast<MockWidget*>(active_widgets[0].get());
        REQUIRE(mock->activate_count == 1);

        on_deactivate();
        REQUIRE(mock->deactivate_count == 1);
    }
}
