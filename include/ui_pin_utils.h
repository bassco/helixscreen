// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>

namespace helix::ui {

/**
 * Update PIN dot indicators -- set filled/empty style based on digit count.
 * @param parent       Parent object to search for dot children
 * @param dot_prefix   Name prefix, e.g. "lock_dot_" or "pin_dot_"
 * @param max_dots     Total number of dots (e.g. 6)
 * @param filled_count Number of dots to show as filled
 */
void update_pin_dots(lv_obj_t* parent, const char* dot_prefix, int max_dots, int filled_count);

/**
 * Show an error message on a named label using opacity.
 * @param parent     Parent object containing the label
 * @param label_name Name of the label object (e.g. "lock_error_label")
 * @param text       Error text to display (if nullptr, keeps existing text)
 */
void show_pin_error(lv_obj_t* parent, const char* label_name, const char* text);

/**
 * Hide the error label by setting opacity to transparent and clearing text.
 * @param parent     Parent object containing the label
 * @param label_name Name of the label object
 */
void hide_pin_error(lv_obj_t* parent, const char* label_name);

} // namespace helix::ui
