// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui_test_utils.h"
#include "panel_widget.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "ui_update_queue.h"

#include "helix-xml/src/xml/lv_xml.h"
#include "misc/lv_timer_private.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Drain LVGL's async list (lv_async_call queue) by calling lv_timer_handler
/// repeatedly until no one-shot timer fires. lv_async_call schedules its
/// callback as a one-shot timer; lv_timer_handler dispatches it.
void process_async_calls() {
    for (int safety = 0; safety < 50; ++safety) {
        bool fired = false;
        lv_timer_t* t = lv_timer_get_next(nullptr);
        while (t) {
            lv_timer_t* next = lv_timer_get_next(t);
            if (t->repeat_count > 0 && t->timer_cb) {
                t->timer_cb(t);
                fired = true;
                break;
            }
            t = next;
        }
        if (!fired)
            break;
    }
}

} // namespace

TEST_CASE("PanelWidget: supports_reuse defaults to true", "[panel_widget]") {
    struct TestWidget : PanelWidget {
        void attach(lv_obj_t*, lv_obj_t*) override {}
        void detach() override {}
        const char* id() const override { return "test"; }
    };
    TestWidget w;
    REQUIRE(w.supports_reuse() == true);
}

TEST_CASE("PanelWidgetManager singleton access", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    auto& mgr2 = PanelWidgetManager::instance();
    REQUIRE(&mgr == &mgr2);
}

TEST_CASE("PanelWidgetManager shared resources", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    mgr.clear_shared_resources();

    SECTION("returns nullptr for unregistered type") {
        REQUIRE(mgr.shared_resource<int>() == nullptr);
    }

    SECTION("register and retrieve") {
        auto val = std::make_shared<int>(42);
        mgr.register_shared_resource<int>(val);
        REQUIRE(mgr.shared_resource<int>() != nullptr);
        REQUIRE(*mgr.shared_resource<int>() == 42);
    }

    SECTION("clear removes all resources") {
        auto val = std::make_shared<int>(99);
        mgr.register_shared_resource<int>(val);
        mgr.clear_shared_resources();
        REQUIRE(mgr.shared_resource<int>() == nullptr);
    }

    SECTION("multiple types coexist") {
        auto i = std::make_shared<int>(10);
        auto s = std::make_shared<std::string>("hello");
        mgr.register_shared_resource<int>(i);
        mgr.register_shared_resource<std::string>(s);
        REQUIRE(*mgr.shared_resource<int>() == 10);
        REQUIRE(*mgr.shared_resource<std::string>() == "hello");
        mgr.clear_shared_resources();
    }
}

TEST_CASE("PanelWidgetManager config change callbacks", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();

    SECTION("callback is invoked on notify") {
        bool called = false;
        mgr.register_rebuild_callback("test_panel", [&called]() { called = true; });
        mgr.notify_config_changed("test_panel");
        REQUIRE(called);
        mgr.unregister_rebuild_callback("test_panel");
    }

    SECTION("notify for nonexistent panel does not crash") {
        mgr.notify_config_changed("nonexistent");
    }

    SECTION("unregister removes callback") {
        int count = 0;
        mgr.register_rebuild_callback("counting", [&count]() { count++; });
        mgr.notify_config_changed("counting");
        REQUIRE(count == 1);
        mgr.unregister_rebuild_callback("counting");
        mgr.notify_config_changed("counting");
        REQUIRE(count == 1);
    }
}

TEST_CASE("PanelWidgetManager populate with null container", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    auto widgets = mgr.populate_widgets("home", nullptr);
    REQUIRE(widgets.empty());
}

TEST_CASE("Widget factories are self-registered", "[panel_widget][self_registration]") {
    lv_init_safe(); // Widget registration requires LVGL for XML event callbacks
    helix::init_widget_registrations();

    const char* expected[] = {"temperature", "temp_stack", "led",      "power_device",
                              "network",     "thermistor", "fan_stack"};
    for (const auto* id : expected) {
        INFO("Checking widget factory: " << id);
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        REQUIRE(def->factory != nullptr);
    }
}

TEST_CASE("PanelWidgetManager raw pointer shared resources", "[panel_widget][manager]") {
    auto& mgr = PanelWidgetManager::instance();
    mgr.clear_shared_resources();

    int stack_val = 77;
    mgr.register_shared_resource<int>(&stack_val);
    REQUIRE(mgr.shared_resource<int>() != nullptr);
    REQUIRE(*mgr.shared_resource<int>() == 77);
    mgr.clear_shared_resources();
}

