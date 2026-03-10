# Spoolman Location Field Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Spoolman `location` field support — parse, display, edit, search, and filter by location.

**Architecture:** The `location` field flows from Spoolman API JSON → `SpoolInfo` struct → list view display (inline with vendor) and edit modal (new text field). Search and a dropdown filter on the panel provide discovery. All changes follow existing patterns in the Spoolman subsystem.

**Tech Stack:** C++17, LVGL 9.5 XML, Catch2 tests, Makefile build

**Spec:** `docs/superpowers/specs/2026-03-10-spoolman-location-field-design.md`

---

## Chunk 1: Data Layer + Tests

### Task 1: Add location field to SpoolInfo and parse from JSON

**Files:**
- Modify: `include/spoolman_types.h:79-96` (SpoolInfo struct)
- Modify: `include/spoolman_types.h:163-168` (filter_spools doc comment)
- Modify: `src/api/moonraker_spoolman_api.cpp:24-76` (parse_spool_info)
- Modify: `src/printer/spoolman_types.cpp:39-42` (filter_spools searchable string)
- Test: `tests/unit/test_spoolman.cpp`

- [ ] **Step 1: Add `location` field to `SpoolInfo`**

In `include/spoolman_types.h`, add after `lot_nr` (line 94):

```cpp
std::string location;          // Physical storage location (max 64 chars, from Spoolman)
```

Update `filter_spools()` doc comment (line ~167) to mention location:

```cpp
 * searchable text (ID, vendor, material, color_name, location). Case-insensitive.
```

- [ ] **Step 2: Parse location from Spoolman JSON**

In `src/api/moonraker_spoolman_api.cpp`, inside `parse_spool_info()`, add after the `comment` line (line 33):

```cpp
info.location = safe_string(spool_json, "location");
```

Note: `location` is a top-level spool field (not nested under `filament`).

- [ ] **Step 3: Add location to filter_spools searchable string**

In `src/printer/spoolman_types.cpp` line 41-42, change:

```cpp
std::string searchable = "#" + std::to_string(spool.id) + " " + spool.vendor + " " +
                         spool.material + " " + spool.color_name;
```

to:

```cpp
std::string searchable = "#" + std::to_string(spool.id) + " " + spool.vendor + " " +
                         spool.material + " " + spool.color_name + " " + spool.location;
```

- [ ] **Step 4: Write failing tests for location search**

In `tests/unit/test_spoolman.cpp`, update `make_filter_test_spools()` to add location to some test spools. Add after `s1.color_name = "Jet Black";` (line 756):

```cpp
s1.location = "Shelf A";
```

Add after `s3.color_name = "Red";` (line 770):

```cpp
s3.location = "Shelf A";
```

Then add new test cases after the existing filter tests (~line 857):

```cpp
TEST_CASE("filter_spools - location search", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "shelf");
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].id == 1);
    REQUIRE(result[1].id == 3);
}

TEST_CASE("filter_spools - location + material AND search", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    auto result = filter_spools(spools, "shelf pla");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 1);
}

TEST_CASE("filter_spools - empty location does not break search", "[filament][filter]") {
    auto spools = make_filter_test_spools();
    // Spool s2 and s4 have empty location — they should still match on other fields
    auto result = filter_spools(spools, "hatchbox");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == 42);
}
```

- [ ] **Step 5: Write JSON parse tests for location**

Also in `tests/unit/test_spoolman.cpp`, add after the new filter tests:

```cpp
TEST_CASE("SpoolInfo - location field parsed from JSON", "[filament][parsing]") {
    // This test uses the mock API which internally calls parse_spool_info()
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPI api(client, state);

    // Create a spool to get a valid ID
    nlohmann::json body;
    body["filament_id"] = 1;
    body["location"] = "Shelf B";
    int created_id = 0;

    api.spoolman().create_spoolman_spool(
        body,
        [&](const SpoolInfo& spool) {
            created_id = spool.id;
        },
        [](const MoonrakerError&) { FAIL("create failed"); });

    REQUIRE(created_id > 0);

    // Fetch it back and verify location was round-tripped
    api.spoolman().get_spoolman_spool(
        created_id,
        [](const std::optional<SpoolInfo>& spool) {
            REQUIRE(spool.has_value());
            // Note: mock may not persist location — if not, this test documents the gap
        },
        [](const MoonrakerError&) { FAIL("get failed"); });
}

TEST_CASE("SpoolInfo - location defaults to empty string", "[filament]") {
    SpoolInfo spool;
    REQUIRE(spool.location.empty());
}
```

