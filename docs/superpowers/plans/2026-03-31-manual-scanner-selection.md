# Manual Barcode Scanner Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users manually select a USB barcode scanner by vendor:product ID when auto-detection fails (prestonbrown/helixscreen#659)

**Architecture:** Add `enumerate_usb_hid_devices()` to `input_device_scanner` that returns all USB HID keyboards with vendor/product IDs. Store selected device in local config. `find_hid_keyboard_devices()` checks config first and falls back to existing name-based priority. UI is a device picker modal launched from a new "Barcode Scanner" action row in the Spoolman settings overlay.

**Tech Stack:** C++17, LVGL 9.5 XML, sysfs, Catch2

---

### Task 1: Add vendor/product ID reading and `enumerate_usb_hid_devices()`

**Files:**
- Modify: `include/input_device_scanner.h`
- Modify: `src/api/input_device_scanner.cpp`
- Modify: `tests/unit/test_input_device_scanner.cpp`

- [ ] **Step 1: Add `UsbHidDevice` struct and `enumerate_usb_hid_devices()` declaration to header**

In `include/input_device_scanner.h`, add after the `ScannedDevice` struct (line 19):

```cpp
/// USB HID device with vendor/product identification for manual scanner selection.
struct UsbHidDevice {
    std::string name;         // e.g., "TMS HIDKeyBoard"
    std::string vendor_id;    // e.g., "1a2c" (hex from sysfs)
    std::string product_id;   // e.g., "4c5e" (hex from sysfs)
    std::string event_path;   // e.g., "/dev/input/event5"
};
```

Add after the `find_hid_keyboard_devices` declarations (after line 43):

```cpp
/// Enumerate all USB HID keyboard-capable devices with vendor/product IDs.
/// Used by the scanner picker UI. Returns all matching devices (no prioritization).
std::vector<UsbHidDevice> enumerate_usb_hid_devices();
std::vector<UsbHidDevice> enumerate_usb_hid_devices(const std::string& dev_base,
                                                     const std::string& sysfs_base);
```

- [ ] **Step 2: Write failing tests for `enumerate_usb_hid_devices()`**

In `tests/unit/test_input_device_scanner.cpp`, extend `MockInputTree::add_device()` to support vendor/product files. Add after line 55 (inside the `for` loop body, before the closing `}`):

```cpp
// Write vendor/product ID files if the bustype is USB or Bluetooth
if (bustype == "0003" || bustype == "0005") {
    std::ofstream(sysfs_path + "/device/id/vendor") << "1a2c";
    std::ofstream(sysfs_path + "/device/id/product")
        << std::string("000") + std::to_string(event_num);
}
```

Then add a new overload to `MockInputTree` (after line 57, before the closing `};` of the struct):

```cpp
void add_device_with_ids(int event_num, const std::string& name,
                         const std::map<std::string, std::string>& caps,
                         const std::string& bustype,
                         const std::string& vendor, const std::string& product) {
    add_device(event_num, name, caps, bustype);
    std::string sysfs_path = sysfs_dir + "/event" + std::to_string(event_num);
    // Overwrite the default IDs written by add_device
    std::ofstream(sysfs_path + "/device/id/vendor") << vendor;
    std::ofstream(sysfs_path + "/device/id/product") << product;
}
```

Add the new test section at the end of the file:

