// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_modal_scanner_picker.h"

#include "settings_manager.h"
#include "theme_manager.h"
#include "ui_event_safety.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

ScannerPickerModal* ScannerPickerModal::s_active_instance_ = nullptr;

// ============================================================================
// RowData — attached to each clickable row via lv_obj_set_user_data()
// ============================================================================

struct RowData {
    std::string vendor_product; // "" = auto-detect
    std::string device_name;
    ScannerPickerModal* modal;
};

// ============================================================================
// CONSTRUCTION
// ============================================================================

ScannerPickerModal::ScannerPickerModal(SelectionCallback on_select)
    : on_select_(std::move(on_select)) {
    // Read current selection from SettingsManager
    current_device_id_ = helix::SettingsManager::instance().get_scanner_device_id();

    // Register XML callback BEFORE show() creates the XML component.
    // Modal::show() calls create_and_show() before on_show(), so callbacks
    // must be registered in the constructor to be available during XML parsing.
    lv_xml_register_event_cb(nullptr, "on_scanner_refresh", [](lv_event_t* /*e*/) {
        LVGL_SAFE_EVENT_CB_BEGIN("[ScannerPickerModal] on_scanner_refresh");
        if (s_active_instance_) {
            s_active_instance_->populate_device_list();
        }
        LVGL_SAFE_EVENT_CB_END();
    });
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void ScannerPickerModal::on_show() {
    s_active_instance_ = this;

    // Store this pointer on dialog for static callback lookup
    if (dialog()) {
        lv_obj_set_user_data(dialog(), this);
    }

    // Wire close (X) button and Cancel button to Modal cancel handler
    wire_cancel_button("btn_close");
    wire_cancel_button("btn_primary");

    // Find widget pointers from XML
    device_list_ = find_widget("scanner_device_list");
    empty_state_ = find_widget("scanner_empty_state");

    populate_device_list();

    spdlog::debug("[{}] Shown, current device: '{}'", get_name(),
                  current_device_id_.empty() ? "auto-detect" : current_device_id_);
}

// ============================================================================
// DEVICE LIST POPULATION
// ============================================================================

void ScannerPickerModal::populate_device_list() {
    if (!device_list_) {
        spdlog::warn("[{}] device_list_ widget not found", get_name());
        return;
    }

    // Clean existing rows
    lv_obj_clean(device_list_);

    // Add "Auto-detect (default)" row
    add_device_row(device_list_, "Auto-detect (default)", "Uses name-based priority", "");

    // Enumerate USB HID devices
    auto devices = helix::input::enumerate_usb_hid_devices();

    spdlog::debug("[{}] Found {} USB HID devices", get_name(), devices.size());

    for (const auto& dev : devices) {
        std::string vendor_product = dev.vendor_id + ":" + dev.product_id;
        std::string sublabel = vendor_product + "  " + dev.event_path;
        add_device_row(device_list_, dev.name, sublabel, vendor_product);
    }

    // Show/hide empty state
    if (empty_state_) {
        if (devices.empty()) {
            lv_obj_remove_flag(empty_state_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(empty_state_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ============================================================================
// ROW CREATION
// ============================================================================

void ScannerPickerModal::add_device_row(lv_obj_t* list, const std::string& label,
                                         const std::string& sublabel,
                                         const std::string& vendor_product) {
    // Create a row container
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, theme_manager_get_spacing("space_md"), 0);
    lv_obj_set_style_pad_gap(row, 4, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(row, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // Highlight selected device
    bool is_selected = (vendor_product == current_device_id_);
    if (is_selected) {
        auto primary = theme_manager_get_color("primary");
        lv_obj_set_style_bg_color(row, primary, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, primary, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_60, 0);
        lv_obj_set_style_radius(row, 8, 0);
    } else {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    }

    // Add bottom border as separator (except for selected items which have full border)
    if (!is_selected) {
        auto border_color = theme_manager_get_color("border");
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, border_color, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_30, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    }

    // Name label (body font) — clickable=false so clicks bubble to row
    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, label.c_str());
    lv_obj_set_style_text_font(name_label, theme_manager_get_font("font_body"), 0);
    lv_obj_set_width(name_label, lv_pct(100));
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_obj_remove_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Sublabel (small font, muted color) — clickable=false so clicks bubble to row
    lv_obj_t* sub_label = lv_label_create(row);
    lv_label_set_text(sub_label, sublabel.c_str());
    lv_obj_set_style_text_font(sub_label, theme_manager_get_font("font_small"), 0);
    lv_obj_set_style_text_color(sub_label, theme_manager_get_color("text_muted"), 0);
    lv_obj_set_width(sub_label, lv_pct(100));
    lv_label_set_long_mode(sub_label, LV_LABEL_LONG_WRAP);
    lv_obj_remove_flag(sub_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sub_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Attach RowData for click handling
    auto* data = new RowData{vendor_product, label, this};
    lv_obj_set_user_data(row, data);

    // Click handler — dynamic list content, lv_obj_add_event_cb is acceptable
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
            auto* row_obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            auto* d = static_cast<RowData*>(lv_obj_get_user_data(row_obj));
            if (d && d->modal) {
                d->modal->handle_device_selected(d->vendor_product, d->device_name);
            }
        },
        LV_EVENT_CLICKED, nullptr);

    // Clean up RowData on delete to prevent leaks
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
            auto* row_obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            delete static_cast<RowData*>(lv_obj_get_user_data(row_obj));
            lv_obj_set_user_data(row_obj, nullptr);
        },
        LV_EVENT_DELETE, nullptr);
}

// ============================================================================
// SELECTION HANDLING
// ============================================================================

void ScannerPickerModal::handle_device_selected(const std::string& vendor_product,
                                                 const std::string& device_name) {
    spdlog::info("[{}] Selected device: '{}' ({})", get_name(), device_name,
                 vendor_product.empty() ? "auto-detect" : vendor_product);

    // Persist selection
    helix::SettingsManager::instance().set_scanner_device_id(vendor_product);
    helix::SettingsManager::instance().set_scanner_device_name(
        vendor_product.empty() ? "" : device_name);

    // Fire callback
    if (on_select_) {
        on_select_(vendor_product, device_name);
    }

    // Clear active instance before hide (hide destroys us)
    s_active_instance_ = nullptr;

    hide();
}

} // namespace helix::ui