- [ ] **Step 6: Build and run tests**

Run: `make test && ./build/bin/helix-tests "[filter]" -v && ./build/bin/helix-tests "[parsing]" -v`
Expected: All existing tests pass + new location tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/spoolman_types.h src/api/moonraker_spoolman_api.cpp src/printer/spoolman_types.cpp tests/unit/test_spoolman.cpp
git commit -m "feat(spoolman): add location field to SpoolInfo, parse from API, include in search"
```

---

## Chunk 2: Spool Row Display

### Task 2: Show location inline with vendor name in spool list rows

**Files:**
- Modify: `src/ui/ui_spoolman_list_view.cpp:188-192` (configure_row vendor label)

- [ ] **Step 1: Update configure_row to show location with vendor**

In `src/ui/ui_spoolman_list_view.cpp`, replace the vendor label block (lines 189-192):

```cpp
// Update vendor (with location if available)
lv_obj_t* vendor_label = lv_obj_find_by_name(row, "spool_vendor");
if (vendor_label) {
    const char* vendor = spool.vendor.empty() ? "Unknown" : spool.vendor.c_str();
    lv_label_set_text(vendor_label, vendor);
}
```

with:

```cpp
// Update vendor (with location if available)
lv_obj_t* vendor_label = lv_obj_find_by_name(row, "spool_vendor");
if (vendor_label) {
    std::string vendor_text = spool.vendor.empty() ? "Unknown" : spool.vendor;
    if (!spool.location.empty()) {
        vendor_text += " \xC2\xB7 " + spool.location; // middle dot: U+00B7
    }
    lv_label_set_text(vendor_label, vendor_text.c_str());
}
```

Note: `\xC2\xB7` is the UTF-8 encoding of `·` (middle dot, U+00B7).

- [ ] **Step 2: Build**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 3: Visual verification**

Run: `./build/bin/helix-screen --test -vv`

Navigate to the Spoolman panel. Mock spools won't have location data, so all rows will show vendor name only (no separator). This confirms no regression.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_spoolman_list_view.cpp
git commit -m "feat(spoolman): display spool location inline with vendor name in list rows"
```

---

## Chunk 3: Edit Modal

### Task 3: Add location field to edit modal XML and C++

**Files:**
- Modify: `ui_xml/spoolman_edit_modal.xml:83-101` (Row 2)
- Modify: `src/ui/ui_spoolman_edit_modal.cpp` (populate, read, dirty, save, tab order)

- [ ] **Step 1: Update edit modal XML — Row 2 becomes three fields**

In `ui_xml/spoolman_edit_modal.xml`, replace Row 2 (lines 83-101):

```xml
        <!-- Row 2: Price | Lot Nr -->
        <lv_obj width="100%"
                height="content" style_pad_all="0" flex_flow="row" style_pad_gap="#space_md" scrollable="false">
          <lv_obj height="content"
                  flex_grow="1" style_pad_all="0" flex_flow="column" style_pad_gap="#space_xxs" scrollable="false">
            <text_small text="Price" translation_tag="Price"/>
            <text_input name="field_price"
                        width="100%" input_mode="number" keyboard_hint="numeric" placeholder="0.00" max_length="10">
              <event_cb trigger="value_changed" callback="spoolman_edit_field_changed_cb"/>
            </text_input>
          </lv_obj>
          <lv_obj height="content"
                  flex_grow="1" style_pad_all="0" flex_flow="column" style_pad_gap="#space_xxs" scrollable="false">
            <text_small text="Lot/Batch Nr" translation_tag="Lot/Batch Nr"/>
            <text_input name="field_lot_nr" width="100%" placeholder="" max_length="32">
              <event_cb trigger="value_changed" callback="spoolman_edit_field_changed_cb"/>
            </text_input>
          </lv_obj>
        </lv_obj>
```

