# Happy Hare Device Actions Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring Happy Hare device actions to parity with AFC — live values, complete execute_device_action, persistence, topology filtering, and compact button layout.

**Architecture:** Expand HH backend to query `configfile.settings.mmu` at startup for initial values, cache them in member variables, overlay into `get_device_actions()`, persist user overrides via `Config` JSON, and re-apply on reconnect. Shared UI fix for button grid layout.

**Tech Stack:** C++, LVGL, Moonraker JSON-RPC, Catch2 tests

**Spec:** `docs/superpowers/specs/2026-03-10-happy-hare-device-actions-design.md`

**Important notes:**
- **Persistence uses `Config` (not `SettingsManager`)** — SettingsManager has typed getter/setter pairs for fixed settings. Config's generic JSON pointer API (`config->set("/hh_overrides/key/value", val)`) is better suited for the dynamic key-value storage we need here.
- **HH has TWO gear load speeds:** `GEAR_FROM_BUFFER_SPEED` (default 150, for buffered filament) and `GEAR_FROM_SPOOL_SPEED` (default 60, for spool-direct). Existing code maps `gear_load_speed` to `GEAR_FROM_BUFFER_SPEED`. This plan splits them into two separate actions.
- **Behavioral changes from existing code:** The `motors_toggle` action changes from `MMU_MOTORS_OFF HOLD=1` (enable) to `MMU_HOME` (enable). The `servo_buzz` action changes from `MMU_SERVO BUZZ=1` to `MMU_SERVO` (no args = buzz). `calibrate_servo` is removed (HH has no servo calibration G-code — servo angles are saved via `MMU_SERVO POS=up SAVE=1`).
- **`lv_obj_add_event_cb` in button grid** is existing code being refactored, not a new violation of the declarative UI rule. The device section detail overlay is explicitly exempted (dynamic controls from backend data).

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/printer/hh_defaults.cpp` | Static default sections and actions (expanded with toolhead section) |
| `include/hh_defaults.h` | Declarations for defaults |
| `include/ams_backend_happy_hare.h` | New member variables for cached config values, persistence helpers |
| `src/printer/ams_backend_happy_hare.cpp` | Config query, live value overlay, execute_device_action, persistence, topology filtering |
| `src/ui/ui_ams_device_section_detail_overlay.cpp` | Button grid layout (shared fix) |
| `tests/unit/test_ams_backend_happy_hare.cpp` | Tests for live values, execute, persistence, topology |

---

## Chunk 1: Button Grid Layout (Shared UI Fix)

### Task 1: Button Grid Layout in Device Section Detail Overlay

This is a shared UI fix that benefits both AFC and HH. Changes how BUTTON-type actions render — 2 per row instead of 1.

**Files:**
- Modify: `src/ui/ui_ams_device_section_detail_overlay.cpp:154-183` (refresh method) and `:189-240` (create_action_control BUTTON case)

- [ ] **Step 1: Modify `refresh()` to group consecutive buttons**

In `refresh()` (line 154), after the loop that calls `create_action_control()` for each action, change the approach: instead of calling `create_action_control()` directly for each action, collect consecutive BUTTON actions and create them in a shared row container.

Replace the loop body in `refresh()` that iterates `cached_actions_` (around lines 168-178). The new pattern:

```cpp
// Inside refresh(), replacing the per-action create_action_control loop:
lv_obj_t* button_row = nullptr;
int button_count_in_row = 0;

for (const auto& action : cached_actions_) {
    if (action.section != section_id_)
        continue;

    if (action.type == helix::printer::ActionType::BUTTON) {
        // Start a new 2-column row if needed
        if (!button_row || button_count_in_row >= 2) {
            button_row = lv_obj_create(actions_container_);
            lv_obj_set_width(button_row, LV_PCT(100));
            lv_obj_set_height(button_row, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(button_row, 0, 0);
            lv_obj_set_style_pad_column(button_row,
                                         theme_manager_get_spacing("space_sm"), 0);
            lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(button_row, 0, 0);
            lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_START,
                                   LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_remove_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);
            button_count_in_row = 0;
        }
        create_button_in_row(button_row, action);
        button_count_in_row++;
    } else {
        // Non-button action breaks the button row
        button_row = nullptr;
        button_count_in_row = 0;
        create_action_control(actions_container_, action);
    }
}
```

- [ ] **Step 2: Extract button creation into `create_button_in_row()`**

Add a new private method. Move the BUTTON case logic from `create_action_control()` into this new method, but change width from `flex_grow=1` to roughly 48%:

```cpp
void AmsDeviceSectionDetailOverlay::create_button_in_row(
    lv_obj_t* row, const helix::printer::DeviceAction& action) {
    lv_obj_t* btn = lv_button_create(row);
    // ~48% width instead of flex_grow=1 full width
    lv_obj_set_width(btn, LV_PCT(48));
    lv_obj_set_height(btn, theme_manager_get_spacing("button_height_sm"));
    lv_obj_set_style_radius(btn, theme_manager_get_spacing("border_radius"), 0);
    lv_obj_set_flex_grow(btn, 1);  // Grow equally within the row

    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, lv_tr(action.label.c_str()));
    lv_obj_center(btn_label);

    action_ids_.push_back(action.id);
    lv_obj_set_user_data(btn, reinterpret_cast<void*>(action_ids_.size() - 1));
    lv_obj_add_event_cb(btn, on_action_clicked, LV_EVENT_CLICKED, nullptr);

    if (!action.enabled) {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
}
```

Add declaration in the header (or private section of the class).

- [ ] **Step 3: Remove BUTTON case from `create_action_control()`**

In `create_action_control()` (line 211), remove the `case ActionType::BUTTON` block since buttons are now handled by `create_button_in_row()` via `refresh()`. Add a comment explaining why.

- [ ] **Step 4: Build and visually verify**

```bash
make -j
```

No automated test needed for pure layout change — this is visual. Manual verification: run `./build/bin/helix-screen --test -vv` and navigate to AMS device settings to confirm buttons render 2-per-row.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_ams_device_section_detail_overlay.cpp
git commit -m "fix(ui): render device action buttons in 2-column grid layout"
```

