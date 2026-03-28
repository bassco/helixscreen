// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_sound.h
 * @brief Sound Settings overlay - volume, themes, UI sounds
 *
 * This overlay allows users to configure:
 * - Master sounds toggle
 * - Master volume slider (attenuates all sound output)
 * - UI sounds toggle (button taps, navigation)
 * - Sound theme selection
 * - Test beep playback
 *
 * @pattern Overlay (lazy init)
 * @threading Main thread only
 *
 * @see SettingsManager for persistence
 * @see SoundManager for theme/playback
 * @see SoundSequencer for volume attenuation
 */

#pragma once

#include "lvgl/lvgl.h"
#include "overlay_base.h"

namespace helix::settings {

/**
 * @class SoundSettingsOverlay
 * @brief Overlay for configuring sound-related settings
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::settings::get_sound_settings_overlay();
 * overlay.show(parent_screen);
 * @endcode
 */
class SoundSettingsOverlay : public OverlayBase {
  public:
    SoundSettingsOverlay();
    ~SoundSettingsOverlay() override;

    //
    // === OverlayBase Interface ===
    //

    void init_subjects() override;
    void register_callbacks() override;

    const char* get_name() const override {
        return "Sound Settings";
    }

    void on_activate() override;
    void on_deactivate() override;

    //
    // === UI Creation ===
    //

    lv_obj_t* create(lv_obj_t* parent) override;
    void show(lv_obj_t* parent_screen);

    bool is_created() const {
        return overlay_root_ != nullptr;
    }

    //
    // === Event Handlers (public for static callbacks) ===
    //

    void handle_sounds_changed(bool enabled);
    void handle_ui_sounds_changed(bool enabled);
    void handle_volume_changed(int value);
    void handle_sound_theme_changed(int index);
    void handle_test_beep();
    void handle_test_tracker();

  private:
    //
    // === Internal Methods ===
    //

    void init_sounds_toggle();
    void init_volume_slider();
    void init_sound_theme_dropdown();

    //
    // === State ===
    //

    /// Subject for volume value label binding
    lv_subject_t volume_value_subject_;
    char volume_value_buf_[8]; // e.g., "100%"

    //
    // === Static Callbacks ===
    //

    static void on_sounds_changed(lv_event_t* e);
    static void on_ui_sounds_changed(lv_event_t* e);
    static void on_volume_changed(lv_event_t* e);
    static void on_volume_released(lv_event_t* e);
    static void on_sound_theme_changed(lv_event_t* e);
    static void on_test_beep(lv_event_t* e);
    static void on_test_tracker(lv_event_t* e);
};

/**
 * @brief Global instance accessor
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 *
 * @return Reference to singleton SoundSettingsOverlay
 */
SoundSettingsOverlay& get_sound_settings_overlay();

} // namespace helix::settings
