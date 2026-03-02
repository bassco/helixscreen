// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_printer_list_overlay.h"

#include "app_globals.h"
#include "config.h"
#include "printer_state.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "ui_callback_helpers.h"
#include "ui_event_safety.h"
#include "ui_icon_codepoints.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

// =============================================================================
// Global Instance
// =============================================================================

static std::unique_ptr<helix::ui::PrinterListOverlay> g_printer_list_overlay;

namespace helix::ui {

bool PrinterListOverlay::s_callbacks_registered_ = false;

PrinterListOverlay& get_printer_list_overlay() {
    if (!g_printer_list_overlay) {
        g_printer_list_overlay = std::make_unique<PrinterListOverlay>();
        StaticPanelRegistry::instance().register_destroy("PrinterListOverlay",
                                                         []() { g_printer_list_overlay.reset(); });
    }
    return *g_printer_list_overlay;
}

// =============================================================================
// Callback Registration
// =============================================================================

void PrinterListOverlay::register_callbacks() {
    if (s_callbacks_registered_) {
        return;
    }

    register_xml_callbacks({
        {"printer_list_add_cb", on_add_printer_cb},
    });

    s_callbacks_registered_ = true;
    spdlog::debug("[PrinterListOverlay] Callbacks registered");
}

// =============================================================================
// Create / Show
// =============================================================================

lv_obj_t* PrinterListOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    if (!create_overlay_from_xml(parent, "printer_list_overlay")) {
        return nullptr;
    }

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void PrinterListOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    register_callbacks();

    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    NavigationManager::instance().push_overlay(overlay_root_);
}

// =============================================================================
// Lifecycle
// =============================================================================

void PrinterListOverlay::on_activate() {
    OverlayBase::on_activate();
    populate_printer_list();
}

void PrinterListOverlay::on_deactivate() {
    cleanup_row_user_data();
    OverlayBase::on_deactivate();
}

// =============================================================================
// Printer List Population
// =============================================================================

