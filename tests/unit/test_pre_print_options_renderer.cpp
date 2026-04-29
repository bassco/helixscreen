// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_pre_print_options_renderer.h"

#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"

#include "pre_print_option.h"
#include "printer_detector.h"

using namespace helix::ui;

namespace {

/// Walk a container's children top-to-bottom and return ids of switch widgets
/// found, in display order. Helper for asserting "the rows came out in the
/// expected sequence" without coupling to LVGL widget pointers.
std::vector<std::string> child_widget_classes(lv_obj_t* container) {
    std::vector<std::string> classes;
    uint32_t n = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* child = lv_obj_get_child(container, i);
        const lv_obj_class_t* cls = lv_obj_get_class(child);
        if (cls == &lv_label_class) {
            classes.emplace_back("label");
        } else {
            // Anything else (rows, switches, etc.) — track as "row" since
            // every non-label child in our renderer output is a row.
            classes.emplace_back("row");
        }
    }
    return classes;
}

/// Count direct child labels at the top level of a container.
size_t count_subheaders(lv_obj_t* container) {
    size_t count = 0;
    uint32_t n = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < n; ++i) {
        if (lv_obj_get_class(lv_obj_get_child(container, i)) == &lv_label_class) {
            ++count;
        }
    }
    return count;
}