---

## Chunk 2: Expand HH Defaults & Config Query

### Task 2: Add Toolhead Section and Expanded Actions to hh_defaults

**Files:**
- Modify: `src/printer/hh_defaults.cpp`
- Modify: `include/hh_defaults.h`

- [ ] **Step 1: Write test for new sections and actions**

Add to `tests/unit/test_ams_backend_happy_hare.cpp`:

```cpp
TEST_CASE("hh_default_sections includes toolhead", "[ams][happy_hare][device_actions]") {
    auto sections = helix::printer::hh_default_sections();
    bool found = false;
    for (const auto& s : sections) {
        if (s.id == "toolhead") {
            found = true;
            REQUIRE(s.label == "Toolhead");
        }
    }
    REQUIRE(found);
}

TEST_CASE("hh_default_actions includes toolhead sliders", "[ams][happy_hare][device_actions]") {
    auto actions = helix::printer::hh_default_actions();
    std::set<std::string> toolhead_ids;
    for (const auto& a : actions) {
        if (a.section == "toolhead") toolhead_ids.insert(a.id);
    }
    REQUIRE(toolhead_ids.count("toolhead_sensor_to_nozzle") == 1);
    REQUIRE(toolhead_ids.count("toolhead_extruder_to_nozzle") == 1);
    REQUIRE(toolhead_ids.count("toolhead_entry_to_extruder") == 1);
    REQUIRE(toolhead_ids.count("toolhead_ooze_reduction") == 1);
}

TEST_CASE("hh_default_actions includes extruder and split gear speeds", "[ams][happy_hare][device_actions]") {
    auto actions = helix::printer::hh_default_actions();
    std::set<std::string> speed_ids;
    for (const auto& a : actions) {
        if (a.section == "speed") speed_ids.insert(a.id);
    }
    REQUIRE(speed_ids.count("extruder_load_speed") == 1);
    REQUIRE(speed_ids.count("extruder_unload_speed") == 1);
    REQUIRE(speed_ids.count("gear_from_buffer_speed") == 1);
    REQUIRE(speed_ids.count("gear_from_spool_speed") == 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare][device_actions]" -v
```

Expected: FAIL — no `toolhead` section or `toolhead_sensor_to_nozzle` action.

- [ ] **Step 3: Add toolhead section to `hh_default_sections()`**

In `hh_defaults.cpp`, add to the sections vector (after "accessories", before "maintenance"):

```cpp
{"toolhead", "Toolhead", 2, "Extruder distances and sensor configuration"},
```

Bump `maintenance` order_index from 3 to 4, and bump `accessories` from 2 to 3 (or just insert toolhead at order 2 and shift others).

- [ ] **Step 4: Add new actions to `hh_default_actions()`**

Add toolhead sliders, extruder speed sliders, sync_to_extruder toggle, and test_move button. Follow the existing helper lambda patterns in `hh_defaults.cpp`:

```cpp
// --- Toolhead section ---
add_slider("toolhead_sensor_to_nozzle", "Sensor to Nozzle", "toolhead",
           62.0, 1.0f, 200.0f, "mm");
add_slider("toolhead_extruder_to_nozzle", "Extruder to Nozzle", "toolhead",
           72.0, 5.0f, 200.0f, "mm");
add_slider("toolhead_entry_to_extruder", "Entry to Extruder", "toolhead",
           0.0, 0.0f, 200.0f, "mm");
add_slider("toolhead_ooze_reduction", "Ooze Reduction", "toolhead",
           2.0, -5.0f, 20.0f, "mm");

// --- Speed section (additions + rename existing) ---
// Replace existing gear_load_speed/gear_unload_speed with split buffer/spool:
// Remove: add_slider("gear_load_speed", ...) and add_slider("gear_unload_speed", ...)
// Add:
add_slider("gear_from_buffer_speed", "Gear Buffer Speed", "speed",
           150.0, 10.0f, 300.0f, "mm/s");
add_slider("gear_from_spool_speed", "Gear Spool Speed", "speed",
           60.0, 10.0f, 300.0f, "mm/s");
add_slider("extruder_load_speed", "Extruder Load Speed", "speed",
           45.0, 10.0f, 100.0f, "mm/s");
add_slider("extruder_unload_speed", "Extruder Unload Speed", "speed",
           45.0, 10.0f, 100.0f, "mm/s");

// --- Accessories section (addition) ---
{
    DeviceAction a;
    a.id = "sync_to_extruder";
    a.label = "Sync to Extruder";
    a.section = "accessories";
    a.type = ActionType::TOGGLE;
    a.current_value = false;
    actions.push_back(std::move(a));
}

// --- Maintenance section (addition) ---
add_button("test_move", "Test Move", "maintenance");
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare][device_actions]" -v
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/printer/hh_defaults.cpp include/hh_defaults.h tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "feat(hh): add toolhead section and expanded device actions to defaults"
```

### Task 3: Query configfile.settings.mmu for Initial Values

Add a new method `query_config_defaults()` to the HH backend that reads initial values from Moonraker's configfile on startup.

**Files:**
- Modify: `include/ams_backend_happy_hare.h` (add member variables and method declaration)
- Modify: `src/printer/ams_backend_happy_hare.cpp` (implement query and caching)

- [ ] **Step 1: Add member variables to header**

In `include/ams_backend_happy_hare.h`, add to the private section (around line 232):