```cpp
TEST_CASE("enumerate_usb_hid_devices returns devices with vendor/product IDs", "[input]") {
    using helix::input::enumerate_usb_hid_devices;

    SECTION("returns device with correct vendor/product IDs") {
        MockInputTree tree("enum_basic");
        tree.add_device_with_ids(2, "TMS HIDKeyBoard", {
            {"key", "40000000"},
            {"abs", "0"},
            {"rel", "0"}
        }, "0003", "1a2c", "4c5e");

        auto result = enumerate_usb_hid_devices(tree.dev_dir, tree.sysfs_dir);
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].name == "TMS HIDKeyBoard");
        REQUIRE(result[0].vendor_id == "1a2c");
        REQUIRE(result[0].product_id == "4c5e");
        REQUIRE(result[0].event_path.find("event2") != std::string::npos);
    }

    SECTION("returns multiple devices") {
        MockInputTree tree("enum_multi");
        tree.add_device_with_ids(1, "USB Keyboard", {
            {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
        }, "0003", "04d9", "a070");
        tree.add_device_with_ids(3, "TMS HIDKeyBoard", {
            {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
        }, "0003", "1a2c", "4c5e");

        auto result = enumerate_usb_hid_devices(tree.dev_dir, tree.sysfs_dir);
        REQUIRE(result.size() == 2);
    }

    SECTION("skips non-USB devices") {
        MockInputTree tree("enum_skip_platform");
        tree.add_device(0, "MCE IR Keyboard", {
            {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
        }, "0019");

        auto result = enumerate_usb_hid_devices(tree.dev_dir, tree.sysfs_dir);
        REQUIRE(result.empty());
    }

    SECTION("skips touchscreens") {
        MockInputTree tree("enum_skip_touch");
        tree.add_device_with_ids(0, "Goodix TS", {
            {"abs", "3"}, {"key", "40000000"}, {"rel", "0"}
        }, "0003", "1234", "5678");

        auto result = enumerate_usb_hid_devices(tree.dev_dir, tree.sysfs_dir);
        REQUIRE(result.empty());
    }

    SECTION("empty directory returns empty vector") {
        MockInputTree tree("enum_empty");
        auto result = enumerate_usb_hid_devices(tree.dev_dir, tree.sysfs_dir);
        REQUIRE(result.empty());
    }
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[input]" -v`
Expected: FAIL — `enumerate_usb_hid_devices` not defined

- [ ] **Step 4: Implement `enumerate_usb_hid_devices()` and vendor/product reading**

In `src/api/input_device_scanner.cpp`, add a helper in the anonymous namespace (after `read_bus_type`, line 43):

```cpp
std::string read_vendor_id(const std::string& sysfs_base, int event_num) {
    return helix::input::read_sysfs_line(
        sysfs_device_path(sysfs_base, event_num, "id/vendor"));
}

std::string read_product_id(const std::string& sysfs_base, int event_num) {
    return helix::input::read_sysfs_line(
        sysfs_device_path(sysfs_base, event_num, "id/product"));
}
```

Add the implementation before the closing `}  // namespace helix::input` (before line 342):

```cpp
std::vector<UsbHidDevice> enumerate_usb_hid_devices(const std::string& dev_base,
                                                     const std::string& sysfs_base) {
    std::vector<UsbHidDevice> devices;

    auto dir = std::unique_ptr<DIR, decltype(&closedir)>(opendir(dev_base.c_str()), closedir);
    if (!dir) {
        spdlog::debug("[InputScanner] Cannot open {}", dev_base);
        return devices;
    }

    struct dirent* entry;
    while ((entry = readdir(dir.get())) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        int event_num = -1;
        if (sscanf(entry->d_name, "event%d", &event_num) != 1 || event_num < 0) continue;

        std::string device_path = dev_base + "/" + entry->d_name;
        if (access(device_path.c_str(), R_OK) != 0) continue;

        int bus = read_bus_type(sysfs_base, event_num);
        if (bus != BUS_USB && bus != BUS_BLUETOOTH) continue;

        std::string key_caps = read_sysfs_capability(sysfs_base, event_num, "key");
        if (!check_capability_bit(key_caps, 30)) continue;

        std::string abs_caps = read_sysfs_capability(sysfs_base, event_num, "abs");
        bool has_legacy_abs = check_capability_bit(abs_caps, 0) && check_capability_bit(abs_caps, 1);
        bool has_mt_abs = check_capability_bit(abs_caps, 53) && check_capability_bit(abs_caps, 54);
        if (has_legacy_abs || has_mt_abs) continue;

        std::string name = read_device_name(sysfs_base, event_num);
        std::string vendor = read_vendor_id(sysfs_base, event_num);
        std::string product = read_product_id(sysfs_base, event_num);

        spdlog::info("[InputScanner] Enumerated USB HID device: {} ({}) vendor={} product={}",
                     device_path, name, vendor, product);
        devices.push_back({std::move(name), std::move(vendor), std::move(product),
                           std::move(device_path)});
    }

    return devices;
}

std::vector<UsbHidDevice> enumerate_usb_hid_devices() {
    return enumerate_usb_hid_devices("/dev/input", "/sys/class/input");
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[input]" -v`
Expected: All `[input]` tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/input_device_scanner.h src/api/input_device_scanner.cpp tests/unit/test_input_device_scanner.cpp
git commit -m "feat(scanner): add enumerate_usb_hid_devices() with vendor/product IDs (#659)"
```

---

### Task 2: Add config-based device matching to `find_hid_keyboard_devices()`

**Files:**
- Modify: `include/input_device_scanner.h`
- Modify: `src/api/input_device_scanner.cpp`
- Modify: `tests/unit/test_input_device_scanner.cpp`

- [ ] **Step 1: Add overload with vendor:product filter parameter**

In `include/input_device_scanner.h`, add after the existing `find_hid_keyboard_devices` declarations:

```cpp
/// Like find_hid_keyboard_devices(), but if configured_vendor_product is non-empty
/// (format "vendor:product", e.g. "1a2c:4c5e"), the matching device is returned
/// as the sole result with highest priority. Falls back to name-based priority
/// if the configured device is not found.
std::vector<ScannedDevice> find_hid_keyboard_devices(const std::string& dev_base,
                                                      const std::string& sysfs_base,
                                                      const std::string& configured_vendor_product);