// Regression test for AD5X bundles XG9QJ3V9 / PFEHDEXF (v0.99.49):
// SIGBUS in unsubscribe_on_delete_cb -> lv_obj_remove_event_cb_with_user_data
// during early startup, after a burst of 8 hardware-gate subjects fired in
// ~150 ms. Each firing triggered populate_page synchronously in the same
// UpdateQueue tick; the resulting backlog of N×children async deletes
// corrupted LVGL's event list (L081 family).
//
// Fix: setup_gate_observers now coalesces multiple firings within a tick
// into a single async-deferred rebuild via lv_async_call. This test
// verifies the coalescing invariant: regardless of how many gate observers
// fire in one drain, rebuild_cb runs at most once.
TEST_CASE("PanelWidgetManager coalesces multiple gate firings into one rebuild",
          "[panel_widget][manager][regression][L081]") {
    lv_init_safe();
    helix::init_widget_registrations();

    auto& mgr = PanelWidgetManager::instance();

    // Register klippy_state (always observed by setup_gate_observers) plus
    // the hardware_gate_subject for every registered widget def. Any subject
    // that doesn't already exist gets created here so the observer chain has
    // something to attach to. nullptr scope = global.
    static lv_subject_t klippy_state_subj;
    if (!lv_xml_get_subject(nullptr, "klippy_state")) {
        lv_subject_init_int(&klippy_state_subj, 0);
        lv_xml_register_subject(nullptr, "klippy_state", &klippy_state_subj);
    }

    // Static so registrations persist across SECTION re-entries; otherwise the
    // second SECTION sees the global registrations from the first but its
    // local vector is empty and we can't iterate to set values.
    static std::vector<std::pair<std::string, lv_subject_t*>> all_gate_subjs = []() {
        std::vector<std::pair<std::string, lv_subject_t*>> out;
        for (const auto& def : helix::get_all_widget_defs()) {
            if (!def.hardware_gate_subject)
                continue;
            const char* name = def.hardware_gate_subject;
            // De-dup
            bool dup = false;
            for (const auto& kv : out) {
                if (kv.first == name) { dup = true; break; }
            }
            if (dup) continue;
            if (auto* existing = lv_xml_get_subject(nullptr, name)) {
                out.emplace_back(name, existing);
            } else {
                auto* subj = new lv_subject_t;
                lv_subject_init_int(subj, 0);
                lv_xml_register_subject(nullptr, name, subj);
                out.emplace_back(name, subj);
            }
        }
        return out;
    }();
    REQUIRE(all_gate_subjs.size() >= 2); // need at least 2 to test coalescing

    int rebuild_count = 0;
    mgr.setup_gate_observers("test_panel", [&rebuild_count]() { ++rebuild_count; });

    auto& q = helix::ui::UpdateQueue::instance();

    SECTION("burst of N firings produces 1 rebuild") {
        // Set every gate subject to a new value. Each set fires the observer's
        // queue_update; combined with the immediate-fire from registration,
        // we get 2N callbacks queued. Without coalescing, each would fire its
        // own rebuild_cb — causing the L081 backlog corruption seen on AD5X.
        if (auto* ks = lv_xml_get_subject(nullptr, "klippy_state"))
            lv_subject_set_int(ks, 1);
        for (auto& [name, subj] : all_gate_subjs) {
            lv_subject_set_int(subj, 1);
        }

        // Drain the UpdateQueue — delivers all observer callbacks.
        // Each callback either schedules a new lv_async_call (the first one
        // in the tick) or coalesces (the rest).
        q.drain();

        // No rebuild has run yet — it's queued via lv_async_call.
        REQUIRE(rebuild_count == 0);

        // Run LVGL's async list. Should fire exactly one rebuild.
        process_async_calls();
        REQUIRE(rebuild_count == 1);
    }

    SECTION("late-arriving gate after rebuild starts queues another rebuild") {
        // First burst → 1 rebuild
        if (auto* ks = lv_xml_get_subject(nullptr, "klippy_state"))
            lv_subject_set_int(ks, 1);
        for (auto& [name, subj] : all_gate_subjs) {
            lv_subject_set_int(subj, 1);
        }
        q.drain();
        process_async_calls();
        REQUIRE(rebuild_count == 1);

        // Second burst (different values) after first rebuild completed →
        // pending flag was cleared, so this queues another rebuild.
        for (auto& [name, subj] : all_gate_subjs) {
            lv_subject_set_int(subj, 2);
        }
        q.drain();
        process_async_calls();
        REQUIRE(rebuild_count == 2);
    }

    // Cleanup observers so the test fixture's reset_all() doesn't see stale
    // state. setup_gate_observers stores the new vector under panel_id; the
    // destructor of ObserverGuard will call lv_observer_remove which is safe
    // because the subjects are still alive at this point.
    mgr.clear_gate_observers("test_panel");
}