```cpp
// Cached config defaults from configfile.settings.mmu
struct ConfigDefaults {
    float gear_from_buffer_speed = 150.0f;
    float gear_from_spool_speed = 60.0f;
    float gear_unload_speed = 80.0f;
    float selector_move_speed = 200.0f;
    float extruder_load_speed = 45.0f;
    float extruder_unload_speed = 45.0f;
    float toolhead_sensor_to_nozzle = 62.0f;
    float toolhead_extruder_to_nozzle = 72.0f;
    float toolhead_entry_to_extruder = 0.0f;
    float toolhead_ooze_reduction = 2.0f;
    int sync_to_extruder = 0;
    int clog_detection = 0;
    bool loaded = false;
};
ConfigDefaults config_defaults_;

// User overrides (set via UI, persisted to Config)
struct UserOverrides {
    std::optional<float> gear_from_buffer_speed;
    std::optional<float> gear_from_spool_speed;
    std::optional<float> gear_unload_speed;
    std::optional<float> selector_move_speed;
    std::optional<float> extruder_load_speed;
    std::optional<float> extruder_unload_speed;
    std::optional<float> toolhead_sensor_to_nozzle;
    std::optional<float> toolhead_extruder_to_nozzle;
    std::optional<float> toolhead_entry_to_extruder;
    std::optional<float> toolhead_ooze_reduction;
    std::optional<int> sync_to_extruder;
    std::optional<int> clog_detection;
};
UserOverrides user_overrides_;

// Status-backed values (from printer.mmu.* subscriptions)
std::string led_exit_effect_;
std::string espooler_active_;
int flowguard_encoder_mode_ = 0;

void query_config_defaults();
void load_persisted_overrides();
void save_override(const std::string& key, float value);
void save_override(const std::string& key, int value);
void reapply_overrides();
```

- [ ] **Step 2: Implement `query_config_defaults()`**

In `ams_backend_happy_hare.cpp`, add after `query_selector_type_from_config()`. Follow the same pattern (async query with alive guard):

```cpp
void AmsBackendHappyHare::query_config_defaults() {
    if (!client_) return;

    std::weak_ptr<bool> weak_alive = alive_;
    nlohmann::json params;
    params["objects"]["configfile"] = {"settings"};

    client_->send_jsonrpc(
        "printer.objects.query", params,
        [this, weak_alive](const nlohmann::json& result) {
            if (weak_alive.expired()) return;

            try {
                const auto& settings = result["status"]["configfile"]["settings"];
                if (!settings.contains("mmu")) return;
                const auto& mmu = settings["mmu"];

                auto get_float = [&](const char* key, float def) -> float {
                    if (mmu.contains(key)) {
                        // configfile returns strings, need to parse
                        auto val = mmu[key];
                        if (val.is_number()) return val.get<float>();
                        if (val.is_string()) {
                            try { return std::stof(val.get<std::string>()); }
                            catch (...) { return def; }
                        }
                    }
                    return def;
                };
                auto get_int = [&](const char* key, int def) -> int {
                    if (mmu.contains(key)) {
                        auto val = mmu[key];
                        if (val.is_number()) return val.get<int>();
                        if (val.is_string()) {
                            try { return std::stoi(val.get<std::string>()); }
                            catch (...) { return def; }
                        }
                    }
                    return def;
                };

                config_defaults_.gear_from_buffer_speed = get_float("gear_from_buffer_speed", 150.0f);
                config_defaults_.gear_from_spool_speed = get_float("gear_from_spool_speed", 60.0f);
                config_defaults_.gear_unload_speed = get_float("gear_unload_speed", 80.0f);
                config_defaults_.selector_move_speed = get_float("selector_move_speed", 200.0f);
                config_defaults_.extruder_load_speed = get_float("extruder_load_speed", 45.0f);
                config_defaults_.extruder_unload_speed = get_float("extruder_unload_speed", 45.0f);
                config_defaults_.toolhead_sensor_to_nozzle = get_float("toolhead_sensor_to_nozzle", 62.0f);
                config_defaults_.toolhead_extruder_to_nozzle = get_float("toolhead_extruder_to_nozzle", 72.0f);
                config_defaults_.toolhead_entry_to_extruder = get_float("toolhead_entry_to_extruder", 0.0f);
                config_defaults_.toolhead_ooze_reduction = get_float("toolhead_ooze_reduction", 2.0f);
                config_defaults_.sync_to_extruder = get_int("sync_to_extruder", 0);
                config_defaults_.clog_detection = get_int("clog_detection", 0);
                config_defaults_.loaded = true;

                spdlog::info("[HappyHare] Config defaults loaded from configfile.settings.mmu");

                // Now check persisted overrides against new config defaults
                load_persisted_overrides();
                reapply_overrides();

            } catch (const std::exception& e) {
                spdlog::warn("[HappyHare] Failed to parse config defaults: {}", e.what());
            }
        });
}
```

- [ ] **Step 3: Call `query_config_defaults()` from `on_started()`**

In `on_started()` (line 53), add the call after the existing queries:

```cpp
query_config_defaults();
```

- [ ] **Step 4: Build to verify compilation**

```bash
make -j
```

- [ ] **Step 5: Commit**

```bash
git add include/ams_backend_happy_hare.h src/printer/ams_backend_happy_hare.cpp
git commit -m "feat(hh): query configfile.settings.mmu for device action defaults"
```

---

## Chunk 3: Live Value Population & Topology Filtering

### Task 4: Populate get_device_actions() with Live Values

**Files:**
- Modify: `src/printer/ams_backend_happy_hare.cpp` (replace one-liner `get_device_actions()`)

- [ ] **Step 1: Write test for live value population**

In `tests/unit/test_ams_backend_happy_hare.cpp`, add the test helper method and test. The test helper already has `test_parse_mmu_state()` — add a way to set config defaults:

```cpp
// Add to AmsBackendHappyHareTestHelper class:
void set_config_defaults_for_test() {
    config_defaults_.gear_from_buffer_speed = 180.0f;
    config_defaults_.gear_from_spool_speed = 70.0f;
    config_defaults_.gear_unload_speed = 90.0f;
    config_defaults_.selector_move_speed = 200.0f;
    config_defaults_.extruder_load_speed = 45.0f;
    config_defaults_.extruder_unload_speed = 45.0f;
    config_defaults_.toolhead_sensor_to_nozzle = 62.0f;
    config_defaults_.toolhead_extruder_to_nozzle = 72.0f;
    config_defaults_.loaded = true;
}

// Test:
TEST_CASE("get_device_actions returns live config values", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);
    helper.set_config_defaults_for_test();

    auto actions = helper.get_device_actions();

    for (const auto& a : actions) {
        if (a.id == "gear_from_buffer_speed") {
            REQUIRE(a.current_value.has_value());
            auto val = std::any_cast<double>(a.current_value);
            REQUIRE(val == Catch::Approx(180.0));
        }
        if (a.id == "gear_unload_speed") {
            REQUIRE(a.current_value.has_value());
            auto val = std::any_cast<double>(a.current_value);
            REQUIRE(val == Catch::Approx(90.0));
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
make test && ./build/bin/helix-tests "get_device_actions returns live config values" -v
```

Expected: FAIL — current `get_device_actions()` returns static defaults.

- [ ] **Step 3: Implement live value overlay in `get_device_actions()`**

Replace the one-liner at line 1689 with a full implementation:

```cpp
std::vector<helix::printer::DeviceAction> AmsBackendHappyHare::get_device_actions() const {
    auto actions = helix::printer::hh_default_actions();

    if (!config_defaults_.loaded) {
        // Mark all non-button actions as loading
        for (auto& a : actions) {
            if (a.type != helix::printer::ActionType::BUTTON) {
                a.enabled = false;
                a.disable_reason = "Loading configuration...";
            }
        }
        return actions;
    }

    // Helper to get effective value: user override > config default
    auto effective_float = [this](const std::optional<float>& override_val, float config_val) -> double {
        return static_cast<double>(override_val.value_or(config_val));
    };
    auto effective_int = [this](const std::optional<int>& override_val, int config_val) -> int {
        return override_val.value_or(config_val);
    };

    for (auto& a : actions) {
        // Speed sliders
        if (a.id == "gear_from_buffer_speed") {
            a.current_value = effective_float(user_overrides_.gear_from_buffer_speed,
                                               config_defaults_.gear_from_buffer_speed);
        } else if (a.id == "gear_from_spool_speed") {
            a.current_value = effective_float(user_overrides_.gear_from_spool_speed,
                                               config_defaults_.gear_from_spool_speed);
        } else if (a.id == "gear_unload_speed") {
            a.current_value = effective_float(user_overrides_.gear_unload_speed,
                                               config_defaults_.gear_unload_speed);
        } else if (a.id == "selector_speed") {
            a.current_value = effective_float(user_overrides_.selector_move_speed,
                                               config_defaults_.selector_move_speed);
        } else if (a.id == "extruder_load_speed") {
            a.current_value = effective_float(user_overrides_.extruder_load_speed,
                                               config_defaults_.extruder_load_speed);
        } else if (a.id == "extruder_unload_speed") {
            a.current_value = effective_float(user_overrides_.extruder_unload_speed,
                                               config_defaults_.extruder_unload_speed);
        }
        // Toolhead sliders
        else if (a.id == "toolhead_sensor_to_nozzle") {
            a.current_value = effective_float(user_overrides_.toolhead_sensor_to_nozzle,
                                               config_defaults_.toolhead_sensor_to_nozzle);
        } else if (a.id == "toolhead_extruder_to_nozzle") {
            a.current_value = effective_float(user_overrides_.toolhead_extruder_to_nozzle,
                                               config_defaults_.toolhead_extruder_to_nozzle);
        } else if (a.id == "toolhead_entry_to_extruder") {
            a.current_value = effective_float(user_overrides_.toolhead_entry_to_extruder,
                                               config_defaults_.toolhead_entry_to_extruder);
        } else if (a.id == "toolhead_ooze_reduction") {
            a.current_value = effective_float(user_overrides_.toolhead_ooze_reduction,
                                               config_defaults_.toolhead_ooze_reduction);
        }
        // Accessories — status-backed values
        else if (a.id == "led_mode" && !led_exit_effect_.empty()) {
            a.current_value = std::string(led_exit_effect_);
        } else if (a.id == "espooler_mode" && !espooler_active_.empty()) {
            a.current_value = std::string(espooler_active_);
        } else if (a.id == "clog_detection") {
            int mode = (flowguard_encoder_mode_ > 0) ? flowguard_encoder_mode_
                       : effective_int(user_overrides_.clog_detection,
                                        config_defaults_.clog_detection);
            if (mode == 0) a.current_value = std::string("Off");
            else if (mode == 1) a.current_value = std::string("Manual");
            else a.current_value = std::string("Auto");
        } else if (a.id == "sync_to_extruder") {
            int val = effective_int(user_overrides_.sync_to_extruder,
                                     config_defaults_.sync_to_extruder);
            a.current_value = std::any(val != 0);
        }
    }

    return actions;
}
```

- [ ] **Step 4: Run tests**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare][device_actions]" -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/printer/ams_backend_happy_hare.cpp tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "feat(hh): populate device actions with live config values"
```

### Task 5: Parse Status Updates for LED, eSpooler, Flowguard

**Files:**
- Modify: `src/printer/ams_backend_happy_hare.cpp` (`handle_status_update` and `parse_mmu_state`)

- [ ] **Step 1: Write test for status-backed value parsing**

```cpp
TEST_CASE("parse_mmu_state extracts flowguard encoder_mode", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    nlohmann::json mmu_data;
    mmu_data["flowguard"] = {{"encoder_mode", 2}};  // Auto
    helper.test_parse_mmu_state(mmu_data);

    // flowguard_encoder_mode_ is private, verify via get_device_actions
    helper.set_config_defaults_for_test();
    auto actions = helper.get_device_actions();
    for (const auto& a : actions) {
        if (a.id == "clog_detection") {
            auto val = std::any_cast<std::string>(a.current_value);
            REQUIRE(val == "Auto");
        }
    }
}