```

- [ ] **Step 2: Write failing tests**

In `tests/unit/test_input_device_scanner.cpp`, add inside the `find_hid_keyboard_devices` test case (after the "empty directory" section, before the closing `}`):

```cpp
SECTION("configured vendor:product device wins over named scanner") {
    MockInputTree tree("hid_configured");
    tree.add_device_with_ids(1, "Tera Barcode Scanner", {
        {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
    }, "0003", "aaaa", "bbbb");
    tree.add_device_with_ids(2, "TMS HIDKeyBoard", {
        {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
    }, "0003", "1a2c", "4c5e");

    auto result = find_hid_keyboard_devices(tree.dev_dir, tree.sysfs_dir, "1a2c:4c5e");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].name == "TMS HIDKeyBoard");
}

SECTION("configured device not found falls back to normal priority") {
    MockInputTree tree("hid_configured_missing");
    tree.add_device_with_ids(1, "Tera Barcode Scanner", {
        {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
    }, "0003", "aaaa", "bbbb");

    auto result = find_hid_keyboard_devices(tree.dev_dir, tree.sysfs_dir, "1a2c:4c5e");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].name == "Tera Barcode Scanner");
}

SECTION("empty configured string uses normal priority") {
    MockInputTree tree("hid_configured_empty");
    tree.add_device_with_ids(1, "USBKey Module", {
        {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
    }, "0003", "1234", "5678");
    tree.add_device_with_ids(2, "Tera Barcode Scanner", {
        {"key", "40000000"}, {"abs", "0"}, {"rel", "0"}
    }, "0003", "aaaa", "bbbb");

    auto result = find_hid_keyboard_devices(tree.dev_dir, tree.sysfs_dir, "");
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].name == "Tera Barcode Scanner");
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[input]" -v`
Expected: FAIL — no matching overload

- [ ] **Step 4: Implement the filtered overload**

In `src/api/input_device_scanner.cpp`, add after `find_hid_keyboard_devices()` (the no-arg wrapper, line 303):

```cpp
std::vector<ScannedDevice> find_hid_keyboard_devices(const std::string& dev_base,
                                                      const std::string& sysfs_base,
                                                      const std::string& configured_vendor_product) {
    if (configured_vendor_product.empty()) {
        return find_hid_keyboard_devices(dev_base, sysfs_base);
    }

    // Parse "vendor:product" string
    auto colon = configured_vendor_product.find(':');
    if (colon == std::string::npos) {
        spdlog::warn("[InputScanner] Invalid configured_vendor_product format: {}",
                     configured_vendor_product);
        return find_hid_keyboard_devices(dev_base, sysfs_base);
    }
    std::string target_vendor = configured_vendor_product.substr(0, colon);
    std::string target_product = configured_vendor_product.substr(colon + 1);

    // Scan for the configured device
    auto dir = std::unique_ptr<DIR, decltype(&closedir)>(opendir(dev_base.c_str()), closedir);
    if (!dir) return find_hid_keyboard_devices(dev_base, sysfs_base);

    struct dirent* entry;
    while ((entry = readdir(dir.get())) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        int event_num = -1;
        if (sscanf(entry->d_name, "event%d", &event_num) != 1 || event_num < 0) continue;

        std::string device_path = dev_base + "/" + entry->d_name;
        if (access(device_path.c_str(), R_OK) != 0) continue;

        int bus = read_bus_type(sysfs_base, event_num);
        if (bus != BUS_USB && bus != BUS_BLUETOOTH) continue;

        std::string vendor = read_vendor_id(sysfs_base, event_num);
        std::string product = read_product_id(sysfs_base, event_num);

        if (vendor == target_vendor && product == target_product) {
            std::string name = read_device_name(sysfs_base, event_num);
            spdlog::info("[InputScanner] Found configured scanner device: {} ({}) "
                         "vendor={} product={}", device_path, name, vendor, product);
            return {{device_path, name, event_num}};
        }
    }

    spdlog::info("[InputScanner] Configured device {}:{} not found, falling back to auto-detect",
                 target_vendor, target_product);
    return find_hid_keyboard_devices(dev_base, sysfs_base);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[input]" -v`
