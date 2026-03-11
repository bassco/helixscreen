# HTLF Mixed Topology Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix HTLF filament routing display — add MIXED topology for units with both direct and hub-routed lanes, fix tool labels, fix badge leak, improve empty slot UX.

**Architecture:** Add `PathTopology::MIXED` enum value. Parse per-lane `hub` field from AFC data to classify topology. New `draw_mixed_topology()` renders direct lanes to individual nozzles and hub lanes converging through hub box to shared nozzle. Badge leak fixed by cleaning badge_layer before repopulating. Empty slots show dashed circle + plus icon.

**Tech Stack:** C++17, LVGL 9.5, custom XML widget system, spdlog, nlohmann/json

**Spec:** `docs/superpowers/specs/2026-03-10-htlf-mixed-topology-design.md`

---

## Chunk 1: Foundation (Enum, Badge Fix, Data Plumbing)

### Task 1: Add PathTopology::MIXED enum and badge leak fix

Two quick wins with no dependencies.

**Files:**
- Modify: `include/ams_types.h:373-395`
- Modify: `src/ui/ui_ams_detail.cpp:406-422`

- [ ] **Step 1: Add MIXED to PathTopology enum**

In `include/ams_types.h`, add `MIXED = 3` after `PARALLEL = 2`:

```cpp
enum class PathTopology {
    LINEAR = 0,  ///< Happy Hare: selector picks one input
    HUB = 1,     ///< AFC: merger combines inputs through hub
    PARALLEL = 2, ///< Tool Changer: each slot is a separate toolhead
    MIXED = 3    ///< Direct + Hub: some lanes direct, some through hub to shared extruder
};
```

Update `path_topology_to_string()` to handle the new case:

```cpp
case PathTopology::MIXED:
    return "Mixed (Direct + Hub)";
```

- [ ] **Step 2: Add lane_is_hub_routed to AmsUnit struct**

In `include/ams_types.h`, after `hub_tool_label` field (around line 682), add:

```cpp
/// Per-lane hub routing flag. true = lane routes through hub, false = direct to extruder.
/// Empty vector means routing info unavailable (treat as uniform topology).
std::vector<bool> lane_is_hub_routed;
```

- [ ] **Step 3: Fix badge leak — add lv_obj_clean to ams_detail_update_badges**

In `src/ui/ui_ams_detail.cpp`, function `ams_detail_update_badges()` at line 409, add cleanup after the null check:

```cpp
void ams_detail_update_badges(AmsDetailWidgets& w, lv_obj_t* slot_widgets[], int slot_count,
                              const AmsSlotLayout& layout) {
    if (!w.badge_layer)
        return;

    // Clean stale badges from previous unit view (badges are reparented here
    // from slot widgets, so they persist across unit switches if not cleaned)
    lv_obj_clean(w.badge_layer);

    int32_t slot_spacing = layout.slot_width - layout.overlap;
    // ... rest unchanged
```

- [ ] **Step 4: Build to verify no compile errors**

Run: `make -j`
Expected: Clean compile

- [ ] **Step 5: Commit**

```
git add include/ams_types.h src/ui/ui_ams_detail.cpp
git commit -m "feat(ams): add PathTopology::MIXED enum and fix badge layer leak (#364)"
```

---

### Task 2: Per-lane hub routing storage in AFC backend

Parse the `hub` field from AFC per-lane data and use it for topology classification.

**Files:**
- Modify: `include/ams_backend_afc.h:70-81`
- Modify: `src/printer/ams_backend_afc.cpp:1002-1136` (parse_afc_stepper)
- Modify: `src/printer/ams_backend_afc.cpp:1339-1399` (parse_afc_unit_object)
- Modify: `src/printer/ams_backend_afc.cpp:1440-1459` (reorganize_slots topology propagation)
- Test: `tests/unit/test_ams_backend_afc.cpp`

- [ ] **Step 1: Write failing test for MIXED topology classification**

In `tests/unit/test_ams_backend_afc.cpp`, add a test that feeds HTLF-like data and checks topology:

```cpp
TEST_CASE("AFC HTLF mixed topology classification", "[afc][topology]") {
    // Create backend and feed it unit data with mixed hub routing
    // Unit has 4 lanes: 2 direct + 2 via hub
    // Expected: get_unit_topology() returns PathTopology::MIXED

    AmsBackendAfc backend;
    // ... setup with HTLF-like AFC data
    // Verify topology is MIXED, not PARALLEL
    auto info = backend.get_system_info();
    REQUIRE(info.units.size() >= 1);
    // Find the HTLF unit
    for (const auto& unit : info.units) {
        if (unit.name == "HTLF HTLF_1") {
            REQUIRE(unit.topology == PathTopology::MIXED);
            REQUIRE(unit.lane_is_hub_routed.size() == 4);
            REQUIRE(unit.lane_is_hub_routed[0] == false); // lane1 direct
            REQUIRE(unit.lane_is_hub_routed[1] == false); // lane2 direct
            REQUIRE(unit.lane_is_hub_routed[2] == true);  // lane3 hub
            REQUIRE(unit.lane_is_hub_routed[3] == true);  // lane4 hub
        }
    }
}
```

Note: The test will need to simulate the AFC data flow. Look at existing tests in `test_ams_backend_afc.cpp` for the pattern — they typically create a backend instance and feed it JSON via `handle_status_update()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `make test && ./build/bin/helix-tests "[afc][topology]" -v`
Expected: FAIL (MIXED not yet classified)

- [ ] **Step 3: Add lane_is_hub_routed to AfcUnitInfo**

In `include/ams_backend_afc.h`, add to `AfcUnitInfo` struct (after `topology` at line 80):

```cpp
    PathTopology topology = PathTopology::HUB; ///< Derived topology for this unit

    /// Per-lane hub routing. Parallel to `lanes` vector.
    /// true = lane routes through hub, false = direct to extruder.
    std::vector<bool> lane_is_hub_routed;
