// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_status_widget.h"

#include "app_constants.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_panel_print_status.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "filament_sensor_manager.h"
#include "observer_factory.h"
#include "panel_widget_registry.h"
#include "print_history_manager.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "thumbnail_cache.h"
#include "thumbnail_load_context.h"
#include "thumbnail_processor.h"

#include <spdlog/spdlog.h>

#include <cstdio>

namespace helix {
void register_print_status_widget() {
    register_widget_factory("print_status", []() { return std::make_unique<PrintStatusWidget>(); });
    // No init_subjects needed — this widget uses subjects owned by PrinterState

    // Register XML event callbacks at startup (before any XML is parsed)
    lv_xml_register_event_cb(nullptr, "print_card_clicked_cb", PrintStatusWidget::print_card_clicked_cb);
}
} // namespace helix

using namespace helix;

PrintStatusWidget::PrintStatusWidget() : printer_state_(get_printer_state()) {}

PrintStatusWidget::~PrintStatusWidget() {
    detach();
}

void PrintStatusWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    using helix::ui::observe_int_sync;
    using helix::ui::observe_print_state;
    using helix::ui::observe_string_immediate;

    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    alive_->store(true);

    // Store this pointer for event callback recovery
    lv_obj_set_user_data(widget_obj_, this);

    // Cache widget references from XML
    print_card_thumb_ = lv_obj_find_by_name(widget_obj_, "print_card_thumb");
    print_card_active_thumb_ = lv_obj_find_by_name(widget_obj_, "print_card_active_thumb");
    print_card_label_ = lv_obj_find_by_name(widget_obj_, "print_card_label");
    print_card_layout_ = lv_obj_find_by_name(widget_obj_, "print_card_layout");
    print_card_thumb_wrap_ = lv_obj_find_by_name(widget_obj_, "print_card_thumb_wrap");
    print_card_info_ = lv_obj_find_by_name(widget_obj_, "print_card_info");

    // Set up observers (after widget references are cached and widget_obj_ is set)
    print_state_observer_ =
        observe_print_state<PrintStatusWidget>(printer_state_.get_print_state_enum_subject(), this,
                                               [](PrintStatusWidget* self, PrintJobState state) {
                                                   if (!self->widget_obj_)
                                                       return;
                                                   self->on_print_state_changed(state);
                                               });

    print_progress_observer_ =
        observe_int_sync<PrintStatusWidget>(printer_state_.get_print_progress_subject(), this,
                                            [](PrintStatusWidget* self, int /*progress*/) {
                                                if (!self->widget_obj_)
                                                    return;
                                                self->on_print_progress_or_time_changed();
                                            });

    print_time_left_observer_ =
        observe_int_sync<PrintStatusWidget>(printer_state_.get_print_time_left_subject(), this,
                                            [](PrintStatusWidget* self, int /*time*/) {
                                                if (!self->widget_obj_)
                                                    return;
                                                self->on_print_progress_or_time_changed();
                                            });

    // Use observe_string_immediate: the thumbnail handler only calls lv_image_set_src
    // (no observer lifecycle changes), and set_print_thumbnail_path is always called
    // from the UI thread via queue_update. Immediate avoids the double-deferral that
    // caused stale reads when the subject changed between notification and handler.
    print_thumbnail_path_observer_ = observe_string_immediate<PrintStatusWidget>(
        printer_state_.get_print_thumbnail_path_subject(), this,
        [](PrintStatusWidget* self, const char* path) {
            if (!self->widget_obj_)
                return;
            self->on_print_thumbnail_path_changed(path);
        });

    auto& fsm = helix::FilamentSensorManager::instance();
    filament_runout_observer_ = observe_int_sync<PrintStatusWidget>(
        fsm.get_any_runout_subject(), this, [](PrintStatusWidget* self, int any_runout) {
            if (!self->widget_obj_)
                return;
            spdlog::debug("[PrintStatusWidget] Filament runout subject changed: {}", any_runout);
            if (any_runout == 1) {
                self->check_and_show_idle_runout_modal();
            } else {
                self->runout_modal_shown_ = false;
            }
        });

    // Register history observer to update idle thumbnail when history loads [L072]
    std::weak_ptr<std::atomic<bool>> weak = alive_;
    history_changed_cb_ = [this, weak]() {
        if (weak.expired()) return;
        if (!widget_obj_ || !print_card_thumb_) return;
        auto state = static_cast<PrintJobState>(
            lv_subject_get_int(printer_state_.get_print_state_enum_subject()));
        bool is_idle = (state != PrintJobState::PRINTING && state != PrintJobState::PAUSED);
        if (is_idle) {
            reset_print_card_to_idle();
        }
    };
    if (auto* hm = get_print_history_manager()) {
        hm->add_observer(&history_changed_cb_);
    }

    spdlog::debug("[PrintStatusWidget] Subscribed to print state/progress/time/thumbnail/runout");

    // Check initial print state
    if (print_card_thumb_ && print_card_active_thumb_ && print_card_label_) {
        auto state = static_cast<PrintJobState>(
            lv_subject_get_int(printer_state_.get_print_state_enum_subject()));
        if (state == PrintJobState::PRINTING || state == PrintJobState::PAUSED) {
            on_print_state_changed(state);
        } else {
            // Set idle thumbnail (last print or benchy fallback)
            reset_print_card_to_idle();
        }
        spdlog::debug("[PrintStatusWidget] Found print card widgets for dynamic updates");
    } else {
        spdlog::warn("[PrintStatusWidget] Could not find all print card widgets "
                     "(thumb={}, active_thumb={}, label={})",
                     print_card_thumb_ != nullptr, print_card_active_thumb_ != nullptr,
                     print_card_label_ != nullptr);
    }

    spdlog::debug("[PrintStatusWidget] Attached");
}