Expected: All `[input]` tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/input_device_scanner.h src/api/input_device_scanner.cpp tests/unit/test_input_device_scanner.cpp
git commit -m "feat(scanner): config-based vendor:product matching in find_hid_keyboard_devices (#659)"
```

---

### Task 3: Add scanner config to SettingsManager and wire into `UsbScannerMonitor`

**Files:**
- Modify: `include/settings_manager.h`
- Modify: `src/system/settings_manager.cpp`
- Modify: `src/system/usb_scanner_monitor.cpp`

- [ ] **Step 1: Add getter/setter declarations to SettingsManager**

In `include/settings_manager.h`, add before the `private:` section (before line 262):

```cpp
    // =========================================================================
    // BARCODE SCANNER (owned by SettingsManager — manual device selection)
    // =========================================================================

    /** @brief Get configured scanner vendor:product ID (empty = auto-detect) */
    std::string get_scanner_device_id() const;

    /** @brief Set scanner vendor:product ID (empty = clear, auto-detect) */
    void set_scanner_device_id(const std::string& vendor_product);

    /** @brief Get configured scanner device display name */
    std::string get_scanner_device_name() const;

    /** @brief Set configured scanner device display name */
    void set_scanner_device_name(const std::string& name);
```

Add private members (after `chamber_sensor_assignment_`, line 283):

```cpp
    // Scanner device selection (plain strings, no LVGL subjects needed)
    std::string scanner_device_id_;       // "vendor:product" or empty
    std::string scanner_device_name_;     // display name for UI
```

- [ ] **Step 2: Implement getters/setters and load from config**

In `src/system/settings_manager.cpp`, load from config in `init_subjects()`. Add at the end of the function, before `subjects_initialized_ = true;`:

```cpp
    // Load scanner device selection
    scanner_device_id_ = config->get<std::string>(config->df() + "scanner/usb_vendor_product", "");
    scanner_device_name_ = config->get<std::string>(config->df() + "scanner/usb_device_name", "");
    if (!scanner_device_id_.empty()) {
        spdlog::info("[SettingsManager] Loaded scanner device: {} ({})",
                     scanner_device_name_, scanner_device_id_);
    }
```

Add the getter/setter implementations at the end of the file (before the closing `}`):

```cpp
std::string SettingsManager::get_scanner_device_id() const {
    return scanner_device_id_;
}

void SettingsManager::set_scanner_device_id(const std::string& vendor_product) {
    spdlog::info("[SettingsManager] set_scanner_device_id({})", vendor_product);
    scanner_device_id_ = vendor_product;
    Config* config = Config::get_instance();
    config->set<std::string>(config->df() + "scanner/usb_vendor_product", vendor_product);
    config->save();
}

std::string SettingsManager::get_scanner_device_name() const {
    return scanner_device_name_;
}