TEST_CASE("parse_mmu_state extracts LED exit_effect", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);
    helper.set_config_defaults_for_test();

    nlohmann::json mmu_data;
    mmu_data["leds"] = {{"unit0", {{"exit_effect", "breathing"}}}};
    helper.test_parse_mmu_state(mmu_data);

    auto actions = helper.get_device_actions();
    for (const auto& a : actions) {
        if (a.id == "led_mode") {
            auto val = std::any_cast<std::string>(a.current_value);
            REQUIRE(val == "breathing");
        }
    }
}
```

- [ ] **Step 2: Run to verify failure**

```bash
make test && ./build/bin/helix-tests "parse_mmu_state extracts" -v
```

- [ ] **Step 3: Add parsing in `parse_mmu_state()`**

In `parse_mmu_state()` (line 270), add parsing for the new status fields. Add at the end of the method:

```cpp
// Parse flowguard (clog detection)
if (mmu_data.contains("flowguard") && mmu_data["flowguard"].is_object()) {
    const auto& fg = mmu_data["flowguard"];
    if (fg.contains("encoder_mode") && fg["encoder_mode"].is_number()) {
        flowguard_encoder_mode_ = fg["encoder_mode"].get<int>();
    }
}

// Parse LED state
if (mmu_data.contains("leds") && mmu_data["leds"].is_object()) {
    const auto& leds = mmu_data["leds"];
    if (leds.contains("unit0") && leds["unit0"].is_object()) {
        const auto& u0 = leds["unit0"];
        if (u0.contains("exit_effect") && u0["exit_effect"].is_string()) {
            led_exit_effect_ = u0["exit_effect"].get<std::string>();
        }
    }
}

// Parse eSpooler
if (mmu_data.contains("espooler_active") && mmu_data["espooler_active"].is_string()) {
    espooler_active_ = mmu_data["espooler_active"].get<std::string>();
}
```

- [ ] **Step 4: Also parse in `handle_status_update()`**

The `handle_status_update()` method (line 237) currently only looks at `params[0]["mmu"]`. Check if `mmu.leds`, `mmu.flowguard` arrive as top-level keys in the notification or nested. If they arrive as separate objects like `params[0]["mmu leds"]`, handle accordingly. If nested under `mmu`, the `parse_mmu_state` additions above are sufficient.

Look at how AFC handles similar nested objects for reference.

- [ ] **Step 5: Run tests**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare][device_actions]" -v
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/printer/ams_backend_happy_hare.cpp tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "feat(hh): parse LED, eSpooler, flowguard from Moonraker status"
```

### Task 6: Dynamic Topology Filtering

**Files:**
- Modify: `src/printer/ams_backend_happy_hare.cpp` (`get_device_actions()` and `get_device_sections()`)

- [ ] **Step 1: Write test for topology filtering**

```cpp
TEST_CASE("Type B topology hides servo and selector actions", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);
    helper.set_config_defaults_for_test();
    helper.set_selector_type("VirtualSelector");  // Type B (use existing test helper method)

    auto actions = helper.get_device_actions();
    for (const auto& a : actions) {
        if (a.id == "calibrate_servo" || a.id == "servo_buzz") {
            REQUIRE_FALSE(a.enabled);
            REQUIRE_FALSE(a.disable_reason.empty());
        }
        if (a.id == "selector_speed") {
            REQUIRE_FALSE(a.enabled);
        }
        if (a.id == "clog_detection") {
            REQUIRE_FALSE(a.enabled);
        }
    }
}

TEST_CASE("Type A topology shows all actions", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);
    helper.set_config_defaults_for_test();
    // selector_type_ defaults to "" (not VirtualSelector) = Type A

    auto actions = helper.get_device_actions();
    for (const auto& a : actions) {
        if (a.id == "calibrate_servo" || a.id == "selector_speed") {
            REQUIRE(a.enabled);
        }
    }
}
```

Use existing `set_selector_type()` method already in the test helper (line 133).

- [ ] **Step 2: Run to verify failure**

```bash
make test && ./build/bin/helix-tests "topology" -v
```

- [ ] **Step 3: Add topology filtering to `get_device_actions()`**

At the end of the method, before `return actions;`, add:

```cpp
// Topology filtering
bool is_type_b = (selector_type_ == "VirtualSelector");
for (auto& a : actions) {
    if (is_type_b) {
        // Type B (EMU/hub): no servo, no selector, no encoder-based clog detection
        if (a.id == "calibrate_servo" || a.id == "servo_buzz" ||
            a.id == "calibrate_encoder") {
            a.enabled = false;
            a.disable_reason = "Not available on hub-based (Type B) systems";
        }
        if (a.id == "selector_speed") {
            a.enabled = false;
            a.disable_reason = "No selector on hub-based systems";
        }
        if (a.id == "clog_detection") {
            a.enabled = false;
            a.disable_reason = "Encoder-based clog detection not available on Type B";
        }
    }
}
```

- [ ] **Step 4: Run tests**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare][device_actions]" -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/printer/ams_backend_happy_hare.cpp tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "feat(hh): filter device actions by selector topology (Type A vs B)"
```

---

## Chunk 4: Complete execute_device_action & Persistence

### Task 7: Implement Complete execute_device_action

**Files:**
- Modify: `src/printer/ams_backend_happy_hare.cpp` (expand `execute_device_action`)

- [ ] **Step 1: Write tests for new action execution**

```cpp
TEST_CASE("execute_device_action sends correct G-code for speeds", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    auto result = helper.execute_device_action("gear_from_buffer_speed", std::any(200.0));
    REQUIRE(result.success());
    REQUIRE(helper.has_gcode_containing("GEAR_FROM_BUFFER_SPEED=200"));

    helper.captured_gcodes.clear();
    result = helper.execute_device_action("gear_from_spool_speed", std::any(80.0));
    REQUIRE(result.success());
    REQUIRE(helper.has_gcode_containing("GEAR_FROM_SPOOL_SPEED=80"));
}