void PrinterListOverlay::populate_printer_list() {
    auto* cfg = Config::get_instance();
    if (!cfg) {
        spdlog::warn("[{}] No config instance", get_name());
        return;
    }

    auto printer_ids = cfg->get_printer_ids();
    auto active_id = cfg->get_active_printer_id();

    lv_obj_t* container = lv_obj_find_by_name(overlay_root_, "printer_list_container");
    if (!container) {
        spdlog::error("[{}] printer_list_container not found in XML", get_name());
        return;
    }

    // Clean existing children before repopulating
    cleanup_row_user_data();
    lv_obj_clean(container);

    // Resolve spacing tokens
    auto get_token = [](const char* name, int fallback) {
        const char* s = lv_xml_get_const(nullptr, name);
        return s ? std::atoi(s) : fallback;
    };
    int space_sm = get_token("space_sm", 6);
    int space_xs = get_token("space_xs", 4);

    // Resolve colors
    lv_color_t accent = theme_manager_get_color("accent");
    lv_color_t text_color = theme_manager_get_color("text");
    lv_color_t success_color = theme_manager_get_color("success");
    lv_color_t danger_color = theme_manager_get_color("danger");

    // Resolve fonts via XML token system
    const char* body_font_name = lv_xml_get_const(nullptr, "font_body");
    const lv_font_t* body_font =
        body_font_name ? lv_xml_get_font(nullptr, body_font_name) : lv_font_get_default();
    const char* icon_font_name = lv_xml_get_const(nullptr, "icon_font_sm");
    const lv_font_t* icon_font =
        icon_font_name ? lv_xml_get_font(nullptr, icon_font_name) : body_font;

    // Get icon codepoints
    const char* check_codepoint = ui_icon::lookup_codepoint("check");
    const char* delete_codepoint = ui_icon::lookup_codepoint("delete");

    for (const auto& id : printer_ids) {
        bool is_active = (id == active_id);
        std::string name = cfg->get<std::string>("/printers/" + id + "/printer_name", id);

        // Row container
        lv_obj_t* row = lv_obj_create(container);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, space_sm, 0);
        lv_obj_set_style_pad_gap(row, space_xs, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Pressed state styling
        lv_obj_set_style_bg_opa(row, 30, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(row, accent, LV_STATE_PRESSED);

        // Checkmark icon (active) or spacer (inactive)
        lv_obj_t* indicator = lv_label_create(row);
        if (is_active && check_codepoint) {
            lv_label_set_text(indicator, check_codepoint);
            lv_obj_set_style_text_font(indicator, icon_font, 0);
            lv_obj_set_style_text_color(indicator, accent, 0);
        } else {
            lv_label_set_text(indicator, "");
            lv_obj_set_style_min_width(indicator, 16, 0);
            lv_obj_set_style_text_font(indicator, icon_font, 0);
        }
        lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(indicator, LV_OBJ_FLAG_EVENT_BUBBLE);

        // Printer name label
        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text(label, name.c_str());
        lv_obj_set_flex_grow(label, 1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(label, body_font, 0);
        lv_obj_set_style_text_color(label, text_color, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);

        // Connection status dot (8x8 circle)
        lv_obj_t* dot = lv_obj_create(row);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, success_color, 0);
        lv_obj_set_style_bg_opa(dot, 255, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_EVENT_BUBBLE);

        // Delete button
        lv_obj_t* del_btn = lv_obj_create(row);
        lv_obj_set_size(del_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(del_btn, 0, 0);
        lv_obj_set_style_border_width(del_btn, 0, 0);
        lv_obj_set_style_pad_all(del_btn, space_xs, 0);
        lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(del_btn, LV_OBJ_FLAG_SCROLLABLE);
        // Hide delete when only 1 printer
        if (printer_ids.size() <= 1) {
            lv_obj_add_flag(del_btn, LV_OBJ_FLAG_HIDDEN);
        }
        auto* del_id = new std::string(id);
        lv_obj_set_user_data(del_btn, del_id);
        lv_obj_add_event_cb(del_btn, on_delete_printer_cb, LV_EVENT_CLICKED, nullptr);

        // Delete icon inside the button
        lv_obj_t* del_icon = lv_label_create(del_btn);
        if (delete_codepoint) {
            lv_label_set_text(del_icon, delete_codepoint);
        }
        lv_obj_set_style_text_font(del_icon, icon_font, 0);
        lv_obj_set_style_text_color(del_icon, danger_color, 0);
        lv_obj_remove_flag(del_icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(del_icon, LV_OBJ_FLAG_EVENT_BUBBLE);

        // Store printer ID on the row for switch handler
        auto* id_copy = new std::string(id);
        lv_obj_set_user_data(row, id_copy);
        lv_obj_add_event_cb(row, on_printer_row_cb, LV_EVENT_CLICKED, nullptr);
    }

    spdlog::debug("[{}] Populated {} printers (active: {})", get_name(), printer_ids.size(),
                  active_id);
}

// =============================================================================
// Action Handlers
// =============================================================================

void PrinterListOverlay::handle_switch_printer(const std::string& printer_id) {
    auto* cfg = Config::get_instance();
    if (cfg && printer_id == cfg->get_active_printer_id()) {
        return; // Already active
    }
    spdlog::info("[{}] Switching to printer '{}'", get_name(), printer_id);
    NavigationManager::instance().trigger_printer_switch(printer_id);
}

void PrinterListOverlay::handle_delete_printer(const std::string& printer_id) {
    auto* cfg = Config::get_instance();
    if (!cfg) {
        return;
    }

    std::string name =
        cfg->get<std::string>("/printers/" + printer_id + "/printer_name", printer_id);

    // Heap-allocate ID for the modal callbacks
    auto* id_copy = new std::string(printer_id);

    std::string msg = "Remove " + name + "? All settings for this printer will be deleted.";

    modal_show_confirmation("Remove Printer", msg.c_str(), ModalSeverity::Error, "Remove",
                            on_delete_confirm_cb, on_delete_cancel_cb, id_copy);
}

void PrinterListOverlay::handle_add_printer() {
    spdlog::info("[{}] Add printer requested", get_name());
    NavigationManager::instance().trigger_add_printer();
}

// =============================================================================
// Cleanup
// =============================================================================

void PrinterListOverlay::cleanup_row_user_data() {
    if (!overlay_root_) {
        return;
    }

    lv_obj_t* container = lv_obj_find_by_name(overlay_root_, "printer_list_container");
    if (!container) {
        return;
    }

    uint32_t count = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* row = lv_obj_get_child(container, i);

        // Clean row user_data (printer ID for switch)
        auto* row_id = static_cast<std::string*>(lv_obj_get_user_data(row));
        delete row_id;
        lv_obj_set_user_data(row, nullptr);

        // Clean delete button user_data within the row
        uint32_t child_count = lv_obj_get_child_count(row);
        for (uint32_t j = 0; j < child_count; ++j) {
            lv_obj_t* child = lv_obj_get_child(row, j);
            auto* child_id = static_cast<std::string*>(lv_obj_get_user_data(child));
            if (child_id) {
                delete child_id;
                lv_obj_set_user_data(child, nullptr);
            }
        }
    }
}

// =============================================================================
// Static Callbacks
// =============================================================================

void PrinterListOverlay::on_add_printer_cb(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterListOverlay] on_add_printer_cb");
    get_printer_list_overlay().handle_add_printer();
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterListOverlay::on_printer_row_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterListOverlay] on_printer_row_cb");

    // Walk up parent chain to find the row with user_data
    // (click target may be a child label due to event bubbling)
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    std::string* id_ptr = nullptr;
    lv_obj_t* obj = target;
    while (obj) {
        id_ptr = static_cast<std::string*>(lv_obj_get_user_data(obj));
        if (id_ptr) {
            break;
        }
        obj = lv_obj_get_parent(obj);
    }

    if (!id_ptr) {
        spdlog::warn("[PrinterListOverlay] Row click with no printer ID");
        return;
    }

    std::string selected_id = *id_ptr;
    get_printer_list_overlay().handle_switch_printer(selected_id);

    LVGL_SAFE_EVENT_CB_END();
}