void SettingsManager::set_scanner_device_name(const std::string& name) {
    spdlog::info("[SettingsManager] set_scanner_device_name({})", name);
    scanner_device_name_ = name;
    Config* config = Config::get_instance();
    config->set<std::string>(config->df() + "scanner/usb_device_name", name);
    config->save();
}
```

- [ ] **Step 3: Wire config into `UsbScannerMonitor::find_scanner_devices()`**

In `src/system/usb_scanner_monitor.cpp`, modify `find_scanner_devices()` (lines 77-88) to read the configured device ID:

```cpp
std::vector<std::string> UsbScannerMonitor::find_scanner_devices()
{
    // Check if user has manually configured a specific scanner device
    std::string configured_id = helix::SettingsManager::instance().get_scanner_device_id();

    auto devices = helix::input::find_hid_keyboard_devices(
        "/dev/input", "/sys/class/input", configured_id);
    if (devices.empty()) return {};

    spdlog::info("UsbScannerMonitor: using HID device: {} ({})",
                 devices[0].path, devices[0].name);
    return {devices[0].path};
}
```

Add the include at the top of the file (with the other includes):

```cpp
#include "settings_manager.h"
```

- [ ] **Step 4: Build to verify compilation**

Run: `make -j`
Expected: Clean build, no errors

- [ ] **Step 5: Run existing tests to verify no regressions**

Run: `make test-run`
Expected: All tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/settings_manager.h src/system/settings_manager.cpp src/system/usb_scanner_monitor.cpp
git commit -m "feat(scanner): persist manual scanner selection in config (#659)"
```

---

### Task 4: Add "Barcode Scanner" action row to Spoolman overlay XML

**Files:**
- Modify: `ui_xml/spoolman_settings.xml`

- [ ] **Step 1: Add the action row to the Spoolman settings XML**

In `ui_xml/spoolman_settings.xml`, add after the label printer row (after line 139, before the closing `</lv_obj>`):

```xml
      <!-- ================================================================== -->
      <!-- Barcode Scanner (opens device picker)                              -->
      <!-- ================================================================== -->
      <setting_action_row name="row_barcode_scanner"
                          label="Barcode Scanner" label_tag="Barcode Scanner" icon="barcode_scanner"
                          bind_description="scanner_device_status"
                          callback="on_barcode_scanner_clicked"/>
```

- [ ] **Step 2: Verify the XML loads**

Applying [L031]: XML files are loaded at runtime — no rebuild needed, just relaunch.

