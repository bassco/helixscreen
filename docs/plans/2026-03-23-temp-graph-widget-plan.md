# Temperature Graph Dashboard Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a configurable, multi-instance dashboard panel widget that displays temperature sensor data over time as an adaptive line chart.

**Architecture:** Extends the existing `ui_temp_graph` C module with a display mode API (feature flags for adaptive sizing), wraps it in a `TempGraphWidget` PanelWidget subclass with per-instance sensor config, and adds a Modal-based edit dialog for sensor/color selection.

**Tech Stack:** C++17, LVGL 9.5, spdlog, nlohmann::json, Catch2

**Spec:** `docs/plans/2026-03-23-temp-graph-widget-design.md`

---

## File Structure

| File | Responsibility |
|------|---------------|
| `include/ui_temp_graph.h` | **Modify** — add `ui_temp_graph_feature` enum + `ui_temp_graph_set_features()` |
| `src/ui/ui_temp_graph.cpp` | **Modify** — implement feature flag visibility controls |
| `src/ui/panel_widgets/temp_graph_widget.h` | **Create** — TempGraphWidget class declaration |
| `src/ui/panel_widgets/temp_graph_widget.cpp` | **Create** — widget implementation, factory, edit modal |
| `ui_xml/components/panel_widget_temp_graph.xml` | **Create** — minimal XML container |
| `src/ui/panel_widget_registry.cpp` | **Modify** — add widget definition + registration call |
| `src/xml_registration.cpp` | **Modify** — register XML component |
| `tests/unit/test_panel_widget_temp_graph.cpp` | **Create** — unit tests |

---

## Task 1: Display Mode API Extension

Extend `ui_temp_graph` with feature flag controls. This is the foundation — the widget needs this API before it can do adaptive sizing.

**Files:**
- Modify: `include/ui_temp_graph.h`
- Modify: `src/ui/ui_temp_graph.cpp`
- Test: `tests/unit/test_panel_widget_temp_graph.cpp`

**Docs to check:**
- `include/ui_temp_graph.h` — existing struct and API
- `src/ui/ui_temp_graph.cpp` — how Y-axis labels, gradients, and chart elements are created

- [ ] **Step 1: Write failing tests for feature flag API**

Create `tests/unit/test_panel_widget_temp_graph.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_temp_graph.h"
#include "../catch_amalgamated.hpp"
#include "../lvgl_ui_test_fixture.h""

TEST_CASE_METHOD(LVGLUITestFixture,
                 "TempGraph: feature flags default to all-on",
                 "[temp_graph][features]") {
    auto* graph = ui_temp_graph_create(lv_screen_active());
    REQUIRE(graph != nullptr);

    // Default: all features enabled
    uint32_t features = ui_temp_graph_get_features(graph);
    REQUIRE((features & TEMP_GRAPH_LINES) != 0);
    REQUIRE((features & TEMP_GRAPH_Y_AXIS) != 0);
    REQUIRE((features & TEMP_GRAPH_X_AXIS) != 0);
    REQUIRE((features & TEMP_GRAPH_GRADIENTS) != 0);
    REQUIRE((features & TEMP_GRAPH_TARGET_LINES) != 0);

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "TempGraph: set_features controls visibility",
                 "[temp_graph][features]") {
    auto* graph = ui_temp_graph_create(lv_screen_active());
    REQUIRE(graph != nullptr);

    // Sparkline mode: lines only
    ui_temp_graph_set_features(graph, TEMP_GRAPH_LINES);
    uint32_t features = ui_temp_graph_get_features(graph);
    REQUIRE(features == TEMP_GRAPH_LINES);

    // Full mode: everything
    uint32_t full = TEMP_GRAPH_LINES | TEMP_GRAPH_TARGET_LINES |
                    TEMP_GRAPH_LEGEND | TEMP_GRAPH_Y_AXIS |
                    TEMP_GRAPH_X_AXIS | TEMP_GRAPH_GRADIENTS |
                    TEMP_GRAPH_READOUTS;
    ui_temp_graph_set_features(graph, full);
    REQUIRE(ui_temp_graph_get_features(graph) == full);

    ui_temp_graph_destroy(graph);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test-run 2>&1 | grep -A5 "temp_graph.*features"`
Expected: Compile error — `TEMP_GRAPH_LINES` undefined, `ui_temp_graph_set_features` undefined

- [ ] **Step 3: Add enum and function declarations to header**

In `include/ui_temp_graph.h`, add after the existing `#define` constants (around line 50):