```

- [ ] **Step 4: Add lane_hub_routing_ map to AmsBackendAfc**

In `include/ams_backend_afc.h`, add a private member to the class (alongside other maps like `hub_sensors_`):

```cpp
/// Per-lane hub routing: lane_name → hub name ("direct" for direct lanes)
std::unordered_map<std::string, std::string> lane_hub_routing_;
```

- [ ] **Step 5: Parse hub field in parse_afc_stepper**

In `src/printer/ams_backend_afc.cpp`, after the `map` field parsing (line 1136), add:

```cpp
    // Parse hub routing for this lane ("direct" or hub name like "HTLF_1")
    if (data.contains("hub") && data["hub"].is_string()) {
        std::string hub = data["hub"].get<std::string>();
        lane_hub_routing_[lane_name] = hub;
        spdlog::trace("[AMS AFC] Lane {} hub routing: {}", lane_name, hub);
    }
```

- [ ] **Step 6: Update topology classification in parse_afc_unit_object**

In `src/printer/ams_backend_afc.cpp`, replace the topology derivation block (lines 1379-1394) with:

```cpp
    // Derive topology from per-lane hub routing data.
    // The per-lane "hub" field ("direct" vs hub name) is the authoritative source.
    // Fallback to extruder/hub count heuristic if per-lane data unavailable.
    bool has_direct = false;
    bool has_hub_routed = false;
    unit_info.lane_is_hub_routed.clear();

    for (const auto& lane : unit_info.lanes) {
        auto it = lane_hub_routing_.find(lane);
        bool is_hub = false;
        if (it != lane_hub_routing_.end()) {
            is_hub = (it->second != "direct");
        }
        unit_info.lane_is_hub_routed.push_back(is_hub);
        if (is_hub)
            has_hub_routed = true;
        else
            has_direct = true;
    }

    if (has_direct && has_hub_routed) {
        unit_info.topology = PathTopology::MIXED;
    } else if (has_hub_routed) {
        unit_info.topology = PathTopology::HUB;
    } else if (has_direct && unit_info.extruders.size() > 1) {
        unit_info.topology = PathTopology::PARALLEL;
    } else if (!unit_info.hubs.empty() && unit_info.extruders.size() <= 1) {
        unit_info.topology = PathTopology::HUB;
    } else if (unit_info.extruders.size() > 1) {
        unit_info.topology = PathTopology::PARALLEL;
    } else {
        unit_info.topology = PathTopology::HUB; // default
    }
```

- [ ] **Step 7: Propagate lane_is_hub_routed to AmsUnit in reorganize_slots**

In `src/printer/ams_backend_afc.cpp`, inside the `reorganize_slots()` topology propagation loop (around line 1447), after `sys_unit.topology = ui.topology;`, add:

```cpp
                        sys_unit.lane_is_hub_routed = ui.lane_is_hub_routed;
```

- [ ] **Step 8: Run test to verify it passes**

Run: `make test && ./build/bin/helix-tests "[afc][topology]" -v`
Expected: PASS

- [ ] **Step 9: Commit**

```
git add include/ams_backend_afc.h src/printer/ams_backend_afc.cpp tests/unit/test_ams_backend_afc.cpp
git commit -m "feat(afc): parse per-lane hub routing and classify MIXED topology (#364)"
```

---

### Task 3: Per-slot metadata in FilamentPathData + tool label fix

Add mapped_tool and hub_routed arrays to the filament path canvas data, fix tool labels.

**Files:**
- Modify: `src/ui/ui_filament_path_canvas.cpp:133-155` (FilamentPathData struct)
- Modify: `src/ui/ui_filament_path_canvas.cpp:1529` (tool label fix)
- Modify: `include/ui_filament_path_canvas.h` (new setter declarations)
- Modify: `src/ui/ui_ams_detail.cpp:424-465` (plumb data in setup_path_canvas)

- [ ] **Step 1: Add per-slot fields to FilamentPathData**

In `src/ui/ui_filament_path_canvas.cpp`, after `slot_has_prep_sensor` (line 154), add:

```cpp
    // Per-slot tool mapping (actual AFC map values, not slot index)
    int mapped_tool[MAX_SLOTS];      // -1 = use slot index as fallback
    bool slot_is_hub_routed[MAX_SLOTS] = {}; // true = lane routes through hub (MIXED topology)
```

In the struct definition or constructor initialization, ensure `mapped_tool` is initialized to -1. Add initialization in the widget create function where other arrays are zeroed:

```cpp
    std::fill(std::begin(data->mapped_tool), std::end(data->mapped_tool), -1);
```

- [ ] **Step 2: Add setter functions**

In `include/ui_filament_path_canvas.h`, add declarations:

```cpp
void ui_filament_path_canvas_set_slot_mapped_tool(lv_obj_t* obj, int slot, int tool);
void ui_filament_path_canvas_set_slot_hub_routed(lv_obj_t* obj, int slot, bool is_hub);
```

In `src/ui/ui_filament_path_canvas.cpp`, add implementations (near the other setter functions around line 2572):

```cpp
void ui_filament_path_canvas_set_slot_mapped_tool(lv_obj_t* obj, int slot, int tool) {
    auto* data = get_data(obj);
    if (!data || slot < 0 || slot >= FilamentPathData::MAX_SLOTS)
        return;
    if (data->mapped_tool[slot] != tool) {
        data->mapped_tool[slot] = tool;
        lv_obj_invalidate(obj);
    }
}

