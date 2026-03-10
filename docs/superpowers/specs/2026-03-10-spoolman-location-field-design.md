# Spoolman Location Field Support

## Overview

Add support for Spoolman's `location` field — an optional string (max 64 chars) describing where a spool is physically stored (e.g., "Shelf A", "Drawer 3"). This includes displaying, editing, searching, and filtering by location.

## Spoolman API Reference

- Field: `location` on the Spool model
- Type: `string | null`, max 64 characters
- Description: "Where this spool can be found"
- Read via `GET /v1/spool` response JSON
- Write via `PATCH /v1/spool/{id}` with `{"location": "value"}`

## Changes

### 1. Data Layer

**`include/spoolman_types.h`** — Add to `SpoolInfo`:
```cpp
std::string location;  // Physical storage location (max 64 chars)
```

Update `filter_spools()` doc comment (line ~170) to include "location" in the searchable fields list.

**`src/api/moonraker_spoolman_api.cpp`** — In `parse_spool_info()`:
```cpp
info.location = safe_string(j, "location");
```

### 2. Spool Row Display

**`ui_xml/spoolman_spool_row.xml`** — No XML changes needed. The `spool_vendor` text_small widget already exists.

**`src/ui/ui_spoolman_list_view.cpp`** — When populating row data, set vendor label to combined string:
- With location: `"Polymaker · Shelf A"`
- Without location: `"Polymaker"`
- Separator: ` · ` (middle dot with spaces)

### 3. Edit Modal

**`ui_xml/spoolman_edit_modal.xml`** — Modify Row 2 from two fields to three:
```
Row 2: Price | Location | Lot/Batch Nr
```
- `field_location`: `text_input`, `max_length="64"`, `placeholder="Shelf A"`
- All three fields use `flex_grow="1"`

Note: At modal `style_min_width="380"` minus the 150px left column and padding, three fields will be tight (~105px each). If this proves unusable on small screens, fall back to giving Location its own row.

**`src/ui/ui_spoolman_edit_modal.cpp`** — Update the full read/write/dirty cycle:
- `populate_fields()` — set textarea text from `working_spool_.location`
- `read_fields_into()` — read textarea back into `spool.location`
- `is_dirty()` — add `working_spool_.location != original_spool_.location`
- `handle_save()` — add `location` to `spool_patch` JSON when changed
- `register_textareas()` — include `field_location` in tab-order array

### 4. Search Integration

**`src/printer/spoolman_types.cpp`** — In `filter_spools()`, append `spool.location` to the searchable string:
```cpp
std::string searchable = "#" + std::to_string(spool.id) + " " + spool.vendor + " " +
                         spool.material + " " + spool.color_name + " " + spool.location;
```

### 5. Location Filter Dropdown

**`ui_xml/spoolman_panel.xml`** — Add `lv_dropdown` in `search_row`, inline next to the search box:
```xml
<lv_dropdown name="location_filter" style_min_width="100" .../>
```
- Sits to the right of `search_box` in the same flex row
- Search box keeps `flex_grow="1"`, dropdown has fixed/min width

**`src/ui/ui_panel_spoolman.cpp`** — Populate dropdown and apply filter:
- Extract unique non-empty `location` values from loaded spools
- Sort alphabetically
- Options: `"All\nShelf A\nDrawer 3\n..."` (applying [L045]: use `\n` in C++ code, `&#10;` only in XML attributes)
- "All" is default (index 0), shows all spools
- On selection change, apply location filter
- Filter ordering: location dropdown pre-filters `cached_spools_`, then `filter_spools()` applies search text to the result
- Re-evaluate dropdown visibility and options each time spools are refreshed (including after edit modal saves)
- Hidden when no spools have locations set (avoid showing an empty "All"-only dropdown)

### 6. Tests

**`tests/unit/test_spoolman.cpp`** — Add/update tests:
- `parse_spool_info()` parses `location` from JSON (present, null, missing)
- `filter_spools()` matches on location substring
- `filter_spools()` still works when location is empty

### Non-goals (deferred)

- Sort by location
- Combobox widget (type + dropdown suggestions) for location field in edit modal
- Location autocomplete

## Files Modified

| File | Change |
|------|--------|
| `include/spoolman_types.h` | Add `location` field to `SpoolInfo`, update `filter_spools` doc comment |
| `src/api/moonraker_spoolman_api.cpp` | Parse `location` from JSON |
| `src/printer/spoolman_types.cpp` | Add `location` to searchable string in `filter_spools()` |
| `ui_xml/spoolman_edit_modal.xml` | Add location field to Row 2 (three fields) |
| `ui_xml/spoolman_panel.xml` | Add location filter dropdown in search row |
| `src/ui/ui_spoolman_list_view.cpp` | Display location inline with vendor name |
| `src/ui/ui_spoolman_edit_modal.cpp` | Full read/write/dirty/save/tab-order for location |
| `src/ui/ui_panel_spoolman.cpp` | Populate filter dropdown, apply combined filter |
| `tests/unit/test_spoolman.cpp` | Test location parsing and search matching |
