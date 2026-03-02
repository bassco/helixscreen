// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

namespace helix::ui {

/**
 * @brief Resize a fan arc to fit its parent card
 *
 * Finds "dial_container" and "dial_arc" children by name within card_root,
 * computes optimal square arc size from available content dimensions,
 * and scales the arc track width at an 11:1 ratio.
 *
 * @param card_root Root widget of a fan_dial or fan_status_card
 */
void fan_arc_resize_to_fit(lv_obj_t* card_root);

/**
 * @brief Attach a SIZE_CHANGED callback to a fan card that auto-resizes its arc
 *
 * One-liner to make any fan card responsive. Attaches a stateless
 * SIZE_CHANGED event handler that calls fan_arc_resize_to_fit().
 * Call at most once per card_root (duplicates register extra callbacks).
 *
 * @param card_root Root widget of a fan_dial or fan_status_card
 */
void fan_arc_attach_auto_resize(lv_obj_t* card_root);

} // namespace helix::ui