void PrinterListOverlay::on_delete_printer_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterListOverlay] on_delete_printer_cb");

    // Walk up to find the delete button with user_data
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    std::string* id_ptr = nullptr;
    lv_obj_t* obj = target;
    while (obj) {
        id_ptr = static_cast<std::string*>(lv_obj_get_user_data(obj));
        if (id_ptr) {
            break;
        }
        obj = lv_obj_get_parent(obj);
    }

    if (!id_ptr) {
        spdlog::warn("[PrinterListOverlay] Delete click with no printer ID");
        return;
    }

    std::string printer_id = *id_ptr;
    get_printer_list_overlay().handle_delete_printer(printer_id);

    LVGL_SAFE_EVENT_CB_END();
}

void PrinterListOverlay::on_delete_confirm_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterListOverlay] on_delete_confirm_cb");

    auto* id_ptr = static_cast<std::string*>(lv_event_get_user_data(e));
    if (!id_ptr) {
        return;
    }

    auto* cfg = Config::get_instance();
    if (cfg) {
        bool was_active = (*id_ptr == cfg->get_active_printer_id());
        spdlog::info("[PrinterListOverlay] Removing printer '{}'", *id_ptr);
        cfg->remove_printer(*id_ptr);
        cfg->save();

        if (was_active) {
            auto remaining = cfg->get_printer_ids();
            if (!remaining.empty()) {
                NavigationManager::instance().trigger_printer_switch(remaining.front());
            }
        } else {
            // Defer repopulation out of modal callback to avoid widget mutation mid-event
            helix::ui::queue_update([]() {
                auto* c = Config::get_instance();
                if (c) {
                    auto remaining = c->get_printer_ids();
                    get_printer_state().set_multi_printer_enabled(remaining.size() > 1);
                }
                get_printer_list_overlay().populate_printer_list();
            });
        }
    }

    delete id_ptr;

    LVGL_SAFE_EVENT_CB_END();
}

void PrinterListOverlay::on_delete_cancel_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterListOverlay] on_delete_cancel_cb");
    auto* id_ptr = static_cast<std::string*>(lv_event_get_user_data(e));
    delete id_ptr;
    LVGL_SAFE_EVENT_CB_END();
}

}  // namespace helix::ui
