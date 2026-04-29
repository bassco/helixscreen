// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pre_print_option.h"
#include "ui_observer_guard.h"

#include <functional>
#include <lvgl.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @file ui_pre_print_options_renderer.h
 * @brief Renders the per-print toggle list on the print-detail panel.
 *
 * Owns the per-option `lv_subject_t` state (one int subject per option in the
 * active printer's `PrePrintOptionSet`). Builds a row for each option with a
 * label + ui_switch, grouped by category with sub-headers ("Mechanical setup",
 * "Print quality", "Monitoring"). Visibility per row is bound to a
 * caller-supplied `can_show_*` subject so a row only shows when both
 *   1. the printer's database entry advertises the option, AND
 *   2. macro analysis confirms the START_PRINT macro performs that operation.
 *
 * The renderer does NOT decide visibility itself — the caller passes a lookup
 * function that maps option id → existing PrinterState `can_show_*` subject.
 *
 * State subject lifecycle: subjects are heap-allocated and owned by the
 * renderer. They live until `clear()` runs (or the renderer is destroyed),
 * which happens AFTER the rows themselves are deleted via
 * `safe_clean_children`. Rows hold `bind_state_if_eq` observers on these
 * subjects; deleting the rows first removes the observers.
 */
class PrePrintOptionsRenderer {
  public:
    /**
     * @brief Optional lookup for an option's row visibility (can_show_*).
     *
     * Returning nullptr leaves the row visible unconditionally — used for
     * options whose visibility is purely driven by the option set being
     * present in the database (e.g. timelapse or future framework-only
     * options).
     */
    using VisibilitySubjectLookup = std::function<lv_subject_t*(const std::string& id)>;

    /**
     * @brief Callback invoked when a switch toggles. Receives the option id
     *        and the new state (1 = enabled / checked).
     */
    using OnToggleCallback = std::function<void(const std::string& id, int new_state)>;

    PrePrintOptionsRenderer() = default;
    ~PrePrintOptionsRenderer();

    PrePrintOptionsRenderer(const PrePrintOptionsRenderer&) = delete;
    PrePrintOptionsRenderer& operator=(const PrePrintOptionsRenderer&) = delete;
    PrePrintOptionsRenderer(PrePrintOptionsRenderer&&) = delete;
    PrePrintOptionsRenderer& operator=(PrePrintOptionsRenderer&&) = delete;

    /**
     * @brief Replace the contents of `container` with rows for `option_set`.
     *
     * Existing children of `container` are deleted via `safe_clean_children`
     * before new rows are added. If the option set is empty, the container is
     * left empty (no rows, no headers).
     *
     * Per-option state subjects are initialized to the option's
     * `default_enabled` value. Re-calling `populate()` resets state to
     * defaults — caller must persist any user toggles externally if needed.
     *
     * @param container Parent widget that will hold the rows
     * @param option_set Options to render (sorted by category/order on input)
     * @param visibility_lookup Callback returning the can_show_* subject for
     *        each id, or nullptr to skip visibility binding for that option
     * @param on_toggle Callback fired when any switch changes state
     */
    void populate(lv_obj_t* container, const PrePrintOptionSet& option_set,
                  const VisibilitySubjectLookup& visibility_lookup, OnToggleCallback on_toggle);

    /**
     * @brief Drop all rows and subjects (e.g. on panel close / printer change).
     */
    void clear();

    /**
     * @brief Read the current toggle state for `id`.
     *
     * @return The subject value (0 or 1) for the named option, or
     *         `default_if_missing` when the option is not currently rendered.
     */
    [[nodiscard]] int get_state(const std::string& id, int default_if_missing = 0) const;

    /**
     * @brief Set toggle state for `id`. No-op if id is not present.
     */
    void set_state(const std::string& id, int new_state);

    /**
     * @brief Number of option rows currently rendered. Excludes sub-headers.
     */
    [[nodiscard]] size_t row_count() const {
        return rows_.size();
    }

    /**
     * @brief List the ids of the rendered option rows in display order.
     */
    [[nodiscard]] std::vector<std::string> rendered_ids() const;

    /**
     * @brief Look up the row widget for `id`. Returns nullptr if not present.
     *        Test/diagnostic helper.
     */
    [[nodiscard]] lv_obj_t* get_row(const std::string& id) const;

    /**
     * @brief Look up the switch widget for `id`. Returns nullptr if not
     *        present. Test/diagnostic helper.
     */
    [[nodiscard]] lv_obj_t* get_switch(const std::string& id) const;

  private:
    struct OptionRow {
        std::string id;
        lv_obj_t* row = nullptr;
        lv_obj_t* switch_widget = nullptr;
        std::unique_ptr<lv_subject_t> state_subject;
    };

    static const char* category_translation_key(PrePrintCategory category);
    /// Look up the i18n string for an option's label, falling back to a
    /// humanized version of the id when no `label_key` is set in the DB.
    static std::string label_for(const PrePrintOption& opt);

    void make_subheader(lv_obj_t* container, PrePrintCategory category);
    void make_row(lv_obj_t* container, const PrePrintOption& opt,
                  const VisibilitySubjectLookup& visibility_lookup);

    static void on_switch_value_changed(lv_event_t* e);

    std::vector<OptionRow> rows_;
    OnToggleCallback on_toggle_;
};

} // namespace helix::ui