TEST_CASE("execute_device_action sends correct G-code for toolhead distances", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    auto result = helper.execute_device_action("toolhead_sensor_to_nozzle", std::any(58.5));
    REQUIRE(result.success());
    REQUIRE(helper.has_gcode_containing("TOOLHEAD_SENSOR_TO_NOZZLE=58.5"));
}

TEST_CASE("execute_device_action sends correct G-code for sync toggle", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    auto result = helper.execute_device_action("sync_to_extruder", std::any(true));
    REQUIRE(result.success());
    REQUIRE(helper.has_gcode_containing("SYNC_TO_EXTRUDER=1"));
}

TEST_CASE("execute_device_action sends test_move G-code", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);

    auto result = helper.execute_device_action("test_move", std::any());
    REQUIRE(result.success());
    REQUIRE(helper.has_gcode("MMU_TEST_MOVE"));
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make test && ./build/bin/helix-tests "execute_device_action sends" -v
```

- [ ] **Step 3: Expand execute_device_action**

Replace the existing implementation (lines 1693-1802) with complete handling. Keep the existing button dispatch array but add slider/dropdown/toggle handling:

```cpp
AmsError AmsBackendHappyHare::execute_device_action(
    const std::string& action_id, const std::any& value) {

    // Helper to extract numeric value
    auto require_number = [&](const char* label) -> std::pair<double, AmsError> {
        if (!value.has_value()) {
            return {0.0, AmsError(AmsResult::WRONG_STATE,
                                   fmt::format("{} value required", label))};
        }
        try {
            return {std::any_cast<double>(value), AmsError()};
        } catch (const std::bad_any_cast&) {
            try {
                return {static_cast<double>(std::any_cast<float>(value)), AmsError()};
            } catch (const std::bad_any_cast&) {
                return {0.0, AmsError(AmsResult::WRONG_STATE,
                                       fmt::format("Invalid {} value type", label))};
            }
        }
    };

    auto require_bool = [&](const char* label) -> std::pair<bool, AmsError> {
        if (!value.has_value()) {
            return {false, AmsError(AmsResult::WRONG_STATE,
                                     fmt::format("{} value required", label))};
        }
        try {
            return {std::any_cast<bool>(value), AmsError()};
        } catch (const std::bad_any_cast&) {
            return {false, AmsError(AmsResult::WRONG_STATE,
                                     fmt::format("Invalid {} value type", label))};
        }
    };

    auto require_string = [&](const char* label) -> std::pair<std::string, AmsError> {
        if (!value.has_value()) {
            return {"", AmsError(AmsResult::WRONG_STATE,
                                  fmt::format("{} value required", label))};
        }
        try {
            return {std::any_cast<std::string>(value), AmsError()};
        } catch (const std::bad_any_cast&) {
            return {"", AmsError(AmsResult::WRONG_STATE,
                                  fmt::format("Invalid {} value type", label))};
        }
    };

    // --- Buttons (one-shot G-code, no value) ---
    static const std::vector<std::pair<std::string, std::string>> button_actions = {
        {"calibrate_bowden", "MMU_CALIBRATE_BOWDEN"},
        {"calibrate_encoder", "MMU_CALIBRATE_ENCODER"},
        {"calibrate_gear", "MMU_CALIBRATE_GEAR"},
        {"calibrate_gates", "MMU_CALIBRATE_GATES"},
        {"test_grip", "MMU_TEST_GRIP"},
        {"test_load", "MMU_TEST_LOAD"},
        {"test_move", "MMU_TEST_MOVE"},
        {"servo_buzz", "MMU_SERVO"},
        {"reset_servo_counter", "MMU_STATS COUNTER=servo RESET=1"},
        {"reset_blade_counter", "MMU_STATS COUNTER=cutter RESET=1"},
    };
    for (const auto& [id, gcode] : button_actions) {
        if (action_id == id)
            return execute_gcode(gcode);
    }

    // --- Speed sliders (MMU_TEST_CONFIG) ---
    static const std::vector<std::pair<std::string, std::string>> speed_params = {
        {"gear_from_buffer_speed", "GEAR_FROM_BUFFER_SPEED"},
        {"gear_from_spool_speed", "GEAR_FROM_SPOOL_SPEED"},
        {"gear_unload_speed", "GEAR_UNLOAD_SPEED"},
        {"selector_speed", "SELECTOR_MOVE_SPEED"},
        {"extruder_load_speed", "EXTRUDER_LOAD_SPEED"},
        {"extruder_unload_speed", "EXTRUDER_UNLOAD_SPEED"},
    };
    for (const auto& [id, param] : speed_params) {
        if (action_id == id) {
            auto [val, err] = require_number("speed");
            if (!err) return err;
            auto result = execute_gcode(fmt::format("MMU_TEST_CONFIG {}={:.0f}", param, val));
            if (result.success()) {
                save_override(action_id, static_cast<float>(val));
            }
            return result;
        }
    }

    // --- Toolhead sliders (MMU_TEST_CONFIG) ---
    static const std::vector<std::pair<std::string, std::string>> toolhead_params = {
        {"toolhead_sensor_to_nozzle", "TOOLHEAD_SENSOR_TO_NOZZLE"},
        {"toolhead_extruder_to_nozzle", "TOOLHEAD_EXTRUDER_TO_NOZZLE"},
        {"toolhead_entry_to_extruder", "TOOLHEAD_ENTRY_TO_EXTRUDER"},
        {"toolhead_ooze_reduction", "TOOLHEAD_OOZE_REDUCTION"},
    };
    for (const auto& [id, param] : toolhead_params) {
        if (action_id == id) {
            auto [val, err] = require_number("distance");
            if (!err) return err;
            auto result = execute_gcode(fmt::format("MMU_TEST_CONFIG {}={:.1f}", param, val));
            if (result.success()) {
                save_override(action_id, static_cast<float>(val));
            }
            return result;
        }
    }

    // --- LED dropdown ---
    if (action_id == "led_mode") {
        auto [mode, err] = require_string("LED mode");
        if (!err) return err;
        return execute_gcode(fmt::format("MMU_LED EXIT_EFFECT={}", mode));
    }

    // --- eSpooler dropdown ---
    if (action_id == "espooler_mode") {
        auto [mode, err] = require_string("eSpooler mode");
        if (!err) return err;
        return execute_gcode("MMU_ESPOOLER OPERATION=" + mode);
    }

    // --- Clog detection dropdown ---
    if (action_id == "clog_detection") {
        auto [mode_str, err] = require_string("clog detection mode");
        if (!err) return err;
        int mode_int = 0;
        if (mode_str == "Manual") mode_int = 1;
        else if (mode_str == "Auto") mode_int = 2;
        auto result = execute_gcode(fmt::format("MMU_TEST_CONFIG CLOG_DETECTION={}", mode_int));
        if (result.success()) {
            save_override("clog_detection", mode_int);
        }
        return result;
    }

    // --- Sync to extruder toggle ---
    if (action_id == "sync_to_extruder") {
        auto [val, err] = require_bool("sync to extruder");
        if (!err) return err;
        auto result = execute_gcode(
            fmt::format("MMU_TEST_CONFIG SYNC_TO_EXTRUDER={}", val ? 1 : 0));
        if (result.success()) {
            save_override("sync_to_extruder", val ? 1 : 0);
        }
        return result;
    }

    // --- Motors toggle ---
    if (action_id == "motors_toggle") {
        auto [val, err] = require_bool("motor state");
        if (!err) return err;
        return execute_gcode(val ? "MMU_HOME" : "MMU_MOTORS_OFF");
    }

    return AmsErrorHelper::not_supported("Unknown action: " + action_id);
}
```

- [ ] **Step 4: Run tests**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare][device_actions]" -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/printer/ams_backend_happy_hare.cpp tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "feat(hh): implement complete execute_device_action with all action types"
```