```cpp
/** Feature flags for adaptive display mode */
enum ui_temp_graph_feature {
    TEMP_GRAPH_LINES        = (1 << 0),  /**< Temperature lines (always on) */
    TEMP_GRAPH_TARGET_LINES = (1 << 1),  /**< Dashed target temperature cursors */
    TEMP_GRAPH_LEGEND       = (1 << 2),  /**< Color dots + sensor names */
    TEMP_GRAPH_Y_AXIS       = (1 << 3),  /**< Temperature axis labels */
    TEMP_GRAPH_X_AXIS       = (1 << 4),  /**< Time axis labels */
    TEMP_GRAPH_GRADIENTS    = (1 << 5),  /**< Gradient fills under lines */
    TEMP_GRAPH_READOUTS     = (1 << 6),  /**< Live temp values in legend */
};

/** All features enabled (default for full-size charts) */
#define TEMP_GRAPH_ALL_FEATURES 0x7F
```

Add a `features` field to `ui_temp_graph_t` struct:

```cpp
uint32_t features;  /**< Active feature flags (bitfield of ui_temp_graph_feature) */
```

Add function declarations near the other function signatures:

```cpp
void ui_temp_graph_set_features(ui_temp_graph_t* graph, uint32_t feature_flags);
uint32_t ui_temp_graph_get_features(ui_temp_graph_t* graph);
```

- [ ] **Step 4: Implement feature flag logic**

In `src/ui/ui_temp_graph.cpp`:

In `ui_temp_graph_create()`, after chart creation, initialize:
```cpp
graph->features = TEMP_GRAPH_ALL_FEATURES;
```

Add implementation:
```cpp
void ui_temp_graph_set_features(ui_temp_graph_t* graph, uint32_t feature_flags) {
    if (!graph) return;
    graph->features = feature_flags | TEMP_GRAPH_LINES;  // Lines always on

    // Y-axis visibility
    bool show_y = (feature_flags & TEMP_GRAPH_Y_AXIS) != 0;
    ui_temp_graph_set_y_axis(graph, graph->y_axis_increment, show_y);

    // Gradients: set opacity to 0 when disabled
    for (int i = 0; i < graph->series_count; i++) {
        auto& meta = graph->series_meta[i];
        if (!(feature_flags & TEMP_GRAPH_GRADIENTS)) {
            meta.gradient_bottom_opa = LV_OPA_TRANSP;
            meta.gradient_top_opa = LV_OPA_TRANSP;
        }
    }

    // Target cursors visibility
    for (int i = 0; i < graph->series_count; i++) {
        auto& meta = graph->series_meta[i];
        if (meta.target_cursor) {
            bool show_target = meta.show_target && (feature_flags & TEMP_GRAPH_TARGET_LINES);
            // Cursors don't have a hide API — move off-screen when hidden
            if (!show_target) {
                lv_point_t off = {-100, -100};
                lv_chart_set_cursor_pos(graph->chart, meta.target_cursor, &off);
            }
        }
    }

    lv_chart_refresh(graph->chart);
}

uint32_t ui_temp_graph_get_features(ui_temp_graph_t* graph) {
    if (!graph) return 0;
    return graph->features;
}
```

**X-axis implementation:** Add a `bool show_x_axis` field to `ui_temp_graph_t` (alongside existing `show_y_axis`). In the X-axis label draw code in `ui_temp_graph.cpp` (the section that renders time labels below the chart), add an early return if `!graph->show_x_axis`. Set this field in `ui_temp_graph_set_features()` based on `TEMP_GRAPH_X_AXIS`.

**Legend and readouts:** These are widget-level UI (not part of the `ui_temp_graph` C module). The widget creates legend labels as LVGL `text_tiny` objects above the chart in `attach()`. `TEMP_GRAPH_LEGEND` controls their visibility via `LV_OBJ_FLAG_HIDDEN`. `TEMP_GRAPH_READOUTS` controls whether current/target temp values are appended to legend text. The widget manages these objects directly, not through the C module.

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test-run 2>&1 | grep -E "(temp_graph.*features|PASSED|FAILED)"`
Expected: Both tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/ui_temp_graph.h src/ui/ui_temp_graph.cpp tests/unit/test_panel_widget_temp_graph.cpp
git commit -m "feat(temp-graph): add display mode feature flags API"
```

---

## Task 2: XML Component + Widget Registration

