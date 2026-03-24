// SPDX-License-Identifier: GPL-3.0-or-later

#include "nozzle_temps_widget.h"

#include "ui_update_queue.h"

#include "app_globals.h"
#include "observer_factory.h"
#include "panel_widget_registry.h"
#include "printer_state.h"
#include "theme_manager.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace helix {

void register_nozzle_temps_widget() {
    register_widget_factory("nozzle_temps", [](const std::string&) {
        auto& ps = get_printer_state();
        return std::make_unique<NozzleTempsWidget>(ps);
    });
}

} // namespace helix

using namespace helix;

NozzleTempsWidget::NozzleTempsWidget(PrinterState& printer_state)
    : printer_state_(printer_state) {}

NozzleTempsWidget::~NozzleTempsWidget() {
    detach();
}

void NozzleTempsWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    *alive_ = true;

    rebuild_rows();

    // Observe extruder version changes to rebuild rows when tools are discovered
    std::weak_ptr<bool> weak_alive = alive_;
    version_observer_ = helix::ui::observe_int_sync<NozzleTempsWidget>(
        printer_state_.get_extruder_version_subject(), this,
        [weak_alive](NozzleTempsWidget* self, int) {
            if (weak_alive.expired())
                return;
            self->rebuild_rows();
        });

    spdlog::debug("[NozzleTempsWidget] Attached with {} extruder rows", extruder_rows_.size());
}

void NozzleTempsWidget::detach() {
    *alive_ = false;
    clear_rows();
    // version_observer_ already reset in clear_rows() under freeze
    widget_obj_ = nullptr;
    parent_screen_ = nullptr;
}

void NozzleTempsWidget::clear_rows() {
    auto freeze = helix::ui::UpdateQueue::instance().scoped_freeze();
    helix::ui::UpdateQueue::instance().drain();

    version_observer_.reset();
    extruder_rows_.clear();
    bed_temp_observer_.reset();
    bed_target_observer_.reset();

    auto* container = widget_obj_ ? lv_obj_find_by_name(widget_obj_, "nozzle_temps_container")
                                  : nullptr;
    if (container)
        lv_obj_clean(container);

    bed_row_ = nullptr;
    bed_temp_label_ = nullptr;
    bed_target_label_ = nullptr;
    bed_progress_bar_ = nullptr;
    cached_bed_temp_ = 0;
    cached_bed_target_ = 0;
}

void NozzleTempsWidget::rebuild_rows() {
    clear_rows();

    auto* container = widget_obj_ ? lv_obj_find_by_name(widget_obj_, "nozzle_temps_container")
                                  : nullptr;
    if (!container) {
        spdlog::warn("[NozzleTempsWidget] Container not found in XML");
        return;
    }

    std::weak_ptr<bool> weak_alive = alive_;

    // Create extruder rows ordered by ToolState tools
    const auto& tools = ToolState::instance().tools();
    for (const auto& tool : tools) {
        if (!tool.extruder_name)
            continue;

        ExtruderRow row;
        row.name = *tool.extruder_name;
        create_extruder_row(container, row);

        // Observe per-extruder temp subject with lifetime token
        lv_subject_t* temp_subj =
            printer_state_.get_extruder_temp_subject(row.name, row.temp_lifetime);
        lv_subject_t* target_subj =
            printer_state_.get_extruder_target_subject(row.name, row.target_lifetime);

        if (temp_subj) {
            row.cached_temp = lv_subject_get_int(temp_subj);
            auto* temp_lbl = row.temp_label;
            auto* target_lbl = row.target_label;
            auto* bar = row.progress_bar;
            row.temp_observer = helix::ui::observe_int_sync<NozzleTempsWidget>(
                temp_subj, this,
                [weak_alive, idx = extruder_rows_.size(), temp_lbl, target_lbl,
                 bar](NozzleTempsWidget* self, int temp) {
                    if (weak_alive.expired())
                        return;
                    if (idx < self->extruder_rows_.size()) {
                        self->extruder_rows_[idx].cached_temp = temp;
                        self->update_row_display(temp_lbl, target_lbl, bar, temp,
                                                 self->extruder_rows_[idx].cached_target, false);
                    }
                },
                row.temp_lifetime);
        }

        if (target_subj) {
            row.cached_target = lv_subject_get_int(target_subj);
            auto* temp_lbl = row.temp_label;
            auto* target_lbl = row.target_label;
            auto* bar = row.progress_bar;
            row.target_observer = helix::ui::observe_int_sync<NozzleTempsWidget>(
                target_subj, this,
                [weak_alive, idx = extruder_rows_.size(), temp_lbl, target_lbl,
                 bar](NozzleTempsWidget* self, int target) {
                    if (weak_alive.expired())
                        return;
                    if (idx < self->extruder_rows_.size()) {
                        self->extruder_rows_[idx].cached_target = target;
                        self->update_row_display(temp_lbl, target_lbl, bar,
                                                 self->extruder_rows_[idx].cached_temp, target,
                                                 false);
                    }
                },
                row.target_lifetime);
        }

        // Initial display update
        update_row_display(row.temp_label, row.target_label, row.progress_bar, row.cached_temp,
                           row.cached_target, false);

        extruder_rows_.push_back(std::move(row));
    }

    // Bed row at the end
    create_bed_row(container);

    // Bed observers (static subjects, no lifetime token needed)
    lv_subject_t* bed_temp_subj = printer_state_.get_bed_temp_subject();
    lv_subject_t* bed_target_subj = printer_state_.get_bed_target_subject();

    if (bed_temp_subj) {
        cached_bed_temp_ = lv_subject_get_int(bed_temp_subj);
        bed_temp_observer_ = helix::ui::observe_int_sync<NozzleTempsWidget>(
            bed_temp_subj, this,
            [weak_alive](NozzleTempsWidget* self, int temp) {
                if (weak_alive.expired())
                    return;
                self->cached_bed_temp_ = temp;
                self->update_row_display(self->bed_temp_label_, self->bed_target_label_,
                                         self->bed_progress_bar_, temp, self->cached_bed_target_,
                                         true);
            });
    }

    if (bed_target_subj) {
        cached_bed_target_ = lv_subject_get_int(bed_target_subj);
        bed_target_observer_ = helix::ui::observe_int_sync<NozzleTempsWidget>(
            bed_target_subj, this,
            [weak_alive](NozzleTempsWidget* self, int target) {
                if (weak_alive.expired())
                    return;
                self->cached_bed_target_ = target;
                self->update_row_display(self->bed_temp_label_, self->bed_target_label_,
                                         self->bed_progress_bar_, self->cached_bed_temp_, target,
                                         true);
            });
    }

    update_row_display(bed_temp_label_, bed_target_label_, bed_progress_bar_, cached_bed_temp_,
                       cached_bed_target_, true);

    // Re-attach version observer (cleared in clear_rows)
    version_observer_ = helix::ui::observe_int_sync<NozzleTempsWidget>(
        printer_state_.get_extruder_version_subject(), this,
        [weak_alive](NozzleTempsWidget* self, int) {
            if (weak_alive.expired())
                return;
            self->rebuild_rows();
        });

    spdlog::debug("[NozzleTempsWidget] Rebuilt with {} extruder rows + bed", extruder_rows_.size());
}

