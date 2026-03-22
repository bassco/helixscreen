# Multi-Instance Widget System + Power Device Widget

**Date:** 2026-03-22
**Issues:** #342 (Multiple Widget Instances), #467 (Flexible Power Button)

## Overview

Replace the hardcoded favorite_macro_1..5 multi-instance pattern with a proper dynamic multi-instance widget system. Then build a new power device widget on top of it, with WebSocket-driven live state.

The system is designed so any widget can opt into multi-instance support by setting a single flag, enabling future use cases like multiple thermistor widgets (#342).

## Architecture

### Multi-Instance Widget Infrastructure

#### PanelWidgetDef changes

```cpp
struct PanelWidgetDef {
    // ... existing fields ...
    bool multi_instance = false;     // NEW: allows dynamic instance creation
    // const char* catalog_group;    // REMOVED: replaced by multi_instance
};
```

When `multi_instance = true`:
- The def's `id` is the **base ID** (e.g., `"power_device"`, `"favorite_macro"`)
- Instances get IDs like `"favorite_macro:1"`, `"power_device:2"`
- The factory receives the full instance ID so it can store it
- The grid is the only constraint on instance count — no artificial limits
- Multi-instance defs MUST have a factory (component name resolution goes through the widget instance's `get_component_name()`)
- `build_default_grid()` must skip `multi_instance` defs — they should never appear as bare base IDs in config entries. Both current multi-instance types have `default_enabled=false`, but this invariant should be enforced with a guard.
- `default_layout.json` anchors should only reference single-instance widget IDs

#### WidgetFactory signature change

```cpp
// BEFORE
using WidgetFactory = std::function<std::unique_ptr<PanelWidget>()>;

// AFTER
using WidgetFactory = std::function<std::unique_ptr<PanelWidget>(const std::string& instance_id)>;
```

All existing single-instance factories receive the instance ID but ignore it. Multi-instance factories pass it to their constructor.

#### find_widget_def() — delimiter-aware lookup

```cpp
const PanelWidgetDef* find_widget_def(std::string_view id) {
    // Try exact match first (handles all single-instance widgets)
    auto it = std::find_if(defs.begin(), defs.end(),
                           [&](const PanelWidgetDef& d) { return id == d.id; });
    if (it != defs.end()) return &*it;

    // Multi-instance: strip ":N" suffix and retry
    auto colon = id.rfind(':');
    if (colon != std::string_view::npos) {
        auto base = id.substr(0, colon);
        it = std::find_if(defs.begin(), defs.end(),
                           [&](const PanelWidgetDef& d) {
                               return base == d.id && d.multi_instance;
                           });
        if (it != defs.end()) return &*it;
    }
    return nullptr;
}
```

#### Instance ID minting — PanelWidgetConfig

```cpp
class PanelWidgetConfig {
    // NEW: generate next instance ID for a multi-instance widget type
    std::string mint_instance_id(const std::string& base_id);
};
```

Implementation: scan existing entries for `base_id:*`, find the highest N, return `base_id:(N+1)`. If no instances exist, returns `base_id:1`.

#### Widget catalog changes

`WidgetCatalogOverlay::populate_rows()` currently has two code paths: grouped (`catalog_group`) and ungrouped. Replace the grouped path:

- For `multi_instance` defs: show one row, display "N Placed" count, click mints a new ID then immediately passes it by value to the `on_select` callback (no pointer storage — the minted `std::string` is consumed in the same click handler)
- For single-instance defs: existing behavior unchanged
- Remove all `catalog_group` logic

#### Widget removal behavior

When a multi-instance widget is removed from the grid in edit mode, its config entry should be **deleted entirely** (not just disabled). This keeps the "N Placed" count accurate and avoids accumulating stale disabled entries. Single-instance widgets continue to use the enable/disable pattern.

#### PanelWidgetManager changes

In `populate_widgets()`, the factory call changes from:

```cpp
slot.instance = def->factory();
```

to:

```cpp
slot.instance = def->factory(entry.id);
```

The entry ID is the full instance ID (e.g., `"power_device:3"`). Everything else in the manager works with arbitrary string IDs already.

### Favorite Macro Migration

#### Registry

**Before:** 5 hardcoded `PanelWidgetDef` entries with `catalog_group = "favorite_macro"`, generated in `init_widget_registrations()` from `kFavMacroIds[]`.

**After:** One entry in `s_widget_defs`:

```cpp
{"favorite_macro", "Macro Button", "play",
 "Run a configured macro with one tap", "Macro Button",
 nullptr, nullptr,
 false, 1, 1, 1, 1, 2, 1, .multi_instance = true}
```

Remove `kFavMacroIds[]`, `kMaxFavoriteMacroSlots`, and the insertion loop.

#### FavoriteMacroWidget

- Constructor takes `std::string instance_id` instead of hardcoded ID
- `id()` returns `instance_id_.c_str()` (stored member)
- `get_component_name()` returns `"panel_widget_favorite_macro"` (same XML for all instances — already works this way)
- Single factory registration: `register_widget_factory("favorite_macro", [](const std::string& id) { ... })`

#### Config migration

In `PanelWidgetConfig::load()`, after loading entries from JSON:

```
for each entry:
    if id starts with "favorite_macro_" followed by one or more digits (and nothing else):
        replace the '_' immediately before the trailing digit sequence with ':'
        (e.g., favorite_macro_1 → favorite_macro:1, favorite_macro_10 → favorite_macro:10)
        mark dirty
if dirty: save()
```

One-time, backward-compatible. Old configs get silently upgraded on first load.

### Power Device State (WebSocket-Driven)

#### New singleton: PowerDeviceState

```cpp
class PowerDeviceState {
public:
    static PowerDeviceState& instance();

    // Called during connection setup
    void subscribe(MoonrakerAPI& api);
    void unsubscribe(MoonrakerAPI& api);

    // Called after initial device discovery (HTTP fetch)
    void set_devices(const std::vector<PowerDevice>& devices);

    // Per-device LVGL subjects (dynamic — requires SubjectLifetime)
    // Values: 0=off, 1=on, 2=locked
    lv_subject_t* get_status_subject(const std::string& device, SubjectLifetime& lt);

    // Query device metadata
    bool is_locked_while_printing(const std::string& device) const;
    std::vector<std::string> device_names() const;

    // Shutdown cleanup (registered with StaticSubjectRegistry)
    void deinit_subjects();

private:
    void on_power_changed(const nlohmann::json& msg);
    void reevaluate_lock_states();  // called when print state changes

    struct DeviceInfo {
        std::string name;
        std::string type;
        bool locked_while_printing = false;
        std::unique_ptr<lv_subject_t> status_subject;  // heap-allocated for pointer stability
    };
    std::unordered_map<std::string, DeviceInfo> devices_;
    ObserverGuard print_state_observer_;  // observes print state for lock transitions
    bool subjects_initialized_ = false;
};
```

**Why a singleton:** Multiple power device widgets may reference the same device. The singleton holds canonical state; widgets observe it. PowerPanel also benefits from live data.

**Dynamic subjects with SubjectLifetime:** Per-device subjects are dynamic (created/destroyed on reconnection/rediscovery). Widgets must use the `SubjectLifetime` pattern to avoid use-after-free crashes.

**Pointer stability:** `status_subject` is stored as `std::unique_ptr<lv_subject_t>` (heap-allocated) so that pointers remain stable across `unordered_map` rehashes. LVGL subjects contain internal linked lists of observers; moving them in memory would corrupt those lists.

**StaticSubjectRegistry:** `set_devices()` must register cleanup with `StaticSubjectRegistry::instance().register_deinit("PowerDeviceState", ...)` on first call. `deinit_subjects()` calls `lv_subject_deinit()` on all device subjects and clears the map. This prevents shutdown crashes from observer removal on freed subjects during `lv_deinit()`.

**Moonraker notification:** `notify_power_changed` payload:

```json
{"method": "notify_power_changed", "params": [{"device": "chamber_light", "status": "on", "type": "klipper_device"}]}
```

Handler registered via `api.register_method_callback("notify_power_changed", "PowerDeviceState", ...)`. Callback uses `ui_queue_update()` to marshal to UI thread, then updates the per-device subject.

**Lock state (value 2):** Moonraker's `notify_power_changed` only sends `"status": "on"|"off"`. The locked state is a composite: `locked_while_printing == true` AND a print is active. `PowerDeviceState` observes the print state subject. When print state transitions to/from printing, `reevaluate_lock_states()` checks all devices with `locked_while_printing=true` and updates their status subjects to 2 (locked) or back to their actual on/off value.

**`unsubscribe()` implementation:** Calls `api.unregister_method_callback("notify_power_changed", "PowerDeviceState")`, clears the print state observer, and calls `deinit_subjects()`.

**Integration points:**
- `subscribe()` called in `Application` during connection setup
- `unsubscribe()` called during teardown
- `set_devices()` called from `moonraker_discovery_sequence.cpp` after initial HTTP fetch

### Power Device Widget

#### New files

- `include/power_device_widget.h`
- `src/ui/widgets/power_device_widget.cpp`
- `ui_xml/components/panel_widget_power_device.xml`

#### Registration

```cpp
{"power_device", "Power Device", "power_cycle",
 "Toggle a Moonraker power device", "Power Device",
 "power_device_count", "Requires Moonraker power device",
 false, 1, 1, 1, 1, 1, 1, .multi_instance = true}
```

Coexists with the existing `"power"` widget (the panel launcher). Users can use either or both.

#### Per-instance config

```json
{"device": "chamber_light"}
```

#### PowerDeviceWidget class

```cpp
class PowerDeviceWidget : public PanelWidget {
    std::string instance_id_;
    std::string device_name_;
    SubjectLifetime subject_lifetime_;
    ObserverGuard status_observer_;
    lv_obj_t* badge_circle_ = nullptr;
    lv_obj_t* icon_obj_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* lock_icon_ = nullptr;
    MoonrakerAPI* api_;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void set_config(const nlohmann::json& config) override;
    bool has_edit_configure() const override { return true; }
    bool on_edit_configure() override;

    void handle_toggle();
    void update_display(int status);  // 0=off, 1=on, 2=locked

    // Registered via lv_xml_register_event_cb() — matches XML callback name
    static void power_device_clicked_cb(lv_event_t* e);
};
```

During factory registration, register the XML event callback: `lv_xml_register_event_cb(nullptr, "power_device_clicked_cb", PowerDeviceWidget::power_device_clicked_cb)`.

On `attach()`: if `device_name_` is set, get the status subject from `PowerDeviceState` with lifetime token, observe it — `update_display()` fires reactively. Lock state is handled by `PowerDeviceState` (which observes print state and updates subjects to value 2 when locked).

On `detach()`: clear observers. Lifetime token handles subject invalidation safety.

#### XML component

```xml
<component>
  <view name="panel_widget_power_device" extends="lv_obj"
        height="100%" flex_grow="1" style_pad_all="0" scrollable="false"
        flex_flow="column" style_flex_main_place="center"
        style_flex_cross_place="center" style_flex_track_place="center"
        clickable="true">
    <bind_state_if_not_eq subject="printer_connection_state"
                          state="disabled" ref_value="2"/>
    <event_cb trigger="clicked" callback="power_device_clicked_cb"/>

    <!-- Circular badge background -->
    <lv_obj name="power_badge" width="48" height="48"
            style_radius="24" style_bg_opa="40"
            style_bg_color="#danger" style_border_width="0"
            clickable="false" scrollable="false"
            style_flex_main_place="center" style_flex_cross_place="center">
      <icon name="power_icon" src="power_cycle" size="md"
            variant="danger" clickable="false" event_bubble="true"/>
    </lv_obj>

    <!-- Lock icon (hidden by default) -->
    <icon name="power_lock_icon" src="lock" size="xs" variant="muted"
          clickable="false" event_bubble="true" hidden="true"/>

    <!-- Device name -->
    <text_tiny name="power_device_name" text="Configure"
               translation_tag="Configure"
               style_text_align="center" style_text_color="#text"
               long_mode="wrap" width="100%"
               clickable="false" event_bubble="true"/>

    <!-- ON/OFF/LOCKED status -->
    <text_tiny name="power_device_status" text=""
               style_text_align="center" style_text_color="#danger"
               clickable="false" event_bubble="true"/>
  </view>
</component>
```

#### Visual states

| State | Badge bg | Icon variant | Status text | Status color | Lock icon |
|-------|----------|-------------|-------------|-------------|-----------|
| ON | `danger` @ 40% opa | `danger` | "ON" | `danger` | hidden |
| OFF | `muted` @ 20% opa | `muted` | "OFF" | `text_muted` | hidden |
| LOCKED | `muted` @ 20% opa | `muted` | "LOCKED" | `text_muted` | visible |
| Unconfigured | `secondary` @ 20% opa | `secondary` | "" | — | hidden |

#### Device picker

When the user taps an unconfigured widget or uses the gear button in edit mode, an overlay shows available Moonraker power devices (fetched from `PowerDeviceState::device_names()`). Follows the same pattern as the macro picker. Devices already assigned to other power_device widget instances are shown but dimmed (not hidden — user might intentionally want duplicate buttons).

## File Change Summary

### New files

| File | Purpose |
|------|---------|
| `include/power_device_state.h` | PowerDeviceState singleton |
| `src/printer/power_device_state.cpp` | Implementation + notify_power_changed handler |
| `include/power_device_widget.h` | PowerDeviceWidget class |
| `src/ui/widgets/power_device_widget.cpp` | Implementation + device picker |
| `ui_xml/components/panel_widget_power_device.xml` | Widget XML layout |

### Modified files

| File | Change |
|------|--------|
| `include/panel_widget_registry.h` | Add `multi_instance` to `PanelWidgetDef`, remove `catalog_group`. Change `WidgetFactory` signature |
| `src/ui/panel_widget_registry.cpp` | Remove `kFavMacroIds[]`, slot generation loop. Add `power_device` def. Single `favorite_macro` def with `multi_instance=true` |
| `include/panel_widget_config.h` | Add `mint_instance_id()` |
| `src/system/panel_widget_config.cpp` | Implement `mint_instance_id()`, add `favorite_macro_N` → `favorite_macro:N` migration in `load()` |
| `src/ui/ui_widget_catalog_overlay.cpp` | Replace `catalog_group` logic with `multi_instance` handling |
| `src/ui/panel_widget_manager.cpp` | Pass instance ID to factory calls |
| `include/favorite_macro_widget.h` | Constructor takes `std::string` instance ID |
| `src/ui/widgets/favorite_macro_widget.cpp` | Store instance ID, single factory registration |
| `src/application/application.cpp` | Wire `PowerDeviceState::subscribe()` / `unsubscribe()` |
| `src/api/moonraker_discovery_sequence.cpp` | Call `PowerDeviceState::set_devices()` |
| `config/default_layout.json` | Update `favorite_macro_1` references if present |

### Files needing verification/minor updates

| File | Why |
|------|-----|
| `src/ui/grid_edit_mode.cpp` | Verify dynamic instance IDs work (likely already fine) |
| `src/api/moonraker_client_mock.cpp` | Mock `notify_power_changed` for `--test` mode |
| `src/api/moonraker_api_mock.cpp` | Update mock power device behavior |
| Tests | New tests for multi-instance config, migration, power device state |

## Implementation Phases

1. **Multi-instance infrastructure** — `PanelWidgetDef`, `WidgetFactory` signature, `find_widget_def()`, `mint_instance_id()`, catalog overlay
2. **Favorite macro migration** — convert to new system, config migration, remove hardcoded slots
3. **PowerDeviceState** — singleton, WebSocket subscription, per-device subjects
4. **Power device widget** — widget class, XML component, device picker, registration