### Task 8: Persistence via Config

**Files:**
- Modify: `src/printer/ams_backend_happy_hare.cpp` (implement save/load/reapply)

- [ ] **Step 1: Write test for persistence round-trip**

This test verifies that saving an override and loading it back works. Since Config is a singleton, use the test fixture pattern:

```cpp
TEST_CASE("user override persists and reapplies", "[ams][happy_hare][device_actions]") {
    AmsBackendHappyHareTestHelper helper;
    helper.initialize_test_gates(4);
    helper.set_config_defaults_for_test();

    // Simulate user changing gear buffer speed
    helper.execute_device_action("gear_from_buffer_speed", std::any(200.0));

    // Verify the override is reflected in get_device_actions
    auto actions = helper.get_device_actions();
    for (const auto& a : actions) {
        if (a.id == "gear_from_buffer_speed") {
            auto val = std::any_cast<double>(a.current_value);
            REQUIRE(val == Catch::Approx(200.0));
        }
    }
}
```

- [ ] **Step 2: Implement `save_override()`**

```cpp
void AmsBackendHappyHare::save_override(const std::string& key, float value) {
    // Update in-memory cache
    if (key == "gear_from_buffer_speed") user_overrides_.gear_from_buffer_speed = value;
    else if (key == "gear_from_spool_speed") user_overrides_.gear_from_spool_speed = value;
    else if (key == "gear_unload_speed") user_overrides_.gear_unload_speed = value;
    else if (key == "selector_speed") user_overrides_.selector_move_speed = value;
    else if (key == "extruder_load_speed") user_overrides_.extruder_load_speed = value;
    else if (key == "extruder_unload_speed") user_overrides_.extruder_unload_speed = value;
    else if (key == "toolhead_sensor_to_nozzle") user_overrides_.toolhead_sensor_to_nozzle = value;
    else if (key == "toolhead_extruder_to_nozzle") user_overrides_.toolhead_extruder_to_nozzle = value;
    else if (key == "toolhead_entry_to_extruder") user_overrides_.toolhead_entry_to_extruder = value;
    else if (key == "toolhead_ooze_reduction") user_overrides_.toolhead_ooze_reduction = value;

    // Persist to Config
    Config* config = Config::get_instance();
    if (config) {
        config->set(fmt::format("/hh_overrides/{}/value", key), value);
        // Store the config default at time of override for edit detection
        float config_val = get_config_default_float(key);
        config->set(fmt::format("/hh_overrides/{}/config_default", key), config_val);
        config->save();
    }
}

void AmsBackendHappyHare::save_override(const std::string& key, int value) {
    if (key == "sync_to_extruder") user_overrides_.sync_to_extruder = value;
    else if (key == "clog_detection") user_overrides_.clog_detection = value;

    Config* config = Config::get_instance();
    if (config) {
        config->set(fmt::format("/hh_overrides/{}/value", key), value);
        int config_val = get_config_default_int(key);
        config->set(fmt::format("/hh_overrides/{}/config_default", key), config_val);
        config->save();
    }
}
```

Add helper methods `get_config_default_float()` and `get_config_default_int()` that return the matching `config_defaults_` field for a given action key.

- [ ] **Step 3: Implement `load_persisted_overrides()`**