void ui_filament_path_canvas_set_slot_hub_routed(lv_obj_t* obj, int slot, bool is_hub) {
    auto* data = get_data(obj);
    if (!data || slot < 0 || slot >= FilamentPathData::MAX_SLOTS)
        return;
    if (data->slot_is_hub_routed[slot] != is_hub) {
        data->slot_is_hub_routed[slot] = is_hub;
        lv_obj_invalidate(obj);
    }
}
```

- [ ] **Step 3: Fix tool label in draw_parallel_topology**

In `src/ui/ui_filament_path_canvas.cpp` line 1529, change:

```cpp
snprintf(tool_label, sizeof(tool_label), "T%d", i);
```

to:

```cpp
int tool = (data->mapped_tool[i] >= 0) ? data->mapped_tool[i] : i;
snprintf(tool_label, sizeof(tool_label), "T%d", tool);
```

- [ ] **Step 4: Plumb per-slot data in ams_detail_setup_path_canvas**

In `src/ui/ui_ams_detail.cpp`, in `ams_detail_setup_path_canvas()` after the topology and slot_count are set (around line 452), add per-slot data plumbing:

```cpp
    // Plumb per-slot metadata (mapped_tool, hub routing) to path canvas
    if (unit_index >= 0 && unit_index < static_cast<int>(info.units.size())) {
        const auto& unit = info.units[unit_index];
        for (int i = 0; i < slot_count; ++i) {
            int gi = slot_offset + i;
            SlotInfo slot = backend->get_slot_info(gi);
            ui_filament_path_canvas_set_slot_mapped_tool(canvas, i, slot.mapped_tool);
            if (i < static_cast<int>(unit.lane_is_hub_routed.size())) {
                ui_filament_path_canvas_set_slot_hub_routed(canvas, i, unit.lane_is_hub_routed[i]);
            }
        }
    }
```

- [ ] **Step 5: Build to verify**

Run: `make -j`
Expected: Clean compile

- [ ] **Step 6: Commit**

```
git add src/ui/ui_filament_path_canvas.cpp include/ui_filament_path_canvas.h src/ui/ui_ams_detail.cpp
git commit -m "feat(ams): add per-slot mapped_tool and hub routing to filament path canvas (#364)"
```

---

## Chunk 2: Mock Mode + Drawing

### Task 4: New HTLF+Toolchanger mock mode

Add mock mode that simulates the user's exact setup for visual testing.

**Files:**
- Modify: `include/ams_backend_mock.h:509-517` (add mode flag)
- Modify: `src/printer/ams_backend_mock.cpp` (add mode setup, near line 1750)
- Modify: `src/application/runtime_config.cpp` (wire up env var)
- Modify: `docs/devel/ENVIRONMENT_VARIABLES.md:488-518` (document new mode)

- [ ] **Step 1: Add mode flag and methods to mock header**

In `include/ams_backend_mock.h`, after `ifs_mode_` (line 516), add:

```cpp
    bool htlf_toolchanger_mode_ = false;  ///< Simulate HTLF + Toolchanger mixed topology
```

Add public method declarations (near the other `set_*_mode` methods):

```cpp
    void set_htlf_toolchanger_mode(bool enabled);
    [[nodiscard]] bool is_htlf_toolchanger_mode() const;