void PrintStatusWidget::detach() {
    // Invalidate alive guard FIRST to abort in-flight async fetches
    alive_->store(false);

    // Unregister history observer
    if (auto* hm = get_print_history_manager()) {
        hm->remove_observer(&history_changed_cb_);
    }

    // Release observers
    print_state_observer_.reset();
    print_progress_observer_.reset();
    print_time_left_observer_.reset();
    print_thumbnail_path_observer_.reset();
    filament_runout_observer_.reset();

    // Clear widget references
    print_card_thumb_ = nullptr;
    print_card_active_thumb_ = nullptr;
    print_card_label_ = nullptr;
    print_card_layout_ = nullptr;
    print_card_thumb_wrap_ = nullptr;
    print_card_info_ = nullptr;

    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
    parent_screen_ = nullptr;

    spdlog::debug("[PrintStatusWidget] Detached");
}

// ============================================================================
// Size-Dependent Layout
// ============================================================================

void PrintStatusWidget::on_size_changed(int colspan, int rowspan, int /*width_px*/,
                                        int /*height_px*/) {
    if (!print_card_layout_ || !print_card_thumb_wrap_ || !print_card_info_) {
        return;
    }

    // 2x2: column layout (thumbnail on top, info below)
    // 1x2, 3x2: row layout (thumbnail left, info right)
    bool use_column = (colspan == 2 && rowspan >= 2);

    if (use_column) {
        lv_obj_set_flex_flow(print_card_layout_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(print_card_thumb_wrap_, LV_PCT(100));
        lv_obj_set_style_flex_grow(print_card_thumb_wrap_, 1, 0);
        lv_obj_set_width(print_card_info_, LV_PCT(100));
        lv_obj_set_height(print_card_info_, LV_SIZE_CONTENT);
        lv_obj_set_style_flex_grow(print_card_info_, 0, 0);
    } else {
        lv_obj_set_flex_flow(print_card_layout_, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(print_card_thumb_wrap_, LV_PCT(40));
        lv_obj_set_height(print_card_thumb_wrap_, LV_PCT(100));
        lv_obj_set_style_flex_grow(print_card_thumb_wrap_, 0, 0);
        lv_obj_set_height(print_card_info_, LV_PCT(100));
        lv_obj_set_width(print_card_info_, LV_SIZE_CONTENT);
        lv_obj_set_style_flex_grow(print_card_info_, 1, 0);
    }

    spdlog::debug("[PrintStatusWidget] on_size_changed {}x{} -> {}", colspan, rowspan,
                  use_column ? "column" : "row");
}

// ============================================================================
// Print Card Click Handler
// ============================================================================

void PrintStatusWidget::handle_print_card_clicked() {
    // Startup grace period: reject phantom clicks during early boot
    auto elapsed = std::chrono::steady_clock::now() - AppConstants::Startup::PROCESS_START_TIME;
    if (elapsed < AppConstants::Startup::PRINT_START_GRACE_PERIOD) {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        spdlog::warn("[PrintStatusWidget] Rejected print card click during startup grace period "
                     "({}s < {}s)",
                     secs, AppConstants::Startup::PRINT_START_GRACE_PERIOD.count());
        return;
    }

    if (!printer_state_.can_start_new_print()) {
        // Print in progress - show print status overlay
        spdlog::info(
            "[PrintStatusWidget] Print card clicked - showing print status (print in progress)");

        if (!PrintStatusPanel::push_overlay(parent_screen_)) {
            spdlog::error("[PrintStatusWidget] Failed to push print status overlay");
        }
    } else {
        // No print in progress - navigate to print select panel
        spdlog::info("[PrintStatusWidget] Print card clicked - navigating to print select panel");
        NavigationManager::instance().set_active(PanelId::PrintSelect);
    }
}

// ============================================================================
// Observer Callbacks
// ============================================================================

void PrintStatusWidget::on_print_state_changed(PrintJobState state) {
    if (!widget_obj_ || !print_card_thumb_ || !print_card_label_) {
        return;
    }
    if (!lv_obj_is_valid(widget_obj_)) {
        return;
    }

    bool is_active = (state == PrintJobState::PRINTING || state == PrintJobState::PAUSED);

    if (is_active) {
        spdlog::debug("[PrintStatusWidget] Print active - updating card progress display");
        update_print_card_from_state();
    } else {
        spdlog::debug("[PrintStatusWidget] Print not active - reverting card to idle state");
        reset_print_card_to_idle();
    }
}

void PrintStatusWidget::on_print_progress_or_time_changed() {
    update_print_card_from_state();
}

void PrintStatusWidget::on_print_thumbnail_path_changed(const char* path) {
    if (!widget_obj_ || !print_card_active_thumb_) {
        return;
    }

    if (path && path[0] != '\0') {
        lv_image_set_src(print_card_active_thumb_, path);
        spdlog::info("[PrintStatusWidget] Active print thumbnail updated: {}", path);
    } else {
        lv_image_set_src(print_card_active_thumb_, "A:assets/images/benchy_thumbnail_white.png");
        spdlog::debug("[PrintStatusWidget] Active print thumbnail cleared (empty path)");
    }
}

void PrintStatusWidget::update_print_card_from_state() {
    auto state = static_cast<PrintJobState>(
        lv_subject_get_int(printer_state_.get_print_state_enum_subject()));

    // Only update if actively printing
    if (state != PrintJobState::PRINTING && state != PrintJobState::PAUSED) {
        return;
    }

    int progress = lv_subject_get_int(printer_state_.get_print_progress_subject());
    int time_left = lv_subject_get_int(printer_state_.get_print_time_left_subject());

    update_print_card_label(progress, time_left);
}

void PrintStatusWidget::update_print_card_label(int progress, int time_left_secs) {
    if (!print_card_label_ || !lv_obj_is_valid(print_card_label_)) {
        return;
    }

    char buf[64];
    int hours = time_left_secs / 3600;
    int minutes = (time_left_secs % 3600) / 60;

    if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%d%% \u2022 %dh %02dm left", progress, hours, minutes);
    } else if (minutes > 0) {
        std::snprintf(buf, sizeof(buf), "%d%% \u2022 %dm left", progress, minutes);
    } else {
        std::snprintf(buf, sizeof(buf), "%d%% \u2022 < 1m left", progress);
    }

    lv_label_set_text(print_card_label_, buf);
}

std::string PrintStatusWidget::get_last_print_thumbnail_path() const {
    auto* history = get_print_history_manager();
    if (!history || !history->is_loaded()) {
        return {};
    }

    const auto& jobs = history->get_jobs();
    if (jobs.empty()) {
        return {};
    }

    const auto& job = jobs.front();

    // Select the best thumbnail for the widget's actual rendered size
    if (!job.thumbnails.empty() && print_card_thumb_ && lv_obj_is_valid(print_card_thumb_)) {
        int target_w = lv_obj_get_width(print_card_thumb_);
        int target_h = lv_obj_get_height(print_card_thumb_);

        // Find smallest thumbnail that meets or exceeds the widget dimensions
        const ThumbnailInfo* best_adequate = nullptr;
        const ThumbnailInfo* largest = &job.thumbnails[0];

        for (const auto& t : job.thumbnails) {
            if (t.pixel_count() > largest->pixel_count()) {
                largest = &t;
            }
            if (t.width >= target_w && t.height >= target_h) {
                if (!best_adequate || t.pixel_count() < best_adequate->pixel_count()) {
                    best_adequate = &t;
                }
            }
        }

        const auto* best = best_adequate ? best_adequate : largest;
        spdlog::debug("[PrintStatusWidget] Widget {}x{}, selected thumbnail {}x{} ({})",
                      target_w, target_h, best->width, best->height, best->relative_path);
        return best->relative_path;
    }

    // Fallback: use pre-selected largest thumbnail
    return job.thumbnail_path;
}

void PrintStatusWidget::reset_print_card_to_idle() {
    if (print_card_label_ && lv_obj_is_valid(print_card_label_)) {
        lv_label_set_text(print_card_label_, "Print Files");
    }

    if (!print_card_thumb_ || !lv_obj_is_valid(print_card_thumb_)) {
        return;
    }

    // Try to show the last printed file's thumbnail instead of benchy
    std::string thumb_rel_path = get_last_print_thumbnail_path();
    if (thumb_rel_path.empty()) {
        lv_image_set_src(print_card_thumb_, "A:assets/images/benchy_thumbnail_white.png");
        spdlog::debug("[PrintStatusWidget] Idle thumbnail: benchy (no history)");
        return;
    }

    // Compute pre-scale target from actual widget size (not hardcoded breakpoints)
    int widget_w = lv_obj_get_width(print_card_thumb_);
    int widget_h = lv_obj_get_height(print_card_thumb_);
    auto target = helix::ThumbnailProcessor::get_target_for_resolution(
        widget_w, widget_h, helix::ThumbnailSize::Detail);

    // Check if we already have a pre-scaled BIN version
    auto cached = get_thumbnail_cache().get_if_optimized(thumb_rel_path, target);
    if (!cached.empty()) {
        lv_image_set_src(print_card_thumb_, cached.c_str());
        spdlog::debug("[PrintStatusWidget] Idle thumbnail from cache: {}", cached);
        return;
    }

    // Set benchy as placeholder while we fetch
    lv_image_set_src(print_card_thumb_, "A:assets/images/benchy_thumbnail_white.png");

    // Fetch async from Moonraker
    auto* api = get_moonraker_api();
    if (!api) {
        spdlog::debug("[PrintStatusWidget] Idle thumbnail: benchy (no API)");
        return;
    }

    // Use alive guard to prevent use-after-free if widget is destroyed during fetch [L072]
    lv_obj_t* thumb_widget = print_card_thumb_;
    std::weak_ptr<std::atomic<bool>> weak_alive = alive_;

    get_thumbnail_cache().fetch_optimized(
        api, thumb_rel_path, target,
        [thumb_widget, weak_alive](const std::string& lvgl_path) {
            if (weak_alive.expired()) return;
            helix::ui::queue_update<std::string>(
                std::make_unique<std::string>(lvgl_path),
                [thumb_widget](std::string* path) {
                    if (lv_obj_is_valid(thumb_widget)) {
                        lv_image_set_src(thumb_widget, path->c_str());
                        spdlog::info("[PrintStatusWidget] Idle thumbnail loaded: {}", *path);
                    }
                });
        },
        [](const std::string& error) {
            spdlog::debug("[PrintStatusWidget] Idle thumbnail fetch failed: {}", error);
        });
}

// ============================================================================
// Filament Runout Modal
// ============================================================================

void PrintStatusWidget::check_and_show_idle_runout_modal() {
    // Grace period - don't show modal during startup
    auto& fsm = helix::FilamentSensorManager::instance();
    if (fsm.is_in_startup_grace_period()) {
        spdlog::debug("[PrintStatusWidget] In startup grace period - skipping runout modal");
        return;
    }

    // Verify actual sensor state
    if (!fsm.has_any_runout()) {
        spdlog::debug("[PrintStatusWidget] No actual runout detected - skipping modal");
        return;
    }

    // Check suppression logic (AMS without bypass, wizard active, etc.)
    if (!get_runtime_config()->should_show_runout_modal()) {
        spdlog::debug("[PrintStatusWidget] Runout modal suppressed by runtime config");
        return;
    }

    // Only show modal if not already shown
    if (runout_modal_shown_) {
        spdlog::debug("[PrintStatusWidget] Runout modal already shown - skipping");
        return;
    }

    // Only show if printer is idle (not printing/paused)
    int print_state = lv_subject_get_int(printer_state_.get_print_state_enum_subject());
    if (print_state != static_cast<int>(PrintJobState::STANDBY) &&
        print_state != static_cast<int>(PrintJobState::COMPLETE) &&
        print_state != static_cast<int>(PrintJobState::CANCELLED)) {
        spdlog::debug("[PrintStatusWidget] Print active (state={}) - skipping idle runout modal",
                      print_state);
        return;
    }

    spdlog::info("[PrintStatusWidget] Showing idle runout modal");
    show_idle_runout_modal();
    runout_modal_shown_ = true;
}

void PrintStatusWidget::trigger_idle_runout_check() {
    spdlog::debug("[PrintStatusWidget] Triggering deferred runout check");
    runout_modal_shown_ = false;
    check_and_show_idle_runout_modal();
}

void PrintStatusWidget::show_idle_runout_modal() {
    if (runout_modal_.is_visible()) {
        return;
    }

    runout_modal_.set_on_load_filament([this]() {
        spdlog::info("[PrintStatusWidget] User chose to load filament (idle)");
        NavigationManager::instance().set_active(PanelId::Filament);
    });

    runout_modal_.set_on_resume([]() {
        // Resume not applicable when idle
    });

    runout_modal_.set_on_cancel_print([]() {
        // Cancel not applicable when idle
    });

    runout_modal_.show(parent_screen_);
}

// ============================================================================
// Static Trampolines
// ============================================================================

void PrintStatusWidget::print_card_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusWidget] print_card_clicked_cb");

    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* self = static_cast<PrintStatusWidget*>(lv_obj_get_user_data(target));
    if (self) {
        self->handle_print_card_clicked();
    } else {
        spdlog::warn(
            "[PrintStatusWidget] print_card_clicked_cb: could not recover widget instance");
    }

    LVGL_SAFE_EVENT_CB_END();
}