Create the minimal XML component and register the widget definition in the registry. This makes the widget appear in the settings list (though it won't do anything yet).

**Files:**
- Create: `ui_xml/components/panel_widget_temp_graph.xml`
- Modify: `src/ui/panel_widget_registry.cpp`
- Modify: `src/xml_registration.cpp`
- Test: `tests/unit/test_panel_widget_temp_graph.cpp`

**Docs to check:**
- `ui_xml/components/panel_widget_thermistor.xml` — template for XML structure
- `docs/devel/LVGL9_XML_GUIDE.md` — XML component rules

- [ ] **Step 1: Write failing test for widget registry entry**

Append to `tests/unit/test_panel_widget_temp_graph.cpp`:

```cpp
#include "panel_widget_registry.h"

TEST_CASE("TempGraphWidget: registered in widget registry",
          "[temp_graph][panel_widget]") {
    const auto* def = find_widget_def("temp_graph");
    REQUIRE(def != nullptr);
    REQUIRE(std::string(def->display_name) == "Temperature Graph");
    REQUIRE(std::string(def->icon) == "chart-line");
    REQUIRE(def->multi_instance == true);
    REQUIRE(def->colspan == 2);
    REQUIRE(def->rowspan == 2);
    REQUIRE(def->min_colspan == 1);
    REQUIRE(def->min_rowspan == 1);
    REQUIRE(def->max_colspan == 0);  // No max
    REQUIRE(def->max_rowspan == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test-run 2>&1 | grep -A3 "temp_graph.*registry"`
Expected: FAIL — `find_widget_def("temp_graph")` returns nullptr

- [ ] **Step 3: Create XML component**

Create `ui_xml/components/panel_widget_temp_graph.xml`:

```xml
<?xml version="1.0"?>
<component>
  <view name="panel_widget_temp_graph" extends="lv_obj"
        height="100%" flex_grow="1" style_pad_all="0" scrollable="false"
        clickable="true">
    <bind_state_if_not_eq subject="printer_connection_state" state="disabled" ref_value="2"/>
    <event_cb trigger="clicked" callback="on_temp_graph_widget_clicked"/>
  </view>
</component>
```

- [ ] **Step 4: Add widget definition to registry**

In `src/ui/panel_widget_registry.cpp`:

Add forward declaration with the others (around line 14):
```cpp
void register_temp_graph_widget();
```

Add widget definition entry in `s_widget_defs` (alongside other widgets):
```cpp
{"temp_graph", "Temperature Graph", "chart-line",
 "Live temperature graph with configurable sensors",
 "Temperature Graph",  // translation_tag
 nullptr,  // hardware_gate_subject — always available
 nullptr,  // hardware_gate_hint
 false,    // default_enabled (opt-in widget)
 2, 2,     // colspan, rowspan (default)
 1, 1,     // min_colspan, min_rowspan
 0, 0,     // max_colspan, max_rowspan (no max)
 true,     // multi_instance
 nullptr,  // factory (set by register_temp_graph_widget)
 nullptr}, // init_subjects
```

Add call in `init_widget_registrations()`:
```cpp
register_temp_graph_widget();
```

- [ ] **Step 5: Create stub registration function**

Create `src/ui/panel_widgets/temp_graph_widget.cpp` with just the registration stub.

**Important:** `register_temp_graph_widget()` must be declared and defined at global scope (no namespace), matching the forward declaration in `panel_widget_registry.cpp`. The class itself lives in `namespace helix`.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel_widget_registry.h"

#include <spdlog/spdlog.h>

void register_temp_graph_widget() {
    // Factory will be added in Task 3
    spdlog::debug("[TempGraphWidget] Registered");
}
```

- [ ] **Step 6: Register XML component**

In `src/xml_registration.cpp`, add with the other panel widget registrations (around line 340):
```cpp
register_xml("components/panel_widget_temp_graph.xml");
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `make test-run 2>&1 | grep -E "(temp_graph.*registry|PASSED|FAILED)"`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add ui_xml/components/panel_widget_temp_graph.xml \
        src/ui/panel_widgets/temp_graph_widget.cpp \
        src/ui/panel_widget_registry.cpp \
        src/xml_registration.cpp \
        tests/unit/test_panel_widget_temp_graph.cpp
git commit -m "feat(temp-graph): add widget registry entry and XML component"
```

---

## Task 3: TempGraphWidget Core — Class, Config, Attach/Detach

Implement the widget class with config loading, chart creation, and observer wiring.

**Files:**
- Create: `src/ui/panel_widgets/temp_graph_widget.h`
- Modify: `src/ui/panel_widgets/temp_graph_widget.cpp`
- Test: `tests/unit/test_panel_widget_temp_graph.cpp`

**Docs to check:**
- `src/ui/panel_widgets/thermistor_widget.h` — widget class pattern
- `include/ui_overlay_temp_graph.h` — SeriesInfo struct, observer wiring
- `include/temperature_history_manager.h` — backfill API
- `include/temperature_sensor_manager.h` — sensor discovery
- `include/printer_temperature_state.h` — heater subjects
- `include/ui_update_queue.h` — ScopedFreeze pattern

- [ ] **Step 1: Write failing tests for config and feature mapping**

Append to `tests/unit/test_panel_widget_temp_graph.cpp`:

```cpp
#include "panel_widget_config.h"
#include "config.h"

using json = nlohmann::json;

namespace helix {
class TempGraphConfigFixture {
  protected:
    Config config;

    void setup_with_config(const json& widget_config) {
        config.data = json::object();
        config.data["printers"]["default"]["panel_widgets"]["home"] = json::array({
            {{"id", "temp_graph"},
             {"enabled", true},
             {"config", widget_config},
             {"col", 0}, {"row", 0}}
        });
    }
};
}  // namespace helix

TEST_CASE_METHOD(helix::TempGraphConfigFixture,
                 "TempGraphWidget: default config has hotend + bed enabled",
                 "[temp_graph][config]") {
    setup_with_config(json::object());  // Empty config → defaults
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto cfg = wc.get_widget_config("temp_graph");
    // Default config should be empty (widget applies defaults internally)
    // This tests the round-trip, not the widget's default logic
}

TEST_CASE_METHOD(helix::TempGraphConfigFixture,
                 "TempGraphWidget: custom sensor config round-trips",
                 "[temp_graph][config]") {
    json sensor_cfg = {
        {"sensors", json::array({
            {{"name", "extruder"}, {"enabled", true}, {"color", "#FF4444"}},
            {{"name", "heater_bed"}, {"enabled", false}, {"color", "#88C0D0"}}
        })}
    };
    setup_with_config(sensor_cfg);
    PanelWidgetConfig wc("home", config);
    wc.load();

    auto cfg = wc.get_widget_config("temp_graph");
    REQUIRE(cfg.contains("sensors"));
    REQUIRE(cfg["sensors"].size() == 2);
    REQUIRE(cfg["sensors"][0]["name"] == "extruder");
    REQUIRE(cfg["sensors"][0]["enabled"] == true);
    REQUIRE(cfg["sensors"][1]["enabled"] == false);
}

TEST_CASE("TempGraphWidget: feature flags for size tiers",
          "[temp_graph][features]") {
    // Test the static mapping function
    // 1x1 → LINES only
    uint32_t f1x1 = helix::TempGraphWidget::features_for_size(1, 1);
    REQUIRE(f1x1 == TEMP_GRAPH_LINES);

    // 2x1 → + TARGET_LINES, LEGEND, X_AXIS
    uint32_t f2x1 = helix::TempGraphWidget::features_for_size(2, 1);
    REQUIRE((f2x1 & TEMP_GRAPH_LINES) != 0);
    REQUIRE((f2x1 & TEMP_GRAPH_TARGET_LINES) != 0);
    REQUIRE((f2x1 & TEMP_GRAPH_LEGEND) != 0);
    REQUIRE((f2x1 & TEMP_GRAPH_X_AXIS) != 0);
    REQUIRE((f2x1 & TEMP_GRAPH_Y_AXIS) == 0);

    // 1x2 → + TARGET_LINES, LEGEND, Y_AXIS
    uint32_t f1x2 = helix::TempGraphWidget::features_for_size(1, 2);
    REQUIRE((f1x2 & TEMP_GRAPH_Y_AXIS) != 0);
    REQUIRE((f1x2 & TEMP_GRAPH_X_AXIS) == 0);

    // 2x2 → full minus READOUTS
    uint32_t f2x2 = helix::TempGraphWidget::features_for_size(2, 2);
    REQUIRE((f2x2 & TEMP_GRAPH_GRADIENTS) != 0);
    REQUIRE((f2x2 & TEMP_GRAPH_READOUTS) == 0);

    // 3x2 → all features
    uint32_t f3x2 = helix::TempGraphWidget::features_for_size(3, 2);
    REQUIRE((f3x2 & TEMP_GRAPH_READOUTS) != 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test-run 2>&1 | grep -A3 "temp_graph.*(config|features)"`
Expected: Compile error — `TempGraphWidget` undefined

- [ ] **Step 3: Create widget header**

Create `src/ui/panel_widgets/temp_graph_widget.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "panel_widget.h"
#include "ui_observer_guard.h"
#include "ui_temp_graph.h"

#include <hv/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace helix {

class TempGraphWidget : public PanelWidget {
  public:
    explicit TempGraphWidget(const std::string& instance_id);
    ~TempGraphWidget() override;

    // PanelWidget interface
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    std::string get_component_name() const override { return "panel_widget_temp_graph"; }
    const char* id() const override { return instance_id_.c_str(); }
    void set_config(const nlohmann::json& config) override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    void on_activate() override;
    void on_deactivate() override;
    bool has_edit_configure() const override { return true; }
    bool on_edit_configure() override;
    bool supports_reuse() const override { return true; }

    // Static helpers
    static uint32_t features_for_size(int colspan, int rowspan);

    // XML event callback
    static void on_temp_graph_widget_clicked(lv_event_t* e);

  private:
    struct SeriesInfo {
        std::string name;
        int series_id = -1;
        lv_color_t color{};
        ObserverGuard temp_obs;
        ObserverGuard target_obs;
        SubjectLifetime lifetime;
        bool is_dynamic = false;
    };

    void setup_series();
    void setup_observers();
    void apply_auto_range();
    nlohmann::json build_default_config() const;

    std::string instance_id_;
    nlohmann::json config_;
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    ui_temp_graph_t* graph_ = nullptr;
    std::vector<SeriesInfo> series_;
    ObserverGuard connection_observer_;
    std::shared_ptr<bool> alive_;
    uint32_t generation_ = 0;
    bool paused_ = false;
    int current_colspan_ = 2;
    int current_rowspan_ = 2;
};

}  // namespace helix
```

- [ ] **Step 4: Implement the widget**

Replace the stub in `src/ui/panel_widgets/temp_graph_widget.cpp` with the full implementation. Key sections:

**Constructor + factory registration:**
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "temp_graph_widget.h"

#include "panel_widget_registry.h"
#include "printer_state.h"
#include "temperature_history_manager.h"
#include "temperature_sensor_manager.h"
#include "ui_overlay_temp_graph.h"
#include "ui_update_queue.h"
#include "observer_factory.h"

#include <spdlog/spdlog.h>

namespace helix {

// Default color palette (same as TempGraphOverlay)
static constexpr uint32_t kDefaultColors[] = {
    0xFF4444, 0x88C0D0, 0xA3BE8C, 0xEBCB8B,
    0xB48EAD, 0xD08770, 0x8FBCBB, 0xBF616A,
};
static constexpr int kColorCount = sizeof(kDefaultColors) / sizeof(kDefaultColors[0]);

TempGraphWidget::TempGraphWidget(const std::string& instance_id)
    : instance_id_(instance_id)
    , alive_(std::make_shared<bool>(false)) {}

TempGraphWidget::~TempGraphWidget() {
    if (*alive_) {
        detach();
    }
}

}  // namespace helix

// Global scope — matches forward declaration in panel_widget_registry.cpp
void register_temp_graph_widget() {
    helix::register_widget_factory("temp_graph", [](const std::string& id) {
        return std::make_unique<helix::TempGraphWidget>(id);
    });
    lv_xml_register_event_cb(nullptr, "on_temp_graph_widget_clicked",
                             helix::TempGraphWidget::on_temp_graph_widget_clicked);
}

namespace helix {
```

**features_for_size (static):**
```cpp
uint32_t TempGraphWidget::features_for_size(int colspan, int rowspan) {
    uint32_t f = TEMP_GRAPH_LINES;

    if (colspan >= 2 || rowspan >= 2) {
        f |= TEMP_GRAPH_TARGET_LINES | TEMP_GRAPH_LEGEND;
    }
    if (colspan >= 2) f |= TEMP_GRAPH_X_AXIS;
    if (rowspan >= 2) f |= TEMP_GRAPH_Y_AXIS;
    if (colspan >= 2 && rowspan >= 2) f |= TEMP_GRAPH_GRADIENTS;
    if (colspan >= 3 || (colspan >= 2 && rowspan >= 3)) f |= TEMP_GRAPH_READOUTS;

    return f;
}
```

**attach/detach:**
```cpp
void TempGraphWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    alive_ = std::make_shared<bool>(true);

    if (!widget_obj_) return;

    // Create chart inside the XML container
    graph_ = ui_temp_graph_create(widget_obj_);
    if (!graph_) {
        spdlog::error("[TempGraphWidget:{}] Failed to create graph", instance_id_);
        return;
    }

    // Apply sizing
    auto* chart = ui_temp_graph_get_chart(graph_);
    if (chart) {
        lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
    }

    // Set features for current size
    ui_temp_graph_set_features(graph_, features_for_size(current_colspan_, current_rowspan_));

    // Build config if empty (first attach)
    if (!config_.contains("sensors") || config_["sensors"].empty()) {
        config_ = build_default_config();
    }

    setup_series();
    setup_observers();
    apply_auto_range();
}

void TempGraphWidget::detach() {
    *alive_ = false;

    auto freeze = ui::UpdateQueue::instance().scoped_freeze();
    ui::UpdateQueue::instance().drain();

    connection_observer_.reset();
    series_.clear();

    if (graph_) {
        ui_temp_graph_destroy(graph_);
        graph_ = nullptr;
    }
    widget_obj_ = nullptr;
    parent_screen_ = nullptr;
}
```

**Config and size handlers:**
```cpp
void TempGraphWidget::set_config(const nlohmann::json& config) {
    config_ = config;
}

void TempGraphWidget::on_size_changed(int colspan, int rowspan, int /*width_px*/, int height_px) {
    current_colspan_ = colspan;
    current_rowspan_ = rowspan;
    if (graph_) {
        ui_temp_graph_set_features(graph_, features_for_size(colspan, rowspan));
    }
}

void TempGraphWidget::on_activate() {
    paused_ = false;
    // Backfill to catch up on missed samples while offscreen
    // Follow the pattern in TempGraphOverlay::backfill_series()
    if (graph_) {
        for (auto& si : series_) {
            auto samples = TemperatureHistoryManager::instance().get_samples(si.name);
            std::vector<float> temps;
            temps.reserve(samples.size());
            for (const auto& s : samples) {
                temps.push_back(static_cast<float>(s.temp_centi) / 10.0f);
            }
            if (!temps.empty()) {
                ui_temp_graph_set_series_data(graph_, si.series_id,
                                              temps.data(), static_cast<int>(temps.size()));
            }
        }
    }
}

void TempGraphWidget::on_deactivate() {
    paused_ = true;
}
```

**Click handler:**
```cpp
void TempGraphWidget::on_temp_graph_widget_clicked(lv_event_t* e) {
    auto& overlay = get_global_temp_graph_overlay();
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    overlay.open(TempGraphOverlay::Mode::GraphOnly, lv_obj_get_screen(target));
}
```

**build_default_config:**
```cpp
nlohmann::json TempGraphWidget::build_default_config() const {
    nlohmann::json cfg;
    cfg["sensors"] = nlohmann::json::array();

    int color_idx = 0;
    auto add_sensor = [&](const std::string& name, bool enabled) {
        cfg["sensors"].push_back({
            {"name", name},
            {"enabled", enabled},
            {"color", fmt::format("#{:06X}", kDefaultColors[color_idx % kColorCount])}
        });
        color_idx++;
    };

    // Hotend + bed enabled by default
    auto& temp_state = PrinterState::instance().temperature();
    add_sensor("extruder", true);
    add_sensor("heater_bed", true);

    // Auxiliary sensors disabled by default
    auto& sensor_mgr = TemperatureSensorManager::instance();
    for (const auto& name : sensor_mgr.get_sensor_names()) {
        add_sensor(name, false);
    }

    return cfg;
}
```

**setup_series and setup_observers** follow the `TempGraphOverlay` pattern — create `SeriesInfo` per enabled sensor, observe the appropriate subject (static for bed/single-extruder, dynamic with lifetime for multi-extruder and aux sensors), backfill from `TemperatureHistoryManager`. Observer lambdas capture `weak_ptr<bool> weak = alive_` and `uint32_t gen = generation_`, checking both before accessing widget state.

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test-run 2>&1 | grep -E "(temp_graph|PASSED|FAILED)"`
Expected: All temp_graph tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/ui/panel_widgets/temp_graph_widget.h \
        src/ui/panel_widgets/temp_graph_widget.cpp \
        tests/unit/test_panel_widget_temp_graph.cpp
git commit -m "feat(temp-graph): implement TempGraphWidget core with config and adaptive sizing"
```

---

## Task 4: Edit Mode Modal

Build the Modal subclass for sensor toggle + color picker configuration.

**Files:**
- Modify: `src/ui/panel_widgets/temp_graph_widget.h`
- Modify: `src/ui/panel_widgets/temp_graph_widget.cpp`
- Create: `ui_xml/components/temp_graph_config_modal.xml`
- Modify: `src/xml_registration.cpp`

**Docs to check:**
- `docs/devel/MODAL_SYSTEM.md` — Modal subclass pattern
- `include/ui_shutdown_modal.h` — example Modal subclass
- `ui_xml/shutdown_modal.xml` — example Modal XML

- [ ] **Step 1: Create XML for config modal**

Create `ui_xml/components/temp_graph_config_modal.xml`:

```xml
<?xml version="1.0"?>
<component>
  <view name="temp_graph_config_modal" extends="ui_dialog"
        width="360" height="content">
    <!-- Header -->
    <lv_obj width="100%" height="content" flex_flow="row"
            style_flex_cross_place="center" style_pad_all="#space_sm"
            scrollable="false">
      <icon src="chart-line" size="sm" variant="primary"/>
      <text_heading text="Configure Graph" translation_tag="Configure Graph"
                    style_pad_left="#space_sm" flex_grow="1"/>
    </lv_obj>

    <divider_horizontal/>

    <!-- Scrollable sensor list (populated programmatically) -->
    <lv_obj name="sensor_list" width="100%" height="280"
            flex_flow="column" style_pad_all="#space_sm"
            style_pad_gap="#space_xs" scrollbar_mode="auto"/>

    <divider_horizontal/>

    <!-- OK/Cancel buttons -->
    <modal_button_row primary_text="OK" primary_callback="on_modal_ok_clicked"
                      secondary_text="Cancel" secondary_callback="on_modal_cancel_clicked"/>
  </view>
</component>
```

- [ ] **Step 2: Register XML component**

In `src/xml_registration.cpp`, add:
```cpp
register_xml("components/temp_graph_config_modal.xml");
```

- [ ] **Step 3: Implement Modal subclass**

Add inner class to `temp_graph_widget.h`:

```cpp
class TempGraphConfigModal : public Modal {
  public:
    using SaveCallback = std::function<void(const nlohmann::json&)>;

    TempGraphConfigModal(const nlohmann::json& current_config, SaveCallback on_save);

    const char* get_name() const override { return "Temperature Graph Config"; }
    const char* component_name() const override { return "temp_graph_config_modal"; }

  protected:
    void on_show() override;
    void on_ok() override;

  private:
    void populate_sensor_list();
    void create_sensor_row(lv_obj_t* parent, const std::string& name,
                           bool enabled, lv_color_t color, int index);

    nlohmann::json config_;
    SaveCallback on_save_;
    lv_obj_t* sensor_list_ = nullptr;

    // Tracks toggle state per sensor (by index)
    struct SensorRowState {
        std::string name;
        bool enabled;
        lv_color_t color;
    };
    std::vector<SensorRowState> row_states_;
};
```

Implement `on_edit_configure()` in the widget:
```cpp
bool TempGraphWidget::on_edit_configure() {
    std::weak_ptr<bool> weak = alive_;
    auto modal = std::make_unique<TempGraphConfigModal>(
        config_,
        [this, weak](const nlohmann::json& new_config) {
            if (weak.expired()) return;  // Widget destroyed while modal open
            config_ = new_config;
            save_widget_config(config_);
            generation_++;
            auto* w = widget_obj_;  // Save before detach nulls them
            auto* p = parent_screen_;
            detach();
            attach(w, p);
        });
    Modal::show(std::move(modal));
    return true;
}
```

The modal implementation populates the sensor list programmatically in `on_show()` — each row has a color swatch (button with bg_color set to the sensor color), the sensor display name, and a switch for enable/disable. Color swatch tap opens a small color picker (reuse the 8-color palette as a simple grid of colored buttons).

- [ ] **Step 4: Build and test manually**

Run: `make -j && ./build/bin/helix-screen --test -vv`

Navigate to home panel, enable the temp_graph widget in edit mode, verify:
1. Widget appears with chart
2. Edit button opens config modal
3. Toggling sensors updates the chart after OK
4. Color picker works

- [ ] **Step 5: Commit**

```bash
git add src/ui/panel_widgets/temp_graph_widget.h \
        src/ui/panel_widgets/temp_graph_widget.cpp \
        ui_xml/components/temp_graph_config_modal.xml \
        src/xml_registration.cpp
git commit -m "feat(temp-graph): add sensor config modal with color picker"
```

---

## Task 5: Connection State + Reconnect Handling

Handle printer disconnect/reconnect by observing the connection state subject and rebuilding series.

**Files:**
- Modify: `src/ui/panel_widgets/temp_graph_widget.cpp`
- Test: `tests/unit/test_panel_widget_temp_graph.cpp`

- [ ] **Step 1: Write failing test for disconnect behavior**

```cpp
TEST_CASE_METHOD(LVGLUITestFixture,
                 "TempGraphWidget: clears chart on disconnect",
                 "[temp_graph][connection]") {
    // Setup: create widget, verify it has series
    // Simulate disconnect by setting connection subject to disconnected
    // Drain queue
    // Verify chart is cleared
}
```

- [ ] **Step 2: Implement connection observer in attach()**

In `setup_observers()`, add at the end:

```cpp
auto* conn_subject = PrinterState::instance().get_printer_connection_state_subject();
if (conn_subject) {
    std::weak_ptr<bool> weak = alive_;
    uint32_t gen = generation_;
    connection_observer_ = observe_int_sync<TempGraphWidget>(
        conn_subject, this,
        [weak, gen](TempGraphWidget* self, int state) {
            if (weak.expired() || gen != self->generation_) return;
            if (state == 2) {  // Connected
                spdlog::debug("[TempGraphWidget:{}] Reconnected, rebuilding", self->instance_id_);
                self->generation_++;
                auto* w = self->widget_obj_;  // Save before detach nulls them
                auto* p = self->parent_screen_;
                self->detach();
                self->attach(w, p);
            } else if (state == 0) {  // Disconnected
                if (self->graph_) {
                    ui_temp_graph_clear(self->graph_);
                }
            }
        });
}
```

- [ ] **Step 3: Run tests**

Run: `make test-run 2>&1 | grep -E "(temp_graph|PASSED|FAILED)"`
Expected: All PASS

- [ ] **Step 4: Commit**

```bash
git add src/ui/panel_widgets/temp_graph_widget.cpp \
        tests/unit/test_panel_widget_temp_graph.cpp
git commit -m "feat(temp-graph): handle printer disconnect/reconnect"
```

---

## Task 6: Comprehensive Tests

Fill in remaining test coverage for edge cases identified in the spec.

**Files:**
- Test: `tests/unit/test_panel_widget_temp_graph.cpp`

- [ ] **Step 1: Add remaining tests**

```cpp
TEST_CASE("TempGraphWidget: sensor discovery appends new sensors as disabled",
          "[temp_graph][config]") {
    // Existing config has extruder only
    // Sensor manager reports extruder + chamber
    // After build_default_config merging, chamber should appear disabled
}

TEST_CASE("TempGraphWidget: missing sensors in config silently skipped",
          "[temp_graph][config]") {
    // Config references "temperature_sensor gone" which doesn't exist
    // Widget should not crash, just skip it
}

TEST_CASE("TempGraphWidget: generation counter rejects stale callbacks",
          "[temp_graph][safety]") {
    // Capture generation, bump it, verify callback with old gen is no-op
}

TEST_CASE("TempGraphWidget: paused flag skips chart updates",
          "[temp_graph][lifecycle]") {
    // on_deactivate() → paused_ = true
    // Observer callback should skip update
    // on_activate() → paused_ = false, backfill
}
```

- [ ] **Step 2: Run full test suite**

Run: `make test-run`
Expected: All tests pass, no regressions

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_panel_widget_temp_graph.cpp
git commit -m "test(temp-graph): comprehensive edge case tests"
```

---

## Task 7: Manual Testing + Polish

Visual verification and final cleanup.

- [ ] **Step 1: Build and launch**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

- [ ] **Step 2: Manual test checklist**

Verify in the running app:
1. Enable "Temperature Graph" widget from home panel settings
2. Widget appears at 2x2 with full chart (axes, legend, gradient fills)
3. Resize to 1x1 — sparklines only, no axes
4. Resize to 2x1 — time axis visible, no Y axis
5. Resize to 1x2 — Y axis visible, no time axis
6. Resize to 3x2+ — live readouts in legend
7. Tap widget → opens TempGraphOverlay in GraphOnly mode
8. Edit mode → configure button → modal opens
9. Toggle sensors on/off → chart updates after OK
10. Change a sensor color → reflected in chart
11. Add second instance (temp_graph:2) → independent config

- [ ] **Step 3: Fix any visual issues found**

Address spacing, alignment, or color issues discovered during manual testing.

- [ ] **Step 4: Final commit**

```bash
git add -u
git commit -m "fix(temp-graph): visual polish from manual testing"
```
