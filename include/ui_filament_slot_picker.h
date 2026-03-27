// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_mapper.h"

#include <functional>
#include <lvgl.h>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief Context-menu style slot picker for filament mapping
 *
 * Shows a scrollable list of available AMS/toolchanger slots near the
 * click point. Tap a slot to select it immediately (no OK/Cancel).
 * Tap the backdrop to dismiss without changing the selection.
 *
 * Built entirely in C++ (no XML component) since all content is dynamic.
 */
class FilamentSlotPicker {
  public:
    struct Selection {
        int slot_index = -1;
        int backend_index = -1;
        bool is_auto = false;
    };

    using SelectCallback = std::function<void(const Selection&)>;

    FilamentSlotPicker() = default;
    ~FilamentSlotPicker();

    // Non-copyable
    FilamentSlotPicker(const FilamentSlotPicker&) = delete;
    FilamentSlotPicker& operator=(const FilamentSlotPicker&) = delete;

    /**
     * @brief Show the slot picker near a click point
     * @param parent Screen to attach backdrop to (typically lv_screen_active())
     * @param click_point Display-coordinate click point for positioning
     * @param tool_index Tool number (for header title)
     * @param expected_material G-code expected material for mismatch warnings
     * @param slots Available AMS/toolchanger slots
     * @param current Current selection (for highlighting)
     * @param on_select Callback fired immediately when a slot is tapped
     */
    void show(lv_obj_t* parent, lv_point_t click_point,
              int tool_index, const std::string& expected_material,
              const std::vector<helix::AvailableSlot>& slots,
              const Selection& current,
              SelectCallback on_select);

    void hide();
    [[nodiscard]] bool is_visible() const { return backdrop_ != nullptr; }

  private:
    void create_header(lv_obj_t* card, int tool_index, const std::string& material);
    void create_slot_row(lv_obj_t* list, int index, const helix::AvailableSlot& slot);
    void select(const Selection& sel);
    void position_card(lv_obj_t* card, lv_point_t click_point);

    lv_obj_t* backdrop_ = nullptr;
    SelectCallback on_select_;
    std::vector<helix::AvailableSlot> slots_;
    std::string expected_material_;
    Selection current_selection_;
};

} // namespace helix::ui