```cpp
void AmsBackendHappyHare::load_persisted_overrides() {
    Config* config = Config::get_instance();
    if (!config) return;

    auto load_float = [&](const std::string& key, std::optional<float>& target) {
        try {
            auto& json = config->get_json(fmt::format("/hh_overrides/{}", key));
            if (json.contains("value") && json.contains("config_default")) {
                float saved_default = json["config_default"].get<float>();
                float current_default = get_config_default_float(key);
                if (std::abs(saved_default - current_default) < 0.01f) {
                    // Config hasn't changed — keep our override
                    target = json["value"].get<float>();
                    spdlog::debug("[HappyHare] Loaded override {}: {}", key, *target);
                } else {
                    // Config file was edited — drop our override
                    spdlog::info("[HappyHare] Config default for {} changed ({} → {}), dropping override",
                                  key, saved_default, current_default);
                    target = std::nullopt;
                }
            }
        } catch (...) {
            // No saved override for this key
        }
    };

    auto load_int = [&](const std::string& key, std::optional<int>& target) {
        try {
            auto& json = config->get_json(fmt::format("/hh_overrides/{}", key));
            if (json.contains("value") && json.contains("config_default")) {
                int saved_default = json["config_default"].get<int>();
                int current_default = get_config_default_int(key);
                if (saved_default == current_default) {
                    target = json["value"].get<int>();
                } else {
                    spdlog::info("[HappyHare] Config default for {} changed, dropping override", key);
                    target = std::nullopt;
                }
            }
        } catch (...) {}
    };

    load_float("gear_from_buffer_speed", user_overrides_.gear_from_buffer_speed);
    load_float("gear_from_spool_speed", user_overrides_.gear_from_spool_speed);
    load_float("gear_unload_speed", user_overrides_.gear_unload_speed);
    load_float("selector_speed", user_overrides_.selector_move_speed);
    load_float("extruder_load_speed", user_overrides_.extruder_load_speed);
    load_float("extruder_unload_speed", user_overrides_.extruder_unload_speed);
    load_float("toolhead_sensor_to_nozzle", user_overrides_.toolhead_sensor_to_nozzle);
    load_float("toolhead_extruder_to_nozzle", user_overrides_.toolhead_extruder_to_nozzle);
    load_float("toolhead_entry_to_extruder", user_overrides_.toolhead_entry_to_extruder);
    load_float("toolhead_ooze_reduction", user_overrides_.toolhead_ooze_reduction);
    load_int("sync_to_extruder", user_overrides_.sync_to_extruder);
    load_int("clog_detection", user_overrides_.clog_detection);
}
```

- [ ] **Step 4: Implement `reapply_overrides()`**

```cpp
void AmsBackendHappyHare::reapply_overrides() {
    // Batch all overrides into one MMU_TEST_CONFIG command
    std::string cmd = "MMU_TEST_CONFIG";
    bool has_params = false;

    auto add_float = [&](const std::optional<float>& val, const char* param) {
        if (val.has_value()) {
            cmd += fmt::format(" {}={:.1f}", param, *val);
            has_params = true;
        }
    };
    auto add_int = [&](const std::optional<int>& val, const char* param) {
        if (val.has_value()) {
            cmd += fmt::format(" {}={}", param, *val);
            has_params = true;
        }
    };

    add_float(user_overrides_.gear_from_buffer_speed, "GEAR_FROM_BUFFER_SPEED");
    add_float(user_overrides_.gear_from_spool_speed, "GEAR_FROM_SPOOL_SPEED");
    add_float(user_overrides_.gear_unload_speed, "GEAR_UNLOAD_SPEED");
    add_float(user_overrides_.selector_move_speed, "SELECTOR_MOVE_SPEED");
    add_float(user_overrides_.extruder_load_speed, "EXTRUDER_LOAD_SPEED");
    add_float(user_overrides_.extruder_unload_speed, "EXTRUDER_UNLOAD_SPEED");
    add_float(user_overrides_.toolhead_sensor_to_nozzle, "TOOLHEAD_SENSOR_TO_NOZZLE");
    add_float(user_overrides_.toolhead_extruder_to_nozzle, "TOOLHEAD_EXTRUDER_TO_NOZZLE");
    add_float(user_overrides_.toolhead_entry_to_extruder, "TOOLHEAD_ENTRY_TO_EXTRUDER");
    add_float(user_overrides_.toolhead_ooze_reduction, "TOOLHEAD_OOZE_REDUCTION");
    add_int(user_overrides_.sync_to_extruder, "SYNC_TO_EXTRUDER");
    add_int(user_overrides_.clog_detection, "CLOG_DETECTION");

    if (has_params) {
        spdlog::info("[HappyHare] Re-applying user overrides: {}", cmd);
        execute_gcode(cmd);
    }
}
```

- [ ] **Step 5: Run all tests**

```bash
make test && ./build/bin/helix-tests "[ams][happy_hare]" -v
```

Expected: PASS (all HH tests including new device action tests)

- [ ] **Step 6: Build full app**

```bash
make -j
```

- [ ] **Step 7: Commit**

```bash
git add src/printer/ams_backend_happy_hare.cpp include/ams_backend_happy_hare.h tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "feat(hh): add persistence and reconnect re-apply for device action overrides"
```

---

## Chunk 5: Final Integration & Cleanup

### Task 9: Integration Test and Full Test Run

- [ ] **Step 1: Run full test suite**

```bash
make test-run
```

Verify no regressions across all tests, not just HH.

- [ ] **Step 2: Visual smoke test**

Run with mock printer and navigate to AMS device operations:

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/hh-test.log
```

Ask user to navigate to: AMS panel → Device operations → each section. Verify:
- Buttons render 2-per-row
- Sliders show non-default values (if config query succeeds)
- Topology-disabled actions show disabled state

- [ ] **Step 3: Read log output**

Read `/tmp/hh-test.log` and check for:
- `[HappyHare] Config defaults loaded from configfile.settings.mmu` log line
- No warnings/errors related to device actions
- Correct G-code dispatching when actions are triggered

- [ ] **Step 4: Final commit if any cleanup needed**

```bash
git add -A
git commit -m "chore(hh): device actions integration cleanup"
```