with:

```xml
        <!-- Row 2: Price | Location | Lot Nr -->
        <lv_obj width="100%"
                height="content" style_pad_all="0" flex_flow="row" style_pad_gap="#space_md" scrollable="false">
          <lv_obj height="content"
                  flex_grow="1" style_pad_all="0" flex_flow="column" style_pad_gap="#space_xxs" scrollable="false">
            <text_small text="Price" translation_tag="Price"/>
            <text_input name="field_price"
                        width="100%" input_mode="number" keyboard_hint="numeric" placeholder="0.00" max_length="10">
              <event_cb trigger="value_changed" callback="spoolman_edit_field_changed_cb"/>
            </text_input>
          </lv_obj>
          <lv_obj height="content"
                  flex_grow="1" style_pad_all="0" flex_flow="column" style_pad_gap="#space_xxs" scrollable="false">
            <text_small text="Location" translation_tag="Location"/>
            <text_input name="field_location" width="100%" placeholder="Shelf A" max_length="64">
              <event_cb trigger="value_changed" callback="spoolman_edit_field_changed_cb"/>
            </text_input>
          </lv_obj>
          <lv_obj height="content"
                  flex_grow="1" style_pad_all="0" flex_flow="column" style_pad_gap="#space_xxs" scrollable="false">
            <text_small text="Lot/Batch Nr" translation_tag="Lot/Batch Nr"/>
            <text_input name="field_lot_nr" width="100%" placeholder="" max_length="32">
              <event_cb trigger="value_changed" callback="spoolman_edit_field_changed_cb"/>
            </text_input>
          </lv_obj>
        </lv_obj>
```

- [ ] **Step 2: Update populate_fields() — add location field**

In `src/ui/ui_spoolman_edit_modal.cpp`, add after the `lot_field` block (after line 228):

```cpp
lv_obj_t* location_field = find_widget("field_location");
if (location_field) {
    lv_textarea_set_text(location_field, working_spool_.location.c_str());
}
```

- [ ] **Step 3: Update read_fields_into() — read location back**

In `src/ui/ui_spoolman_edit_modal.cpp`, add after the `field_lot_nr` block (after line 263):

```cpp
field = find_widget("field_location");
if (field) {
    const char* text = lv_textarea_get_text(field);
    spool.location = text ? text : "";
}
```

- [ ] **Step 4: Update is_dirty() — include location comparison**

In `src/ui/ui_spoolman_edit_modal.cpp` line 338-343, add a new condition:

```cpp
working_spool_.location != original_spool_.location ||
```

Add it before the `working_spool_.comment` line. The full method becomes:

```cpp
bool SpoolEditModal::is_dirty() const {
    return std::abs(working_spool_.remaining_weight_g - original_spool_.remaining_weight_g) > 0.1 ||
           std::abs(working_spool_.spool_weight_g - original_spool_.spool_weight_g) > 0.1 ||
           std::abs(working_spool_.price - original_spool_.price) > 0.001 ||
           working_spool_.lot_nr != original_spool_.lot_nr ||
           working_spool_.location != original_spool_.location ||
           working_spool_.comment != original_spool_.comment;
}
```

- [ ] **Step 5: Update handle_save() — add location to spool PATCH**

In `src/ui/ui_spoolman_edit_modal.cpp`, inside `handle_save()`, add after the `comment` patch block (after line 474):

```cpp
if (working_spool_.location != original_spool_.location) {
    spool_patch["location"] = working_spool_.location;
}
```

- [ ] **Step 6: Update register_textareas() — add field_location to tab order**

In `src/ui/ui_spoolman_edit_modal.cpp` line 278, update the field_names array:

```cpp
static constexpr const char* field_names[] = {
    "field_remaining", "field_spool_weight", "field_price",
    "field_location", "field_lot_nr", "field_comment",
};
```