Run: `./build/bin/helix-screen --test -vv -p settings 2>&1 | head -50`
Expected: No XML parse errors. The app launches. The "Barcode Scanner" row appears in Spoolman settings (if you navigate there — the icon may be missing if `barcode_scanner` isn't in the icon font, which is fine for now).

- [ ] **Step 3: Commit**

```bash
git add ui_xml/spoolman_settings.xml
git commit -m "feat(scanner): add barcode scanner action row to Spoolman settings XML (#659)"
```

---

### Task 5: Create scanner picker modal XML

**Files:**
- Create: `ui_xml/scanner_picker_modal.xml`
- Modify: `src/application/main.cpp` (register the XML component)

- [ ] **Step 1: Create the scanner picker modal XML**

Applying [L014]: Must register new XML components in main.cpp.

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Scanner Device Picker Modal -->
<!-- Lists connected USB HID devices for manual barcode scanner selection -->
<component>
  <view name="scanner_picker_modal"
        extends="ui_dialog" title="Select Barcode Scanner" title_tag="Select Barcode Scanner">
    <!-- Scrollable device list -->
    <lv_obj name="scanner_device_list"
            width="100%" flex_grow="1" flex_flow="column" style_pad_gap="0"
            style_pad_all="0" scrollable="true"/>
    <!-- Empty state message (shown when no devices found) -->
    <lv_obj name="scanner_empty_state"
            width="100%" height="content" style_pad_all="#space_lg" flex_flow="column"
            style_flex_main_place="center" style_flex_cross_place="center"
            style_pad_gap="#space_sm" scrollable="false">
      <text_body name="empty_text"
                 text="No USB HID devices detected." translation_tag="No USB HID devices detected."
                 align="center"/>
      <text_small text="Plug in your scanner and tap Refresh." translation_tag="Plug in your scanner and tap Refresh."
                  style_text_color="#text_muted" align="center"/>
    </lv_obj>
    <!-- Button row: Refresh + Close -->
    <modal_button_row
        secondary_text="Refresh" secondary_tag="Refresh" secondary_callback="on_scanner_refresh"
        primary_text="Close" primary_tag="Close" primary_callback="on_scanner_close"/>
  </view>
</component>
```

- [ ] **Step 2: Register the component in main.cpp**

In `src/application/main.cpp`, find the block of `lv_xml_component_register_from_file()` calls. Add in alphabetical order:

```cpp
lv_xml_component_register_from_file("scanner_picker_modal",
                                     (xml_dir + "scanner_picker_modal.xml").c_str());
```

- [ ] **Step 3: Build and verify**

Run: `make -j`
Expected: Clean build

- [ ] **Step 4: Commit**

```bash
git add ui_xml/scanner_picker_modal.xml src/application/main.cpp
git commit -m "feat(scanner): add scanner picker modal XML component (#659)"
```

---

### Task 6: Implement scanner picker modal and wire into Spoolman overlay

**Files:**
- Create: `include/ui_modal_scanner_picker.h`
- Create: `src/ui/ui_modal_scanner_picker.cpp`
- Modify: `include/ui_spoolman_overlay.h`
- Modify: `src/ui/ui_spoolman_overlay.cpp`
- Modify: `Makefile` (if new .cpp files aren't auto-discovered)

- [ ] **Step 1: Create the scanner picker modal header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"

#include "input_device_scanner.h"

#include <functional>
#include <string>
#include <vector>

namespace helix::ui {

/// Modal that lists connected USB HID devices for manual scanner selection.
/// Shows auto-detect option at top, then each discovered device.
class ScannerPickerModal : public Modal {
  public:
    using SelectionCallback = std::function<void(const std::string& vendor_product,
                                                  const std::string& device_name)>;

    explicit ScannerPickerModal(SelectionCallback on_select);
    ~ScannerPickerModal() override = default;

    const char* get_name() const override { return "Scanner Picker"; }
    const char* component_name() const override { return "scanner_picker_modal"; }

    void on_created() override;

  private:
    void populate_device_list();
    void add_device_row(lv_obj_t* list, const std::string& label,
                        const std::string& sublabel,
                        const std::string& vendor_product);
    void handle_device_selected(const std::string& vendor_product,
                                const std::string& device_name);

    static void on_scanner_refresh(lv_event_t* e);
    static void on_scanner_close(lv_event_t* e);

    SelectionCallback on_select_;
    std::string current_device_id_;
    lv_obj_t* device_list_ = nullptr;
    lv_obj_t* empty_state_ = nullptr;
};

} // namespace helix::ui
```

- [ ] **Step 2: Create the scanner picker modal implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_modal_scanner_picker.h"

#include "settings_manager.h"
#include "theme_manager.h"
#include "ui_event_safety.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

ScannerPickerModal::ScannerPickerModal(SelectionCallback on_select)
    : on_select_(std::move(on_select)) {
    current_device_id_ = helix::SettingsManager::instance().get_scanner_device_id();
}

void ScannerPickerModal::on_created() {
    // Register callbacks
    lv_xml_register_event_cb(nullptr, "on_scanner_refresh", on_scanner_refresh);
    lv_xml_register_event_cb(nullptr, "on_scanner_close", on_scanner_close);

    // Find widgets
    device_list_ = lv_obj_find_by_name(dialog_, "scanner_device_list");
    empty_state_ = lv_obj_find_by_name(dialog_, "scanner_empty_state");

    populate_device_list();
}

void ScannerPickerModal::populate_device_list() {
    if (!device_list_) return;

    lv_obj_clean(device_list_);

    // Always show auto-detect option first
    add_device_row(device_list_,
                   lv_tr("Auto-detect (default)"),
                   lv_tr("Use name-based detection"),
                   "");

    // Enumerate connected USB HID devices
    auto devices = helix::input::enumerate_usb_hid_devices();

    // Show/hide empty state
    if (empty_state_) {
        if (devices.empty()) {
            lv_obj_remove_flag(empty_state_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(empty_state_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (const auto& dev : devices) {
        std::string vendor_product = dev.vendor_id + ":" + dev.product_id;
        std::string sublabel = vendor_product + " — " + dev.event_path;
        add_device_row(device_list_, dev.name, sublabel, vendor_product);
    }

    spdlog::debug("[ScannerPicker] Populated {} device(s)", devices.size());
}

void ScannerPickerModal::add_device_row(lv_obj_t* list, const std::string& label,
                                         const std::string& sublabel,
                                         const std::string& vendor_product) {
    // Create a clickable row
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(row, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Highlight if this is the currently selected device
    bool is_selected = (vendor_product == current_device_id_);
    if (is_selected) {
        lv_obj_set_style_bg_color(row, theme_manager_get_color("primary"), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, 30, LV_PART_MAIN);
    }

    // Device name label
    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, label.c_str());
    lv_obj_set_style_text_font(name_label, theme_manager_get_font("body"), LV_PART_MAIN);
    if (is_selected) {
        lv_obj_set_style_text_color(name_label, theme_manager_get_color("primary"), LV_PART_MAIN);
    }
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Sublabel
    lv_obj_t* sub_label = lv_label_create(row);
    lv_label_set_text(sub_label, sublabel.c_str());
    lv_obj_set_style_text_font(sub_label, theme_manager_get_font("small"), LV_PART_MAIN);
    lv_obj_set_style_text_color(sub_label, theme_manager_get_color("text_muted"), LV_PART_MAIN);
    lv_obj_remove_flag(sub_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sub_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Store selection data as user data for the click handler
    struct RowData {
        std::string vendor_product;
        std::string device_name;
        ScannerPickerModal* modal;
    };
    auto* data = new RowData{vendor_product, label, this};
    lv_obj_set_user_data(row, data);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        auto* row_obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto* d = static_cast<RowData*>(lv_obj_get_user_data(row_obj));
        if (d && d->modal) {
            d->modal->handle_device_selected(d->vendor_product, d->device_name);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Clean up RowData when row is deleted
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        auto* row_obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto* d = static_cast<RowData*>(lv_obj_get_user_data(row_obj));
        delete d;
        lv_obj_set_user_data(row_obj, nullptr);
    }, LV_EVENT_DELETE, nullptr);
}

void ScannerPickerModal::handle_device_selected(const std::string& vendor_product,
                                                  const std::string& device_name) {
    spdlog::info("[ScannerPicker] Selected: {} ({})",
                 device_name, vendor_product.empty() ? "auto-detect" : vendor_product);

    // Persist selection
    helix::SettingsManager::instance().set_scanner_device_id(vendor_product);
    helix::SettingsManager::instance().set_scanner_device_name(
        vendor_product.empty() ? "" : device_name);

    // Notify caller
    if (on_select_) {
        on_select_(vendor_product, device_name);
    }

    // Close modal
    Modal::hide(dialog_);
}

void ScannerPickerModal::on_scanner_refresh(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ScannerPicker] on_scanner_refresh");
    // Find the modal instance via the dialog
    auto* modal = Modal::get_active<ScannerPickerModal>();
    if (modal) {
        modal->populate_device_list();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ScannerPickerModal::on_scanner_close(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ScannerPicker] on_scanner_close");
    auto* modal = Modal::get_active<ScannerPickerModal>();
    if (modal) {
        Modal::hide(modal->dialog_);
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
```

**Important:** The above implementation uses `lv_obj_add_event_cb()` for the row click handlers. Per CLAUDE.md rules, we should use XML event callbacks. However, since these rows are dynamically created (not from XML), using `lv_obj_add_event_cb()` is acceptable — this falls under the "widget pool recycling" exception. The modal buttons (Refresh/Close) use XML `<event_cb>` as required.

**Note on `Modal::get_active<T>()`:** Check if this method exists. If not, the pattern used in other modals should be followed. Read `include/ui_modal.h` to verify the API. The implementation may need adjustment based on the actual Modal API — the subagent implementing this task should read `include/ui_modal.h` and `src/ui/ui_modal.cpp` to verify and adapt.

- [ ] **Step 3: Add scanner status subject and callback to SpoolmanOverlay**

In `include/ui_spoolman_overlay.h`, add to the private section (after `status_card_`, line 268):

```cpp
    // Scanner device status display subject
    lv_subject_t scanner_device_status_subject_;
    char scanner_status_buf_[64];

    // Scanner picker callback
    static void on_barcode_scanner_clicked(lv_event_t* e);
    void handle_barcode_scanner_clicked();
    void update_scanner_status_text();
```

In `src/ui/ui_spoolman_overlay.cpp`, add the include:

```cpp
#include "ui_modal_scanner_picker.h"
```

In `init_subjects()`, add before `subjects_initialized_ = true;`:

```cpp
    // Scanner device status text
    auto scanner_name = helix::SettingsManager::instance().get_scanner_device_name();
    auto scanner_id = helix::SettingsManager::instance().get_scanner_device_id();
    const char* status = scanner_id.empty() ? "Auto-detect" : scanner_name.c_str();
    snprintf(scanner_status_buf_, sizeof(scanner_status_buf_), "%s", status);
    lv_subject_init_string(&scanner_device_status_subject_, scanner_status_buf_, nullptr,
                           sizeof(scanner_status_buf_), scanner_status_buf_);
    lv_xml_register_subject(nullptr, "scanner_device_status", &scanner_device_status_subject_);
```

In `register_callbacks()`, add with the other callback registrations:

```cpp
    lv_xml_register_event_cb(nullptr, "on_barcode_scanner_clicked", on_barcode_scanner_clicked);
```

In the destructor, add deinit for the new subject (alongside the existing ones):

```cpp
    lv_subject_deinit(&scanner_device_status_subject_);
```

In `on_ui_destroyed()`, no new widget pointers to null.

Add the callback implementations:

```cpp
void SpoolmanOverlay::on_barcode_scanner_clicked(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[SpoolmanOverlay] on_barcode_scanner_clicked");
    get_spoolman_overlay().handle_barcode_scanner_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void SpoolmanOverlay::handle_barcode_scanner_clicked() {
    spdlog::debug("[{}] Barcode Scanner clicked - opening picker", get_name());

    auto modal = std::make_shared<ScannerPickerModal>(
        [this](const std::string& /*vendor_product*/, const std::string& /*device_name*/) {
            update_scanner_status_text();
        });
    Modal::show(modal);
}

void SpoolmanOverlay::update_scanner_status_text() {
    auto id = helix::SettingsManager::instance().get_scanner_device_id();
    auto name = helix::SettingsManager::instance().get_scanner_device_name();
    // i18n: do not translate "Auto-detect" — it's a technical label
    const char* status = id.empty() ? "Auto-detect" : name.c_str();
    snprintf(scanner_status_buf_, sizeof(scanner_status_buf_), "%s", status);
    lv_subject_set_string(&scanner_device_status_subject_, scanner_status_buf_);
}
```

- [ ] **Step 4: Build**

Run: `make -j`
Expected: Clean build. If the Makefile auto-discovers .cpp files in src/ui/, no Makefile change needed. If not, add `src/ui/ui_modal_scanner_picker.cpp` to the source list.

- [ ] **Step 5: Run tests**

Run: `make test-run`
Expected: All tests pass (no test regressions)

- [ ] **Step 6: Commit**

```bash
git add include/ui_modal_scanner_picker.h src/ui/ui_modal_scanner_picker.cpp include/ui_spoolman_overlay.h src/ui/ui_spoolman_overlay.cpp
git commit -m "feat(scanner): scanner picker modal and Spoolman overlay integration (#659)"
```

---

### Task 7: Verify icon availability and end-to-end test

**Files:**
- Possibly modify: `include/codepoints.h`, `scripts/regen_mdi_fonts.sh`

- [ ] **Step 1: Check if `barcode_scanner` icon exists**

Applying [L009]: Icon font sync workflow.

Search `include/codepoints.h` for `barcode_scanner`. If it exists, skip to Step 3.

- [ ] **Step 2: Add icon if missing**

If `barcode_scanner` is not in codepoints.h:
1. Add `MDI_BARCODE_SCAN` (or similar) to `include/codepoints.h`
2. Add the codepoint to `scripts/regen_mdi_fonts.sh`
3. Run `make regen-fonts`
4. Rebuild: `make -j`

If `barcode_scanner` doesn't have a good MDI match, use `barcode` or `qrcode_scan` instead — update the XML icon name in `spoolman_settings.xml` to match.

- [ ] **Step 3: Launch app and verify end-to-end**

Applying [L060]: Interactive UI testing requires user.

Run in background: `./build/bin/helix-screen --test -vv -p settings 2>&1 | tee /tmp/scanner_test.log`

Tell user to:
1. Navigate to Settings → Spoolman
2. Verify "Barcode Scanner" row appears with "Auto-detect" description
3. Tap the row to open the picker modal
4. Verify the modal shows "Auto-detect (default)" and any connected USB devices
5. Close the modal

Read `/tmp/scanner_test.log` and verify no errors.

- [ ] **Step 4: Commit icon changes (if any)**

```bash
git add include/codepoints.h scripts/regen_mdi_fonts.sh assets/fonts/
git commit -m "chore(icons): add barcode_scanner icon for scanner picker (#659)"
```

- [ ] **Step 5: Final commit (if any remaining changes)**

Run `git status` and commit any remaining changes.