```

- [ ] **Step 2: Implement set_htlf_toolchanger_mode**

In `src/printer/ams_backend_mock.cpp`, after `set_vivid_mixed_mode` implementation (around line 1750), add:

```cpp
void AmsBackendMock::set_htlf_toolchanger_mode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    htlf_toolchanger_mode_ = enabled;

    if (enabled) {
        // Disable conflicting modes
        tool_changer_mode_ = false;
        multi_unit_mode_ = false;
        mixed_topology_mode_ = false;
        vivid_mixed_mode_ = false;

        // Configure as AFC system
        system_info_.type = AmsType::AFC;
        system_info_.type_name = "AFC (Mock HTLF+TC)";
        system_info_.version = "1.0.32-mock";
        system_info_.total_slots = 7;

        auto afc_caps = helix::printer::afc_default_capabilities();
        system_info_.supports_endless_spool = afc_caps.supports_endless_spool;
        system_info_.supports_tool_mapping = afc_caps.supports_tool_mapping;
        system_info_.supports_bypass = afc_caps.supports_bypass;
        system_info_.supports_purge = afc_caps.supports_purge;
        system_info_.tip_method = afc_caps.tip_method;
        system_info_.has_hardware_bypass_sensor = false;

        topology_ = PathTopology::PARALLEL;

        // Per-unit topologies
        unit_topologies_.clear();
        unit_topologies_.push_back(PathTopology::PARALLEL); // Tools unit
        unit_topologies_.push_back(PathTopology::MIXED);    // HTLF unit

        // Initialize registry with 2 units (3+4=7 slots)
        slots_.clear();
        slots_.initialize_units({
            {"HTLF HTLF_1", {"lane1", "lane2", "lane3", "lane4"}},
            {"Toolchanger Tools", {"extruder3", "extruder4", "extruder5"}},
        });

        // Helper to populate a slot
        auto populate_slot = [this](int gi, int si, const char* material, uint32_t color,
                                    const char* color_name, SlotStatus status, int tool,
                                    int spoolman_id, float remaining) {
            auto* entry = slots_.get_mut(gi);
            if (!entry)
                return;
            entry->info.slot_index = si;
            entry->info.global_index = gi;
            entry->info.material = material;
            entry->info.color_rgb = color;
            entry->info.color_name = color_name;
            entry->info.status = status;
            entry->info.mapped_tool = tool;
            entry->info.spoolman_id = spoolman_id;
            entry->info.total_weight_g = 1000.0f;
            entry->info.remaining_weight_g = remaining;
            auto mat_info = filament::find_material(material);
            if (mat_info) {
                entry->info.nozzle_temp_min = mat_info->nozzle_min;
                entry->info.nozzle_temp_max = mat_info->nozzle_max;
                entry->info.bed_temp = mat_info->bed_temp;
            }
        };

        // Unit 0: HTLF_1 — 4 lanes, MIXED topology (alphabetical: HTLF before Toolchanger)
        // lane1→T0 direct, lane2→T2 direct, lane3→T1 hub, lane4→T3 hub
        populate_slot(0, 0, "ABS", 0xFCFBFB, "White", SlotStatus::LOADED, 0, 39, 493.0f);
        populate_slot(1, 1, "ABS", 0x0D2441, "Navy", SlotStatus::LOADED, 2, 4, 430.0f);
        populate_slot(2, 2, "ASA Sparkle", 0x0F274E, "Navy Sparkle", SlotStatus::AVAILABLE, 1, 28, 581.0f);
        populate_slot(3, 3, "", 0x000000, "", SlotStatus::EMPTY, 3, -1, 0.0f);

        // Unit 1: Toolchanger Tools — 3 lanes, PARALLEL
        // extruder3→T4, extruder4→T5, extruder5→T6
        populate_slot(4, 0, "PLA", 0xFFFFFF, "White", SlotStatus::LOADED, 4, 11, 30.0f);
        populate_slot(5, 1, "", 0x000000, "", SlotStatus::EMPTY, 5, -1, 0.0f);
        populate_slot(6, 2, "", 0x000000, "", SlotStatus::EMPTY, 6, -1, 0.0f);

        // Tool mapping: 7 slots → tools T0-T6 (non-sequential per AFC map)
        slots_.set_tool_map({0, 2, 1, 3, 4, 5, 6});

        // Unit-level metadata
        system_info_.units.clear();
        {
            AmsUnit u;
            u.unit_index = 0;
            u.name = "HTLF HTLF_1";
            u.display_name = "HTLF_1";
            u.slot_count = 4;
            u.first_slot_global_index = 0;
            u.connected = true;
            u.has_toolhead_sensor = true;
            u.has_slot_sensors = true;
            u.has_hub_sensor = true;
            u.hub_sensor_triggered = false;
            u.topology = PathTopology::MIXED;
            u.lane_is_hub_routed = {false, false, true, true};
            BufferHealth health;
            health.state = "Advancing";
            health.fault_detection_enabled = false;
            u.buffer_health = health;
            system_info_.units.push_back(u);
        }
        {
            AmsUnit u;
            u.unit_index = 1;
            u.name = "Toolchanger Tools";
            u.display_name = "Tools";
            u.slot_count = 3;
            u.first_slot_global_index = 4;
            u.connected = true;
            u.has_toolhead_sensor = true;
            u.has_slot_sensors = true;
            u.has_hub_sensor = false;
            u.topology = PathTopology::PARALLEL;
            system_info_.units.push_back(u);
        }

        // Start with HTLF lane1 loaded (slot 0, tool T0)
        system_info_.current_slot = 0;
        system_info_.current_tool = 0;
        system_info_.filament_loaded = true;
        filament_segment_ = PathSegment::NOZZLE;

        // AFC device sections and actions
        mock_device_sections_ = helix::printer::afc_default_sections();
        mock_device_actions_ = helix::printer::afc_default_actions();

        spdlog::info("[AmsBackendMock] HTLF+Toolchanger mode: HTLF_1 (4, MIXED) + Tools (3, PARALLEL) = 7 slots");
    } else {
        htlf_toolchanger_mode_ = false;
        unit_topologies_.clear();
        system_info_.type = AmsType::HAPPY_HARE;
        system_info_.type_name = "Happy Hare (Mock)";
        system_info_.version = "2.7.0-mock";
        topology_ = PathTopology::LINEAR;
        spdlog::info("[AmsBackendMock] HTLF+Toolchanger mode disabled");
    }
}

