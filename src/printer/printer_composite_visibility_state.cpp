// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_composite_visibility_state.cpp
 * @brief Composite visibility state management extracted from PrinterState
 *
 * Manages derived visibility subjects that combine plugin installation status
 * with printer capabilities. Used to control visibility of pre-print G-code
 * modification options in the UI.
 *
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_composite_visibility_state.h"

#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterCompositeVisibilityState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterCompositeVisibilityState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterCompositeVisibilityState] Initializing subjects (register_xml={})",
                  register_xml);

    // Composite visibility subjects - all initialize to 0 (hidden by default)
    // These are derived from helix_plugin_installed AND printer_has_* subjects
    INIT_SUBJECT_INT(can_show_bed_mesh, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(can_show_qgl, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(can_show_z_tilt, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(can_show_nozzle_clean, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(can_show_purge_line, 0, subjects_, register_xml);

    // Aggregate: 1 if ANY preprint option is visible (used to hide empty options card)
    INIT_SUBJECT_INT(has_any_preprint_options, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[PrinterCompositeVisibilityState] Subjects initialized successfully");
}

void PrinterCompositeVisibilityState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterCompositeVisibilityState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterCompositeVisibilityState::update_visibility(
    bool plugin_installed, const PrinterCapabilitiesState& capabilities,
    size_t framework_option_count) {
    // Recalculate composite subjects: can_show_X = helix_plugin_installed && printer_has_X
    // These control visibility of pre-print G-code modification options in the UI
    // Only update subjects when the value changes to avoid spurious observer notifications

    auto update_if_changed = [](lv_subject_t* subject, int new_value) {
        if (lv_subject_get_int(subject) != new_value) {
            lv_subject_set_int(subject, new_value);
        }
    };

    // Read capability values from capabilities state and combine with plugin status
    update_if_changed(
        &can_show_bed_mesh_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_bed_mesh_subject()))
            ? 1
            : 0);

    update_if_changed(
        &can_show_qgl_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_qgl_subject())) ? 1
                                                                                             : 0);

    update_if_changed(
        &can_show_z_tilt_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_z_tilt_subject()))
            ? 1
            : 0);

    update_if_changed(&can_show_nozzle_clean_,
                      (plugin_installed &&
                       lv_subject_get_int(capabilities.get_printer_has_nozzle_clean_subject()))
                          ? 1
                          : 0);

    update_if_changed(
        &can_show_purge_line_,
        (plugin_installed && lv_subject_get_int(capabilities.get_printer_has_purge_line_subject()))
            ? 1
            : 0);

    // Aggregate: any preprint option visible (includes timelapse which doesn't need plugin,
    // plus any options declared by the new PrePrintOption framework — necessary so cards
    // showing framework-only options like K2 Plus ai_detect aren't hidden).
    bool any_visible =
        lv_subject_get_int(&can_show_bed_mesh_) || lv_subject_get_int(&can_show_qgl_) ||
        lv_subject_get_int(&can_show_z_tilt_) || lv_subject_get_int(&can_show_nozzle_clean_) ||
        lv_subject_get_int(&can_show_purge_line_) ||
        lv_subject_get_int(capabilities.get_printer_has_timelapse_subject()) ||
        framework_option_count > 0;
    update_if_changed(&has_any_preprint_options_, any_visible ? 1 : 0);

    int cur_bm = lv_subject_get_int(&can_show_bed_mesh_);
    int cur_qgl = lv_subject_get_int(&can_show_qgl_);
    int cur_zt = lv_subject_get_int(&can_show_z_tilt_);
    int cur_nc = lv_subject_get_int(&can_show_nozzle_clean_);
    int cur_pl = lv_subject_get_int(&can_show_purge_line_);
    int cur_any = lv_subject_get_int(&has_any_preprint_options_);

    if (!last_log_state_initialized_ || cur_bm != last_bed_mesh_ || cur_qgl != last_qgl_ ||
        cur_zt != last_z_tilt_ || cur_nc != last_nozzle_clean_ || cur_pl != last_purge_line_ ||
        cur_any != last_any_ || plugin_installed != last_plugin_) {
        spdlog::debug("[PrinterCompositeVisibilityState] Visibility updated: bed_mesh={}, qgl={}, "
                      "z_tilt={}, nozzle_clean={}, purge_line={}, any={} (plugin={})",
                      cur_bm, cur_qgl, cur_zt, cur_nc, cur_pl, cur_any, plugin_installed);
        last_bed_mesh_ = cur_bm;
        last_qgl_ = cur_qgl;
        last_z_tilt_ = cur_zt;
        last_nozzle_clean_ = cur_nc;
        last_purge_line_ = cur_pl;
        last_any_ = cur_any;
        last_plugin_ = plugin_installed;
        last_log_state_initialized_ = true;
    }
}

} // namespace helix