void NozzleTempsWidget::create_extruder_row(lv_obj_t* container, ExtruderRow& row) {
    // Outer column container for text row + progress bar
    lv_obj_t* col = lv_obj_create(container);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_gap(col, theme_manager_get_spacing("space_xxs"), 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    row.row_obj = col;

    // Horizontal row: tool label + current temp + target
    lv_obj_t* text_row = lv_obj_create(col);
    lv_obj_set_size(text_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(text_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(text_row, 0, 0);
    lv_obj_set_style_pad_gap(text_row, theme_manager_get_spacing("space_xs"), 0);
    lv_obj_set_style_bg_opa(text_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_row, 0, 0);
    lv_obj_remove_flag(text_row, LV_OBJ_FLAG_SCROLLABLE);

    // Tool label (e.g. "T0", "T1")
    std::string tool_name = ToolState::instance().tool_name_for_extruder(row.name);
    if (tool_name.empty())
        tool_name = row.name;

    lv_obj_t* tool_label = lv_label_create(text_row);
    lv_label_set_text(tool_label, tool_name.c_str());
    lv_label_set_long_mode(tool_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(tool_label, theme_manager_get_color("primary"), 0);
    const lv_font_t* small_font = theme_manager_get_font("font_small");
    if (small_font)
        lv_obj_set_style_text_font(tool_label, small_font, 0);
    int label_w = theme_manager_get_spacing("space_xl") + theme_manager_get_spacing("space_xxs");
    lv_obj_set_width(tool_label, label_w);
    lv_obj_set_style_min_width(tool_label, label_w, 0);
    lv_obj_set_style_text_align(tool_label, LV_TEXT_ALIGN_LEFT, 0);

    // Current temperature
    row.temp_label = lv_label_create(text_row);
    lv_label_set_text(row.temp_label, "0\xC2\xB0");
    if (small_font)
        lv_obj_set_style_text_font(row.temp_label, small_font, 0);
    lv_obj_set_style_text_color(row.temp_label, theme_manager_get_color("text"), 0);

    // Target label (right-justified)
    row.target_label = lv_label_create(text_row);
    lv_label_set_text(row.target_label, "off");
    lv_obj_set_flex_grow(row.target_label, 1);
    lv_obj_set_style_text_align(row.target_label, LV_TEXT_ALIGN_RIGHT, 0);
    const lv_font_t* xs_font = theme_manager_get_font("font_xs");
    if (xs_font)
        lv_obj_set_style_text_font(row.target_label, xs_font, 0);
    lv_obj_set_style_text_color(row.target_label, theme_manager_get_color("text_muted"), 0);

    // Progress bar
    row.progress_bar = lv_bar_create(col);
    lv_obj_set_size(row.progress_bar, LV_PCT(100), 3);
    lv_bar_set_range(row.progress_bar, 0, 100);
    lv_bar_set_value(row.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(row.progress_bar, theme_manager_get_color("surface"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row.progress_bar, theme_manager_get_color("text_muted"),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(row.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(row.progress_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(row.progress_bar, 1, LV_PART_INDICATOR);
}

void NozzleTempsWidget::create_bed_row(lv_obj_t* container) {
    // Divider above bed row
    lv_obj_t* divider = lv_obj_create(container);
    lv_obj_set_size(divider, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(divider, theme_manager_get_color("border"), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    // Outer column container for text row + progress bar
    lv_obj_t* col = lv_obj_create(container);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_gap(col, theme_manager_get_spacing("space_xxs"), 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    bed_row_ = col;

    // Horizontal row: label + current temp + target
    lv_obj_t* text_row = lv_obj_create(col);
    lv_obj_set_size(text_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(text_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(text_row, 0, 0);
    lv_obj_set_style_pad_gap(text_row, theme_manager_get_spacing("space_xs"), 0);
    lv_obj_set_style_bg_opa(text_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_row, 0, 0);
    lv_obj_remove_flag(text_row, LV_OBJ_FLAG_SCROLLABLE);

    // "Bed" label — clipped to prevent wrapping in tight layouts
    lv_obj_t* bed_label = lv_label_create(text_row);
    lv_label_set_text(bed_label, "Bed");
    lv_label_set_long_mode(bed_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(bed_label, theme_manager_get_color("danger"), 0);
    const lv_font_t* small_font = theme_manager_get_font("font_small");
    if (small_font)
        lv_obj_set_style_text_font(bed_label, small_font, 0);
    int bed_label_w = theme_manager_get_spacing("space_xl") + theme_manager_get_spacing("space_xxs");
    lv_obj_set_width(bed_label, bed_label_w);
    lv_obj_set_style_min_width(bed_label, bed_label_w, 0);
    lv_obj_set_style_text_align(bed_label, LV_TEXT_ALIGN_LEFT, 0);

    // Current temperature
    bed_temp_label_ = lv_label_create(text_row);
    lv_label_set_text(bed_temp_label_, "0\xC2\xB0");
    if (small_font)
        lv_obj_set_style_text_font(bed_temp_label_, small_font, 0);
    lv_obj_set_style_text_color(bed_temp_label_, theme_manager_get_color("text"), 0);

    // Target label (right-justified)
    bed_target_label_ = lv_label_create(text_row);
    lv_label_set_text(bed_target_label_, "off");
    lv_obj_set_flex_grow(bed_target_label_, 1);
    lv_obj_set_style_text_align(bed_target_label_, LV_TEXT_ALIGN_RIGHT, 0);
    const lv_font_t* xs_font = theme_manager_get_font("font_xs");
    if (xs_font)
        lv_obj_set_style_text_font(bed_target_label_, xs_font, 0);
    lv_obj_set_style_text_color(bed_target_label_, theme_manager_get_color("text_muted"), 0);

    // Progress bar (bed uses danger color accent)
    bed_progress_bar_ = lv_bar_create(col);
    lv_obj_set_size(bed_progress_bar_, LV_PCT(100), 3);
    lv_bar_set_range(bed_progress_bar_, 0, 100);
    lv_bar_set_value(bed_progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bed_progress_bar_, theme_manager_get_color("surface"), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bed_progress_bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bed_progress_bar_, theme_manager_get_color("text_muted"),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bed_progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bed_progress_bar_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bed_progress_bar_, 1, LV_PART_INDICATOR);
}

void NozzleTempsWidget::update_row_display(lv_obj_t* temp_label, lv_obj_t* target_label,
                                           lv_obj_t* progress_bar, int temp_centi,
                                           int target_centi, bool is_bed) {
    if (!temp_label || !target_label || !progress_bar)
        return;

    float temp = temp_centi / 10.0f;
    float target = target_centi / 10.0f;
    (void)target; // used only in format string

    lv_label_set_text_fmt(temp_label, "%.0f\xC2\xB0", temp);

    if (target_centi > 0) {
        lv_label_set_text_fmt(target_label, "\xE2\x86\x92 %.0f\xC2\xB0",
                              target_centi / 10.0f);
        int progress = (temp_centi * 100) / target_centi;
        progress = std::clamp(progress, 0, 100);
        lv_bar_set_value(progress_bar, progress, LV_ANIM_ON);

        bool at_temp = std::abs(temp_centi - target_centi) <= 20;
        lv_color_t bar_color;
        if (at_temp) {
            bar_color = theme_manager_get_color("success");
        } else if (is_bed) {
            bar_color = theme_manager_get_color("danger");
        } else {
            bar_color = theme_manager_get_color("warning");
        }
        lv_obj_set_style_bg_color(progress_bar, bar_color, LV_PART_INDICATOR);
    } else {
        lv_label_set_text(target_label, "off");
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progress_bar, theme_manager_get_color("text_muted"),
                                  LV_PART_INDICATOR);
    }
}