/// Build an option set with options across multiple categories so we can
/// exercise the subheader-grouping path. Mirrors the JSON shape that
/// `parse_pre_print_option_set` accepts but builds it directly to keep the
/// test independent of printer_database.json drift.
PrePrintOptionSet make_multi_category_set() {
    PrePrintOptionSet s;
    s.macro_name = "START_PRINT";

    PrePrintOption mech;
    mech.id = "bed_mesh";
    mech.category = PrePrintCategory::Mechanical;
    mech.order = 10;
    mech.default_enabled = true;
    mech.strategy_kind = PrePrintStrategyKind::MacroParam;
    mech.strategy = PrePrintStrategyMacroParam{"SKIP_BED_MESH", "0", "1", "0"};

    PrePrintOption qual;
    qual.id = "nozzle_clean";
    qual.category = PrePrintCategory::Quality;
    qual.order = 10;
    qual.default_enabled = false;
    qual.strategy_kind = PrePrintStrategyKind::MacroParam;
    qual.strategy = PrePrintStrategyMacroParam{"SKIP_NOZZLE_CLEAN", "0", "1", "0"};

    PrePrintOption mon;
    mon.id = "ai_detect";
    mon.category = PrePrintCategory::Monitoring;
    mon.order = 10;
    mon.default_enabled = false;
    mon.strategy_kind = PrePrintStrategyKind::PreStartGcode;
    mon.strategy = PrePrintStrategyPreStartGcode{"LOAD_AI_RUN SWITCH={value}"};

    s.options = {mech, qual, mon};
    return s;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "PrePrintOptionsRenderer: empty option set leaves container empty",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());

    PrePrintOptionSet empty;
    renderer.populate(container, empty, nullptr, nullptr);

    REQUIRE(renderer.row_count() == 0);
    REQUIRE(lv_obj_get_child_count(container) == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "PrePrintOptionsRenderer: single-category set has no subheader",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());

    PrePrintOptionSet set;
    set.macro_name = "START_PRINT";

    PrePrintOption opt;
    opt.id = "bed_mesh";
    opt.category = PrePrintCategory::Mechanical;
    opt.order = 10;
    opt.default_enabled = true;
    opt.strategy_kind = PrePrintStrategyKind::MacroParam;
    opt.strategy = PrePrintStrategyMacroParam{"SKIP_LEVELING", "0", "1", "0"};
    set.options.push_back(opt);

    renderer.populate(container, set, nullptr, nullptr);

    REQUIRE(renderer.row_count() == 1);
    auto rendered = renderer.rendered_ids();
    REQUIRE(rendered.size() == 1);
    REQUIRE(rendered[0] == "bed_mesh");

    // 1 subheader (label) + 1 row container = 2 children.
    REQUIRE(lv_obj_get_child_count(container) == 2);
    REQUIRE(count_subheaders(container) == 1);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: multi-category set emits subheader per category",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());

    auto set = make_multi_category_set();
    renderer.populate(container, set, nullptr, nullptr);

    REQUIRE(renderer.row_count() == 3);
    REQUIRE(renderer.rendered_ids() == std::vector<std::string>{"bed_mesh", "nozzle_clean",
                                                                "ai_detect"});

    // 3 subheaders + 3 rows = 6 children.
    REQUIRE(lv_obj_get_child_count(container) == 6);
    REQUIRE(count_subheaders(container) == 3);

    // Display order: subheader, row, subheader, row, subheader, row.
    auto classes = child_widget_classes(container);
    REQUIRE(classes
            == std::vector<std::string>{"label", "row", "label", "row", "label", "row"});
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: state subjects initialized from default_enabled",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());

    auto set = make_multi_category_set();
    renderer.populate(container, set, nullptr, nullptr);

    // bed_mesh: default_enabled=true -> 1
    REQUIRE(renderer.get_state("bed_mesh") == 1);
    // nozzle_clean: default_enabled=false -> 0
    REQUIRE(renderer.get_state("nozzle_clean") == 0);
    // ai_detect: default_enabled=false -> 0
    REQUIRE(renderer.get_state("ai_detect") == 0);

    // Unknown id: returns the supplied default (0 by default).
    REQUIRE(renderer.get_state("does_not_exist") == 0);
    REQUIRE(renderer.get_state("does_not_exist", 42) == 42);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: visibility lookup hides row when subject is 0",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());

    // Single-option set with a paired visibility subject.
    PrePrintOptionSet set;
    set.macro_name = "START_PRINT";
    PrePrintOption opt;
    opt.id = "bed_mesh";
    opt.category = PrePrintCategory::Mechanical;
    opt.order = 10;
    opt.default_enabled = true;
    opt.strategy_kind = PrePrintStrategyKind::MacroParam;
    opt.strategy = PrePrintStrategyMacroParam{"SKIP_LEVELING", "0", "1", "0"};
    set.options.push_back(opt);

    lv_subject_t can_show{};
    lv_subject_init_int(&can_show, 1); // start visible

    auto vis_lookup = [&](const std::string& id) -> lv_subject_t* {
        return id == "bed_mesh" ? &can_show : nullptr;
    };

    renderer.populate(container, set, vis_lookup, nullptr);
    REQUIRE(renderer.row_count() == 1);

    lv_obj_t* row = renderer.get_row("bed_mesh");
    REQUIRE(row != nullptr);

    // Initially visible.
    REQUIRE_FALSE(lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN));

    // Flipping the visibility subject hides the row.
    lv_subject_set_int(&can_show, 0);
    REQUIRE(lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN));

    // And restoring re-shows it.
    lv_subject_set_int(&can_show, 1);
    REQUIRE_FALSE(lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN));

    // Tear down: clear the renderer FIRST so observers are uninstalled
    // before we deinit the local visibility subject (avoids dangling
    // observer pointers).
    renderer.clear();
    lv_subject_deinit(&can_show);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: set_state updates subject and persists across reads",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());

    auto set = make_multi_category_set();
    renderer.populate(container, set, nullptr, nullptr);

    REQUIRE(renderer.get_state("nozzle_clean") == 0);
    renderer.set_state("nozzle_clean", 1);
    REQUIRE(renderer.get_state("nozzle_clean") == 1);

    // No-op for unknown id (must not crash).
    renderer.set_state("does_not_exist", 1);
    REQUIRE(renderer.get_state("does_not_exist") == 0);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: AD5M Pro live DB entry produces one row, no subheader",
                 "[print_file_detail][pre_print_options][db]") {
    // Sanity-checks the live printer_database.json: AD5M Pro currently has a
    // single mechanical option (bed_mesh). Only one category present means
    // exactly one subheader (per category) is emitted. If the DB grows new
    // categories for this printer, this test will need adjustment.
    auto set = PrinterDetector::get_pre_print_option_set("FlashForge Adventurer 5M Pro");
    REQUIRE_FALSE(set.empty());

    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());
    renderer.populate(container, set, nullptr, nullptr);

    REQUIRE(renderer.row_count() == set.options.size());
    // Every option must have produced a corresponding row.
    for (const auto& opt : set.options) {
        REQUIRE(renderer.get_row(opt.id) != nullptr);
        REQUIRE(renderer.get_switch(opt.id) != nullptr);
    }
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: K1C live DB entry produces one row",
                 "[print_file_detail][pre_print_options][db]") {
    auto set = PrinterDetector::get_pre_print_option_set("Creality K1C");
    REQUIRE_FALSE(set.empty());

    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());
    renderer.populate(container, set, nullptr, nullptr);

    // K1C currently has just bed_mesh (PREPARE param + PRINT_PREPARED
    // pre-start gcode). Verify the row exists with a switch widget.
    REQUIRE(renderer.row_count() == set.options.size());
    REQUIRE(renderer.get_row("bed_mesh") != nullptr);
    REQUIRE(renderer.get_switch("bed_mesh") != nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: K2 Plus live DB renders bed_mesh and ai_detect",
                 "[print_file_detail][pre_print_options][db][ai_detect]") {
    // K2 Plus advertises bed_mesh (Mechanical) + ai_detect (Monitoring).
    // Two distinct categories means two subheaders should be emitted, and
    // the PreStartGcode strategy should render its row identically to
    // MacroParam — the renderer is strategy-agnostic.
    auto set = PrinterDetector::get_pre_print_option_set("Creality K2 Plus");
    REQUIRE_FALSE(set.empty());

    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());
    renderer.populate(container, set, nullptr, nullptr);

    REQUIRE(renderer.row_count() == set.options.size());
    REQUIRE(renderer.get_row("bed_mesh") != nullptr);
    REQUIRE(renderer.get_row("ai_detect") != nullptr);
    REQUIRE(renderer.get_switch("ai_detect") != nullptr);
    // Two categories -> two subheaders.
    REQUIRE(count_subheaders(container) == 2);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrePrintOptionsRenderer: clear() drops rows and resets subjects",
                 "[print_file_detail][pre_print_options]") {
    PrePrintOptionsRenderer renderer;
    lv_obj_t* container = lv_obj_create(test_screen());
    auto set = make_multi_category_set();

    renderer.populate(container, set, nullptr, nullptr);
    REQUIRE(renderer.row_count() == 3);

    renderer.clear();
    REQUIRE(renderer.row_count() == 0);
    REQUIRE(renderer.rendered_ids().empty());
    REQUIRE(renderer.get_row("bed_mesh") == nullptr);
}