bool AmsBackendMock::is_htlf_toolchanger_mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return htlf_toolchanger_mode_;
}
```

- [ ] **Step 3: Wire up environment variable**

Find where `HELIX_MOCK_AMS` is parsed in `src/application/runtime_config.cpp` (look for `set_mixed_topology_mode` or `set_afc_mode` calls). Add a new case:

```cpp
} else if (mock_ams == "htlf_toolchanger" || mock_ams == "htlf_tc" || mock_ams == "htlf") {
    ams_mock->set_htlf_toolchanger_mode(true);
```

- [ ] **Step 4: Document in ENVIRONMENT_VARIABLES.md**

In `docs/devel/ENVIRONMENT_VARIABLES.md`, in the `HELIX_MOCK_AMS` section (around line 488-518), add the new mode:

```markdown
| `htlf_toolchanger` | AFC HTLF + Toolchanger: 4 HTLF lanes (2 direct, 2 hub→shared extruder) + 3 standalone toolheads. Tests MIXED topology. |
```

Also add aliases: `htlf_tc`, `htlf`

- [ ] **Step 5: Build and quick visual test**

Run: `make -j`
Expected: Clean compile

Run in background: `HELIX_MOCK_AMS=htlf ./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/htlf-test.log`

Verify in logs: Look for `HTLF+Toolchanger mode` and `MIXED` topology in the output.

- [ ] **Step 6: Commit**

```
git add include/ams_backend_mock.h src/printer/ams_backend_mock.cpp src/application/runtime_config.cpp docs/devel/ENVIRONMENT_VARIABLES.md
git commit -m "feat(mock): add HTLF+Toolchanger mock mode for MIXED topology testing (#364)"
```

---

### Task 5: draw_mixed_topology() for unit detail view

New drawing function that renders direct lanes to individual nozzles and hub lanes converging through hub box to shared nozzle.

**Files:**
- Modify: `src/ui/ui_filament_path_canvas.cpp:1411-1563` (add new function near draw_parallel_topology)
- Modify: `src/ui/ui_filament_path_canvas.cpp:1576-1581` (dispatch in draw callback)

- [ ] **Step 1: Add dispatch in filament_path_draw_cb**

In `src/ui/ui_filament_path_canvas.cpp`, at line 1578 (before the PARALLEL check), add:

```cpp
    // For MIXED topology (some lanes direct, some through hub), use dedicated function
    if (data->topology == static_cast<int>(PathTopology::MIXED)) {
        draw_mixed_topology(e, data);
        return;
    }
```

- [ ] **Step 2: Implement draw_mixed_topology**

Add a new static function before the dispatch callback (around line 1411, near `draw_parallel_topology`). The function draws Option B layout:

```cpp
/// MIXED topology: some lanes go direct to individual nozzles (like PARALLEL),
/// others converge through a hub box to a shared nozzle (like HUB).
/// Used by HTLF units where lanes 1-2 are direct and lanes 3-4 share an extruder via hub.
static void draw_mixed_topology(lv_event_t* e, FilamentPathData* data) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    int32_t height = lv_area_get_height(&obj_coords);
    int32_t x_off = obj_coords.x1;
    int32_t y_off = obj_coords.y1;

    // Layout ratios (shared with parallel topology for consistency)
    constexpr float ENTRY_Y = -0.12f;
    constexpr float SENSOR_Y = 0.30f;
    constexpr float HUB_Y = 0.52f;     // Hub box Y position (between sensor and nozzle)
    constexpr float TOOLHEAD_Y = 0.70f; // Nozzle position

    int32_t entry_y = y_off + (int32_t)(height * ENTRY_Y);
    int32_t sensor_y = y_off + (int32_t)(height * SENSOR_Y);
    int32_t hub_y = y_off + (int32_t)(height * HUB_Y);
    int32_t toolhead_y = y_off + (int32_t)(height * TOOLHEAD_Y);

    lv_color_t idle_color = data->color_idle;
    lv_color_t bg_color = data->color_bg;
    lv_color_t nozzle_color = data->color_nozzle;
    int32_t line_active = data->line_width_active;
    int32_t sensor_r = data->sensor_radius;
    int32_t tool_scale = LV_MAX(6, data->extruder_scale * 2 / 3);

    // Identify hub lanes and compute hub X position (average of hub lane Xs)
    int hub_lane_count = 0;
    int32_t hub_x_sum = 0;
    for (int i = 0; i < data->slot_count; i++) {
        if (data->slot_is_hub_routed[i]) {
            hub_lane_count++;
            hub_x_sum += get_slot_x(data, i, x_off);
        }
    }
    int32_t hub_center_x = (hub_lane_count > 0) ? (x_off + hub_x_sum / hub_lane_count) : 0;

    // Phase 1: Draw entry lines and sensor dots for ALL lanes
    for (int i = 0; i < data->slot_count; i++) {
        int32_t slot_x = x_off + get_slot_x(data, i, x_off);
        bool is_mounted = (i == data->active_slot);

        lv_color_t tool_color = idle_color;
        bool has_filament = false;
        PathSegment slot_segment = PathSegment::NONE;

        if (i < FilamentPathData::MAX_SLOTS &&
            data->slot_filament_states[i].segment != PathSegment::NONE) {
            has_filament = true;
            tool_color = lv_color_hex(data->slot_filament_states[i].color);
            slot_segment = data->slot_filament_states[i].segment;
        }
        if (is_mounted && data->filament_segment > 0) {
            tool_color = lv_color_hex(data->filament_color);
            has_filament = true;
            slot_segment = static_cast<PathSegment>(data->filament_segment);
        }

        bool at_sensor = has_filament && (slot_segment >= PathSegment::LANE);

        // Entry → sensor line
        if (has_filament) {
            draw_glow_line(layer, slot_x, entry_y, slot_x, sensor_y - sensor_r,
                           tool_color, line_active);
            draw_vertical_line(layer, slot_x, entry_y, sensor_y - sensor_r,
                               tool_color, line_active);
        } else {
            draw_hollow_vertical_line(layer, slot_x, entry_y, sensor_y - sensor_r,
                                      idle_color, bg_color, line_active);
        }

        // Sensor dot
        lv_color_t sensor_color = at_sensor ? tool_color : idle_color;
        draw_sensor_dot(layer, slot_x, sensor_y, sensor_color, at_sensor, sensor_r);
    }

    // Phase 2: Draw paths from sensors to nozzles
    // Direct lanes: straight to individual nozzle
    // Hub lanes: S-curve to hub box, then hub output to shared nozzle

    // Draw hub box first (behind paths)
    if (hub_lane_count > 0) {
        int32_t hub_w = LV_MAX(40, (hub_lane_count > 1)
            ? (x_off + get_slot_x(data, data->slot_count - 1, x_off)
               - x_off - get_slot_x(data, 0, x_off)) / 3
            : 50);
        int32_t hub_h = 20;

        // Check if any hub lane has filament at hub
        bool hub_has_filament = false;
        lv_color_t hub_tint = idle_color;
        for (int i = 0; i < data->slot_count; i++) {
            if (!data->slot_is_hub_routed[i]) continue;
            if (i < FilamentPathData::MAX_SLOTS &&
                data->slot_filament_states[i].segment >= PathSegment::HUB) {
                hub_has_filament = true;
                hub_tint = lv_color_hex(data->slot_filament_states[i].color);
            }
            if (i == data->active_slot && data->filament_segment >= static_cast<int>(PathSegment::HUB)) {
                hub_has_filament = true;
                hub_tint = lv_color_hex(data->filament_color);
            }
        }

        lv_color_t hub_bg = hub_has_filament ? ph_blend(data->color_idle, hub_tint, 0.33f) : data->color_idle;
        lv_color_t hub_border = hub_has_filament ? hub_tint : data->color_idle;
        draw_hub_box(layer, hub_center_x, hub_y, hub_w, hub_h, hub_bg, hub_border,
                     data->color_text, data->label_font, data->border_radius, "HUB", LV_OPA_COVER);
    }

    // Draw each lane's path from sensor to nozzle
    for (int i = 0; i < data->slot_count; i++) {
        int32_t slot_x = x_off + get_slot_x(data, i, x_off);
        bool is_mounted = (i == data->active_slot);
        bool is_hub = data->slot_is_hub_routed[i];

        lv_color_t tool_color = idle_color;
        bool has_filament = false;
        PathSegment slot_segment = PathSegment::NONE;

        if (i < FilamentPathData::MAX_SLOTS &&
            data->slot_filament_states[i].segment != PathSegment::NONE) {
            has_filament = true;
            tool_color = lv_color_hex(data->slot_filament_states[i].color);
            slot_segment = data->slot_filament_states[i].segment;
        }
        if (is_mounted && data->filament_segment > 0) {
            tool_color = lv_color_hex(data->filament_color);
            has_filament = true;
            slot_segment = static_cast<PathSegment>(data->filament_segment);
        }

        bool at_nozzle = has_filament && (slot_segment >= PathSegment::NOZZLE);

        if (is_hub) {
            // Hub lane: S-curve from sensor to hub box top
            bool past_sensor = has_filament && (slot_segment >= PathSegment::LANE);
            int32_t hub_top = hub_y - 10; // Half hub height

            if (past_sensor) {
                // Draw S-curve from sensor to hub via bezier approximation
                // Simple: vertical from sensor, then angle toward hub_center_x
                int32_t mid_y = sensor_y + (hub_top - sensor_y) / 2;
                draw_vertical_line(layer, slot_x, sensor_y + sensor_r, mid_y, tool_color, line_active);

                // Angled line from mid to hub top
                lv_draw_line_dsc_t line_dsc;
                lv_draw_line_dsc_init(&line_dsc);
                line_dsc.color = tool_color;
                line_dsc.width = line_active;
                line_dsc.round_start = 1;
                line_dsc.round_end = 1;
                line_dsc.p1.x = slot_x;
                line_dsc.p1.y = mid_y;
                line_dsc.p2.x = hub_center_x;
                line_dsc.p2.y = hub_top;
                lv_draw_line(layer, &line_dsc);
            } else {
                draw_hollow_vertical_line(layer, slot_x, sensor_y + sensor_r,
                                          hub_top, idle_color, bg_color, line_active);
            }
        } else {
            // Direct lane: straight vertical from sensor to nozzle
            int32_t nozzle_top = toolhead_y - tool_scale * 2;
            if (at_nozzle) {
                draw_glow_line(layer, slot_x, sensor_y + sensor_r, slot_x, nozzle_top,
                               tool_color, line_active);
                draw_vertical_line(layer, slot_x, sensor_y + sensor_r, nozzle_top,
                                   tool_color, line_active);
            } else if (has_filament) {
                draw_vertical_line(layer, slot_x, sensor_y + sensor_r, nozzle_top,
                                   tool_color, line_active);
            } else {
                draw_hollow_vertical_line(layer, slot_x, sensor_y + sensor_r, nozzle_top,
                                          idle_color, bg_color, line_active);
            }
        }
    }

    // Hub output: line from hub bottom to shared nozzle
    if (hub_lane_count > 0) {
        int32_t hub_bottom = hub_y + 10;
        int32_t nozzle_top = toolhead_y - tool_scale * 2;

        // Check if any hub lane has filament past hub
        bool hub_output_active = false;
        lv_color_t hub_out_color = idle_color;
        for (int i = 0; i < data->slot_count; i++) {
            if (!data->slot_is_hub_routed[i]) continue;
            bool is_active = (i == data->active_slot);
            PathSegment seg = PathSegment::NONE;
            if (i < FilamentPathData::MAX_SLOTS)
                seg = data->slot_filament_states[i].segment;
            if (is_active && data->filament_segment >= static_cast<int>(PathSegment::OUTPUT)) {
                hub_output_active = true;
                hub_out_color = lv_color_hex(data->filament_color);
            } else if (seg >= PathSegment::OUTPUT) {
                hub_output_active = true;
                hub_out_color = lv_color_hex(data->slot_filament_states[i].color);
            }
        }

        if (hub_output_active) {
            draw_glow_line(layer, hub_center_x, hub_bottom, hub_center_x, nozzle_top,
                           hub_out_color, line_active);
            draw_vertical_line(layer, hub_center_x, hub_bottom, nozzle_top,
                               hub_out_color, line_active);
        } else {
            draw_hollow_vertical_line(layer, hub_center_x, hub_bottom, nozzle_top,
                                      idle_color, bg_color, line_active);
        }
    }

    // Phase 3: Draw nozzles
    // Track which nozzle positions we've drawn (avoid duplicate for shared hub nozzle)
    bool hub_nozzle_drawn = false;

    for (int i = 0; i < data->slot_count; i++) {
        bool is_hub = data->slot_is_hub_routed[i];
        int32_t nozzle_x;

        if (is_hub) {
            if (hub_nozzle_drawn) continue; // Only draw shared hub nozzle once
            hub_nozzle_drawn = true;
            nozzle_x = hub_center_x;
        } else {
            nozzle_x = x_off + get_slot_x(data, i, x_off);
        }

        bool is_mounted = (i == data->active_slot);
        lv_color_t tool_color = idle_color;
        bool has_filament = false;

        if (i < FilamentPathData::MAX_SLOTS &&
            data->slot_filament_states[i].segment != PathSegment::NONE) {
            has_filament = true;
            tool_color = lv_color_hex(data->slot_filament_states[i].color);
        }
        if (is_mounted && data->filament_segment > 0) {
            tool_color = lv_color_hex(data->filament_color);
            has_filament = true;
        }

        bool at_nozzle = has_filament && ((is_hub ? false : true) || // Direct: check slot's segment
            (data->filament_segment >= static_cast<int>(PathSegment::NOZZLE)));

        // For hub nozzle, check if any hub lane has filament at nozzle
        if (is_hub) {
            at_nozzle = false;
            for (int j = 0; j < data->slot_count; j++) {
                if (!data->slot_is_hub_routed[j]) continue;
                if (j == data->active_slot && data->filament_segment >= static_cast<int>(PathSegment::NOZZLE)) {
                    at_nozzle = true;
                    is_mounted = true;
                    tool_color = lv_color_hex(data->filament_color);
                }
            }
        } else {
            PathSegment seg = (i < FilamentPathData::MAX_SLOTS) ?
                data->slot_filament_states[i].segment : PathSegment::NONE;
            at_nozzle = has_filament && (seg >= PathSegment::NOZZLE);
        }

        lv_color_t noz_color = is_mounted ? nozzle_color : ph_darken(nozzle_color, 60);
        if (at_nozzle) noz_color = tool_color;
        lv_opa_t toolhead_opa = is_mounted ? LV_OPA_COVER : LV_OPA_40;

        switch (helix::SettingsManager::instance().get_effective_toolhead_style()) {
            case helix::ToolheadStyle::STEALTHBURNER:
                draw_nozzle_faceted(layer, nozzle_x, toolhead_y, noz_color, tool_scale, toolhead_opa);
                break;
            case helix::ToolheadStyle::A4T:
                draw_nozzle_a4t(layer, nozzle_x, toolhead_y, noz_color, tool_scale * 6 / 5, toolhead_opa);
                break;
            default:
                draw_nozzle_bambu(layer, nozzle_x, toolhead_y, noz_color, tool_scale, toolhead_opa);
                break;
        }

        // Tool badge below nozzle
        if (data->label_font) {
            char tool_label[16];
            int tool = (data->mapped_tool[i] >= 0) ? data->mapped_tool[i] : i;
            snprintf(tool_label, sizeof(tool_label), "T%d", tool);

            int32_t font_h = lv_font_get_line_height(data->label_font);
            int32_t label_len = (int32_t)strlen(tool_label);
            int32_t badge_w = LV_MAX(24, label_len * (font_h * 3 / 5) + 6);
            int32_t badge_h = font_h + 4;
            int32_t badge_top = toolhead_y + tool_scale * 3 + 4;
            int32_t badge_left = nozzle_x - badge_w / 2;

            lv_area_t badge_area = {badge_left, badge_top, badge_left + badge_w, badge_top + badge_h};
            lv_draw_fill_dsc_t fill_dsc;
            lv_draw_fill_dsc_init(&fill_dsc);
            fill_dsc.color = data->color_idle;
            fill_dsc.opa = (lv_opa_t)LV_MIN(200, toolhead_opa);
            fill_dsc.radius = 4;
            lv_draw_fill(layer, &fill_dsc, &badge_area);

            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = is_mounted ? data->color_success : data->color_text;
            label_dsc.opa = toolhead_opa;
            label_dsc.font = data->label_font;
            label_dsc.align = LV_TEXT_ALIGN_CENTER;
            label_dsc.text = tool_label;
            label_dsc.text_local = 1;

            lv_area_t text_area = {badge_left, badge_top + 2, badge_left + badge_w, badge_top + 2 + font_h};
            lv_draw_label(layer, &label_dsc, &text_area);
        }
    }
}
```

**Note:** This is a first pass. The S-curve from sensor to hub could be refined to use proper cubic bezier (like the HUB topology does). Start with the simpler angled-line approach and iterate visually.

- [ ] **Step 3: Build and visual test**

Run: `make -j`

Start the app with HTLF mock: `HELIX_MOCK_AMS=htlf ./build/bin/helix-screen --test -vv`

Navigate to Multi Filament System overview → click HTLF_1 unit to open detail view. Verify:
- 4 spools shown at top
- 2 direct lanes have individual nozzles below
- 2 hub lanes converge to hub box with shared nozzle
- Tool labels show correct mapped_tool values
- No badge leak when switching between units

- [ ] **Step 4: Commit**

```
git add src/ui/ui_filament_path_canvas.cpp
git commit -m "feat(ams): add draw_mixed_topology for HTLF direct+hub lane rendering (#364)"
```

---

## Chunk 3: Overview Canvas, Empty Slots, Polish

### Task 6: System path canvas overview MIXED support

Update the overview page routing to handle MIXED units correctly.

**Files:**
- Modify: `src/ui/ams_drawing_utils.cpp:346-395` (compute_system_tool_layout)
- Modify: `src/ui/ui_system_path_canvas.cpp:683-862` (route generation)

- [ ] **Step 1: Update compute_system_tool_layout for MIXED**

In `src/ui/ams_drawing_utils.cpp`, in `compute_system_tool_layout()`, the PARALLEL branch counts all slots as tools. For MIXED, count **unique mapped_tool values** instead:

Find the section that handles topology per unit (around line 383). After the `if (topo != PathTopology::PARALLEL)` block, add a MIXED case:

```cpp
        if (topo == PathTopology::MIXED) {
            // MIXED: count unique extruders, not lanes
            // Lanes sharing a mapped_tool value share a physical nozzle
            std::set<int> unique_tools;
            for (const auto& slot : unit.slots) {
                if (slot.mapped_tool >= 0)
                    unique_tools.insert(slot.mapped_tool);
            }
            utl.tool_count = unique_tools.empty() ? unit.slot_count : static_cast<int>(unique_tools.size());
            utl.first_physical_tool = min_tool;
            // Hub tool label from shared extruder (lowest tool among hub lanes)
            if (unit.hub_tool_label >= 0) {
                // Use explicit hub tool label if set
            } else {
                // Derive from hub lanes' mapped_tool
                for (size_t si = 0; si < unit.slots.size() && si < unit.lane_is_hub_routed.size(); ++si) {
                    if (unit.lane_is_hub_routed[si]) {
                        utl.hub_tool_label = unit.slots[si].mapped_tool;
                        break;
                    }
                }
            }
        } else if (topo != PathTopology::PARALLEL) {
```

- [ ] **Step 2: Update route generation for MIXED units**

In `src/ui/ui_system_path_canvas.cpp`, in PASS 1 route generation (around line 683), add MIXED handling. For MIXED units, generate routes like PARALLEL but hub lanes share a single target nozzle:

```cpp
// For MIXED topology: direct lanes get independent routes,
// hub lanes share a single target tool position
if (unit_topo == static_cast<int>(PathTopology::MIXED)) {
    // Get per-lane hub routing from the AmsUnit
    auto* backend = AmsState::instance().get_backend();
    AmsSystemInfo info;
    if (backend) info = backend->get_system_info();

    std::set<int> hub_tools_drawn; // Track which hub nozzle positions we've drawn routes for

    for (int s = 0; s < unit_slot_count; s++) {
        int gi = unit_first_slot + s;
        bool is_hub = false;
        if (i < static_cast<int>(info.units.size()) &&
            s < static_cast<int>(info.units[i].lane_is_hub_routed.size())) {
            is_hub = info.units[i].lane_is_hub_routed[s];
        }

        int mapped = /* get mapped_tool for slot gi */;
        if (is_hub) {
            // Only draw one route per shared hub nozzle
            if (hub_tools_drawn.count(mapped)) continue;
            hub_tools_drawn.insert(mapped);
        }

        // Create route from unit to target tool position
        // (same as existing PARALLEL route generation)
    }
}
```

This is a sketch — the exact implementation depends on how `build_routed_path` works and how tool positions are calculated. Study the existing PARALLEL branch and adapt.

- [ ] **Step 3: Build and visual test**

Run: `make -j`

Start with HTLF mock and check the overview page shows:
- HTLF_1 card with 4 slots and 3 routing lines (2 direct + 1 for hub lanes)
- Tools card with 3 slots and 3 routing lines
- Correct nozzle count at bottom

- [ ] **Step 4: Commit**

```
git add src/ui/ams_drawing_utils.cpp src/ui/ui_system_path_canvas.cpp
git commit -m "feat(ams): support MIXED topology in overview system path canvas (#364)"
```

---

### Task 7: Empty slot visualization improvement

Replace the faint border circle with dashed circle + plus icon + "Empty" label.

**Files:**
- Modify: `src/ui/ui_ams_slot.cpp:643-656` (placeholder creation)
- Modify: `src/ui/ui_ams_slot.cpp:283-298` (apply_slot_status empty handling)

- [ ] **Step 1: Update empty_placeholder creation**

In `src/ui/ui_ams_slot.cpp`, replace the placeholder creation block (lines 643-656):

```cpp
    {
        lv_obj_t* ph = lv_obj_create(data->spool_container);
        lv_obj_set_size(ph, spool_size - 4, spool_size - 4);
        lv_obj_align(ph, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(ph, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ph, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ph, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(ph, theme_manager_get_color("text_muted"), LV_PART_MAIN);
        lv_obj_set_style_border_opa(ph, LV_OPA_60, LV_PART_MAIN);
        // Dashed border effect
        lv_obj_set_style_border_dash_width(ph, 4, LV_PART_MAIN);
        lv_obj_set_style_border_dash_gap(ph, 4, LV_PART_MAIN);
        lv_obj_remove_flag(ph, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ph, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(ph, LV_OBJ_FLAG_HIDDEN);

        // Plus icon centered in circle
        lv_obj_t* plus = lv_label_create(ph);
        lv_label_set_text(plus, ICON_PLUS);
        lv_obj_set_style_text_font(plus, ui_get_icon_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(plus, theme_manager_get_color("text_muted"), LV_PART_MAIN);
        lv_obj_set_style_text_opa(plus, LV_OPA_60, LV_PART_MAIN);
        lv_obj_align(plus, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(plus, LV_OBJ_FLAG_EVENT_BUBBLE);

        data->empty_placeholder = ph;
    }
```

**Note:** Check if `lv_obj_set_style_border_dash_width` and `lv_obj_set_style_border_dash_gap` exist in LVGL 9.5. If not, the dashed effect may need a different approach (e.g., a canvas draw callback). The slightly higher opacity (60% vs 40%) and plus icon should already be a big improvement even without dashes.

- [ ] **Step 2: Set "Empty" text on material_label for empty unassigned slots**

In `src/ui/ui_ams_slot.cpp`, in `apply_slot_status()` around line 296 (the unassigned empty case), add:

```cpp
        } else {
            // Unassigned and empty: hide spool, show empty placeholder circle
            show_spool = false;
            show_empty_placeholder = true;
            // Set material label to "Empty" for unassigned slots
            if (data->material_label) {
                lv_label_set_text(data->material_label, lv_tr("Empty"));
            }
        }
```

- [ ] **Step 3: Build and visual test**

Run: `make -j`

Start with HTLF mock and verify:
- lane4 (empty) shows dashed circle with plus icon
- extruder4, extruder5 (empty) show dashed circle with plus icon
- "Empty" text appears above empty slots
- Filled slots are not affected

- [ ] **Step 4: Commit**

```
git add src/ui/ui_ams_slot.cpp
git commit -m "fix(ams): improve empty slot visualization with plus icon and 'Empty' label (#364)"
```

---

### Task 8: Final testing and documentation

Run full test suite, verify no regressions, update any remaining docs.

**Files:**
- Run: `make test-run`
- Review: All modified files for consistency

- [ ] **Step 1: Run full test suite**

Run: `make test-run`
Expected: All existing tests pass (no regressions)

- [ ] **Step 2: Run HTLF mock visual verification**

Start: `HELIX_MOCK_AMS=htlf ./build/bin/helix-screen --test -vv`

Verify checklist:
- [ ] Overview page: HTLF shows 3 nozzle positions with correct routing
- [ ] Overview page: Tools unit shows 3 independent nozzles
- [ ] HTLF detail: 4 spools, 3 nozzles, hub box between lanes 3+4
- [ ] HTLF detail: Tool labels T0, T2, T1, T3 (matching AFC map)
- [ ] Tools detail: Tool labels T4, T5, T6
- [ ] Switch between units: no badge leak
- [ ] Empty slots: dashed circle + plus icon + "Empty" text
- [ ] Existing mock modes still work (try `HELIX_MOCK_AMS=afc`, `mixed`)

- [ ] **Step 3: Commit any final fixes**

If any visual or functional issues found during verification, fix and commit individually.
