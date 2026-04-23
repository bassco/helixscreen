// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC
//
// Touch jitter filter — suppresses small coordinate changes during
// stationary taps to prevent noisy touch controllers (e.g., Goodix GT9xx)
// from triggering LVGL scroll detection.

#pragma once

#include <lvgl.h>

struct TouchJitterFilter {
    /// Threshold in screen pixels (squared for fast comparison). 0 = disabled.
    int threshold_sq = 0;
    int last_x = 0;
    int last_y = 0;
    bool tracking = false;
    bool broken_out = false; ///< True once intentional movement detected; disables filtering

    /// Post-scroll click guard — suppresses ghost presses that occur when
    /// lifting a finger from a capacitive touchscreen after scrolling.
    /// The contact area changes during lift, causing some controllers to
    /// briefly report release→repress before the final release.
    ///
    /// The cooldown window is configurable via /input/scroll_guard_cooldown_ms
    /// (or HELIX_SCROLL_GUARD_COOLDOWN_MS) in case 80 ms is insufficient for a
    /// given touch controller.
    static constexpr uint32_t SCROLL_GUARD_COOLDOWN_MS_DEFAULT = 80;
    uint32_t scroll_guard_cooldown_ms = SCROLL_GUARD_COOLDOWN_MS_DEFAULT;
    uint32_t scroll_release_tick = 0;  ///< lv_tick_get() when scroll touch ended
    bool scroll_guard_enabled = false; ///< Enable post-scroll click guard
    bool was_scrolling = false; ///< True if the touch that just ended had breakout (drag/scroll)

    /// Apply jitter filtering to touch coordinates.
    /// Suppresses movement within the dead zone until the first intentional movement
    /// exceeds the threshold. After breakout, all coordinates pass through unfiltered
    /// for the rest of the touch (smooth scrolling/dragging). On release, snaps to
    /// last stable position and resets for the next touch.
    void apply(lv_indev_state_t state, int32_t& x, int32_t& y) {
        if (threshold_sq <= 0)
            return;

        if (state == LV_INDEV_STATE_PRESSED) {
            if (!tracking) {
                last_x = x;
                last_y = y;
                tracking = true;
                broken_out = false;
            } else if (!broken_out) {
                int dx = x - last_x;
                int dy = y - last_y;
                if (dx * dx + dy * dy <= threshold_sq) {
                    x = last_x;
                    y = last_y;
                } else {
                    broken_out = true;
                }
            }
            // After breakout: pass through unfiltered (smooth drag/scroll)
        } else {
            if (tracking) {
                if (!broken_out) {
                    // Tap (never broke out): snap to initial press position
                    x = last_x;
                    y = last_y;
                }
                // Record whether this touch involved scrolling/dragging
                was_scrolling = broken_out;
                tracking = false;
                broken_out = false;
            }
        }
    }

    /// Apply post-scroll click guard. Call AFTER apply().
    /// Returns true if the event should be suppressed (converted to RELEASED).
    bool guard_post_scroll(lv_indev_state_t& state) {
        if (!scroll_guard_enabled)
            return false;

        if (state == LV_INDEV_STATE_RELEASED && was_scrolling) {
            // Touch that involved scrolling just ended — start cooldown
            scroll_release_tick = lv_tick_get();
            was_scrolling = false;
            return false;
        }

        if (state == LV_INDEV_STATE_PRESSED && scroll_release_tick > 0) {
            uint32_t elapsed = lv_tick_elaps(scroll_release_tick);
            if (elapsed < scroll_guard_cooldown_ms) {
                // Ghost press during cooldown — suppress
                state = LV_INDEV_STATE_RELEASED;
                return true;
            }
            // Cooldown expired — allow the press
            scroll_release_tick = 0;
        }

        return false;
    }
};