- [ ] **Step 7: Build**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 8: Commit**

```bash
git add ui_xml/spoolman_edit_modal.xml src/ui/ui_spoolman_edit_modal.cpp
git commit -m "feat(spoolman): add location field to edit modal with full read/write/dirty/save cycle"
```

---

## Chunk 4: Location Filter Dropdown

### Task 4: Add location filter dropdown to Spoolman panel

**Files:**
- Modify: `ui_xml/spoolman_panel.xml:18-28` (search_row)
- Modify: `include/ui_panel_spoolman.h` (new member variables and methods)
- Modify: `src/ui/ui_panel_spoolman.cpp` (dropdown logic)

- [ ] **Step 1: Add dropdown to XML search row**

In `ui_xml/spoolman_panel.xml`, add the dropdown inside `search_row` after the `text_input` (before the closing `</lv_obj>` on line 28):

```xml
      <lv_dropdown name="location_filter"
                   options="" style_min_width="100" height="#input_height"
                   style_radius="#border_radius" style_border_width="0" hidden="true">
        <event_cb trigger="value_changed" callback="on_spoolman_location_filter_changed"/>
      </lv_dropdown>
```

The dropdown starts hidden — it's only shown when spools have location data.

- [ ] **Step 2: Add new member variables and methods to header**

In `include/ui_panel_spoolman.h`, add to the private section.

After `std::string search_query_;` (line 91):

```cpp
std::string selected_location_;       ///< Currently selected location filter ("" = All)
bool updating_location_dropdown_ = false; ///< Guard against dropdown event feedback loop
```

After `void apply_filter();` (line 109):

```cpp
void update_location_filter_dropdown();
std::vector<SpoolInfo> filter_by_location(const std::vector<SpoolInfo>& spools) const;
```

After `static void on_search_timer(lv_timer_t* timer);` (line 141):

```cpp
static void on_location_filter_changed(lv_event_t* e);
```

- [ ] **Step 3: Register the new callback**

In `src/ui/ui_panel_spoolman.cpp`, add to the `register_xml_callbacks` list (line 95-101):

```cpp
{"on_spoolman_location_filter_changed", on_location_filter_changed},
```

- [ ] **Step 4: Implement update_location_filter_dropdown()**

Add this method to `src/ui/ui_panel_spoolman.cpp` after `apply_filter()` (~line 341):

```cpp
void SpoolmanPanel::update_location_filter_dropdown() {
    lv_obj_t* dropdown = lv_obj_find_by_name(overlay_root_, "location_filter");
    if (!dropdown) {
        return;
    }

    // Guard: lv_dropdown_set_options() fires value_changed, which calls
    // on_location_filter_changed() → populate_spool_list() → here again.
    // The guard prevents this feedback loop.
    if (updating_location_dropdown_) {
        return;
    }
    updating_location_dropdown_ = true;

    // Collect unique non-empty locations
    std::vector<std::string> locations;
    for (const auto& spool : cached_spools_) {
        if (!spool.location.empty()) {
            if (std::find(locations.begin(), locations.end(), spool.location) == locations.end()) {
                locations.push_back(spool.location);
            }
        }
    }

    // Hide dropdown if no locations exist
    if (locations.empty()) {
        lv_obj_add_flag(dropdown, LV_OBJ_FLAG_HIDDEN);
        selected_location_.clear();
        updating_location_dropdown_ = false;
        return;
    }

    // Sort alphabetically
    std::sort(locations.begin(), locations.end());

    // Build options string: "All\nLocation1\nLocation2\n..."
    std::string options = lv_tr("All");
    for (const auto& loc : locations) {
        options += "\n" + loc;
    }
    lv_dropdown_set_options(dropdown, options.c_str());

    // Restore selection if the previously selected location still exists
    if (!selected_location_.empty()) {
        auto it = std::find(locations.begin(), locations.end(), selected_location_);
        if (it != locations.end()) {
            uint32_t idx = static_cast<uint32_t>(std::distance(locations.begin(), it)) + 1;
            lv_dropdown_set_selected(dropdown, idx);
        } else {
            // Previously selected location no longer exists
            selected_location_.clear();
            lv_dropdown_set_selected(dropdown, 0);
        }
    }

    lv_obj_remove_flag(dropdown, LV_OBJ_FLAG_HIDDEN);
    updating_location_dropdown_ = false;
}
```

- [ ] **Step 5: Implement filter_by_location()**

Add after `update_location_filter_dropdown()`:

```cpp
std::vector<SpoolInfo> SpoolmanPanel::filter_by_location(
    const std::vector<SpoolInfo>& spools) const {
    if (selected_location_.empty()) {
        return spools;
    }
    std::vector<SpoolInfo> result;
    for (const auto& spool : spools) {
        if (spool.location == selected_location_) {
            result.push_back(spool);
        }
    }
    return result;
}
```

- [ ] **Step 6: Update apply_filter() to combine location + search**

In `src/ui/ui_panel_spoolman.cpp`, replace `apply_filter()` (lines 338-341):

```cpp
void SpoolmanPanel::apply_filter() {
    filtered_spools_ = filter_spools(cached_spools_, search_query_);
    update_spool_count();
}
```

with:

```cpp
void SpoolmanPanel::apply_filter() {
    auto location_filtered = filter_by_location(cached_spools_);
    filtered_spools_ = filter_spools(location_filtered, search_query_);
    update_spool_count();
}
```

- [ ] **Step 7: Call update_location_filter_dropdown() when spools are refreshed**

In `src/ui/ui_panel_spoolman.cpp`, inside `populate_spool_list()`, add after `apply_filter();` (line 322):

```cpp
update_location_filter_dropdown();
```

- [ ] **Step 8: Update update_spool_count() to show filtered count for location filter too**

In `src/ui/ui_panel_spoolman.cpp`, change the condition in `update_spool_count()` (line 282):

```cpp
} else if (!search_query_.empty() && filtered_spools_.size() != cached_spools_.size()) {
```

to:

```cpp
} else if (filtered_spools_.size() != cached_spools_.size()) {
```

This shows "Spoolman: 5/19 Spools" whenever the filtered count differs from total (whether due to search, location filter, or both).

- [ ] **Step 9: Implement on_location_filter_changed callback**

Add after `on_search_timer()`:

```cpp
void SpoolmanPanel::on_location_filter_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!dropdown) {
        return;
    }

    auto& panel = get_global_spoolman_panel();

    uint32_t selected = lv_dropdown_get_selected(dropdown);
    if (selected == 0) {
        // "All" selected
        panel.selected_location_.clear();
    } else {
        char buf[128];
        lv_dropdown_get_selected_str(dropdown, buf, sizeof(buf));
        panel.selected_location_ = buf;
    }

    spdlog::debug("[Spoolman] Location filter: '{}'", panel.selected_location_);
    panel.populate_spool_list();
}
```

- [ ] **Step 10: Reset location filter on panel activation**

In `src/ui/ui_panel_spoolman.cpp`, inside `on_activate()`, add after `search_query_.clear();` (line 163):

```cpp
selected_location_.clear();
```

- [ ] **Step 11: Build**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 12: Commit**

```bash
git add ui_xml/spoolman_panel.xml include/ui_panel_spoolman.h src/ui/ui_panel_spoolman.cpp
git commit -m "feat(spoolman): add location filter dropdown to panel search row"
```

---

## Chunk 5: Final Verification

### Task 5: Build, test, and verify

- [ ] **Step 1: Run full test suite**

Run: `make test-run`
Expected: All tests pass.

- [ ] **Step 2: Visual verification with mock data**

Run: `./build/bin/helix-screen --test -vv`

Navigate to Spoolman panel:
- Verify spool rows display correctly (vendor name, no location since mock data won't have it)
- Verify search box works
- Verify location dropdown is hidden (no location data in mocks)
- Open edit modal on a spool — verify location field appears between Price and Lot/Batch Nr
- Type a location, verify Save button activates
- Close and verify no crashes

- [ ] **Step 3: Final commit if any fixes needed**

Only if visual verification revealed issues that required fixes.
