// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera_widget.h"

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "app_globals.h"
#include "moonraker_api.h"
#include "panel_widget_registry.h"
#include "printer_state.h"
#include "static_subject_registry.h"
#include "subject_debug_registry.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

// Module-level subject for status text binding (static — shared across instances)
static lv_subject_t s_camera_status_subject;
static char s_camera_status_buffer[64];
static bool s_subjects_initialized = false;

static void camera_widget_init_subjects() {
    if (s_subjects_initialized) {
        return;
    }

    lv_subject_init_string(&s_camera_status_subject, s_camera_status_buffer, nullptr,
                           sizeof(s_camera_status_buffer), "No Camera");
    lv_xml_register_subject(nullptr, "camera_status_text", &s_camera_status_subject);
    SubjectDebugRegistry::instance().register_subject(&s_camera_status_subject,
                                                      "camera_status_text",
                                                      LV_SUBJECT_TYPE_STRING, __FILE__, __LINE__);

    s_subjects_initialized = true;

    StaticSubjectRegistry::instance().register_deinit("CameraWidgetSubjects", []() {
        if (s_subjects_initialized && lv_is_initialized()) {
            lv_subject_deinit(&s_camera_status_subject);
            s_subjects_initialized = false;
            spdlog::trace("[CameraWidget] Subjects deinitialized");
        }
    });

    spdlog::debug("[CameraWidget] Subjects initialized");
}

// Only one fullscreen camera overlay can be open at a time
static helix::CameraWidget* s_fullscreen_owner = nullptr;

static void on_camera_clicked(lv_event_t* e) {
    auto* self = helix::panel_widget_from_event<helix::CameraWidget>(e);
    if (self) {
        self->open_fullscreen();
    }
}

static void on_camera_fullscreen_close(lv_event_t* /*e*/) {
    if (s_fullscreen_owner) {
        s_fullscreen_owner->close_fullscreen();
    }
}

namespace helix {
void register_camera_widget() {
    register_widget_factory("camera", []() { return std::make_unique<CameraWidget>(); });
    register_widget_subjects("camera", camera_widget_init_subjects);
    lv_xml_register_event_cb(nullptr, "on_camera_clicked", on_camera_clicked);
    lv_xml_register_event_cb(nullptr, "on_camera_fullscreen_close", on_camera_fullscreen_close);
}

CameraWidget::CameraWidget() : alive_(std::make_shared<bool>(true)) {}

CameraWidget::~CameraWidget() {
    // detach() handles LVGL pointer cleanup, observer release, fullscreen close
    detach();
    // Full cleanup beyond detach: invalidate alive guard and stop stream
    if (alive_) {
        *alive_ = false;
    }
    stop_stream();
}

void CameraWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    camera_image_ = lv_obj_find_by_name(widget_obj_, "camera_image");

    lv_obj_set_user_data(widget_obj_, this);

    // Observe printer_has_webcam — URLs may not be available yet at attach
    // time (Moonraker sends webcam list asynchronously). When the subject
    // transitions to 1, try starting the stream if we're active.
    lv_subject_t* gate = lv_xml_get_subject(nullptr, "printer_has_webcam");
    if (gate) {
        webcam_observer_ = helix::ui::observe_int_sync<CameraWidget>(gate, this, [](CameraWidget* self, int val) {
            if (val > 0) {
                self->set_status_text("Connecting Camera...");
                if (self->active_) {
                    self->start_stream();
                }
            } else {
                self->stop_stream();
                self->set_status_text("No Camera");
            }
        });
    }

    spdlog::debug("[CameraWidget] Attached");
}

void CameraWidget::detach() {
    if (!widget_obj_) {
        return; // Already detached or never attached
    }

    // Close fullscreen if open before detaching
    if (fullscreen_overlay_) {
        close_fullscreen();
    }

    // Lightweight detach: release observer and LVGL pointers but preserve
    // the camera stream. alive_ is intentionally NOT invalidated here
    // (unlike other widgets) — the stream keeps running during the
    // detach→reattach gap. Frame callbacks check camera_image_ (null
    // after detach) and safely no-op until re-attach.
    webcam_observer_.reset();

    lv_obj_set_user_data(widget_obj_, nullptr);
    widget_obj_ = nullptr;
    parent_screen_ = nullptr;
    camera_image_ = nullptr;

    spdlog::debug("[CameraWidget] Detached (stream preserved)");
}

void CameraWidget::on_activate() {
    active_ = true;
    start_stream();
}

void CameraWidget::on_deactivate() {
    active_ = false;
    stop_stream();
}

void CameraWidget::on_size_changed(int /*colspan*/, int /*rowspan*/, int /*width_px*/,
                                   int /*height_px*/) {
    // Image scales automatically via XML width="100%" height="100%"
    // If we need scale-to-cover, we'd adjust lv_image_set_inner_align here
    if (camera_image_) {
        lv_image_set_inner_align(camera_image_, LV_IMAGE_ALIGN_COVER);
    }
}

void CameraWidget::start_stream() {
    if (stream_ && stream_->is_running()) {
        return;
    }

    auto& state = get_printer_state();
    std::string stream_url = state.get_webcam_stream_url();
    std::string snapshot_url = state.get_webcam_snapshot_url();

    if (stream_url.empty() && snapshot_url.empty()) {
        // URLs not available yet — observer will retry when they arrive
        return;
    }

    // Resolve relative URLs against the web frontend (nginx on port 80).
    // Moonraker's webcam URLs are relative paths meant for the nginx reverse
    // proxy, NOT the Moonraker API port (7125). Extract the host from the
    // Moonraker HTTP base URL and use port 80.
    auto* api = get_moonraker_api();
    if (api) {
        const auto& base = api->get_http_base_url();
        spdlog::debug("[CameraWidget] HTTP base URL: '{}'", base);
        if (!base.empty()) {
            // Extract host from base URL: "http://HOST:PORT" -> "http://HOST"
            std::string web_base;
            auto scheme_end = base.find("://");
            if (scheme_end != std::string::npos) {
                auto host_start = scheme_end + 3;
                auto port_pos = base.find(':', host_start);
                if (port_pos != std::string::npos) {
                    web_base = base.substr(0, port_pos); // "http://HOST"
                } else {
                    web_base = base; // Already no port
                }
            }
            if (web_base.empty()) {
                web_base = base; // Fallback
            }
            spdlog::debug("[CameraWidget] Web frontend base: '{}'", web_base);

            if (!stream_url.empty() && stream_url[0] == '/') {
                stream_url = web_base + stream_url;
            }
            if (!snapshot_url.empty() && snapshot_url[0] == '/') {
                snapshot_url = web_base + snapshot_url;
            }
        }
    } else {
        spdlog::warn("[CameraWidget] No MoonrakerAPI available for URL resolution");
    }

    set_status_text("Connecting Camera...");

    stream_ = std::make_unique<CameraStream>();
    stream_->set_flip(state.get_webcam_flip_horizontal(), state.get_webcam_flip_vertical());

    // Capture alive guard by value for safe callback
    std::weak_ptr<bool> weak_alive = alive_;

    stream_->start(
        stream_url, snapshot_url,
        [this, weak_alive](lv_draw_buf_t* frame) {
            auto alive = weak_alive.lock();
            if (!alive || !*alive) return;

            helix::ui::queue_update([this, weak_alive, frame]() {
                auto alive = weak_alive.lock();
                if (!alive || !*alive) return;

                // Deliver frame to fullscreen image if open, otherwise widget image
                lv_obj_t* target = fullscreen_image_ ? fullscreen_image_ : camera_image_;
                if (target) {
                    lv_image_set_src(target, frame);
                    set_status_text("");
                    // Hide spinner overlay on first frame
                    if (widget_obj_) {
                        lv_obj_t* overlay = lv_obj_find_by_name(widget_obj_, "camera_overlay");
                        if (overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
                            lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                }

                if (stream_) stream_->frame_consumed();
            });
        },
        [this, weak_alive](const char* msg) {
            auto alive = weak_alive.lock();
            if (!alive || !*alive) return;

            std::string status(msg);
            helix::ui::queue_update([this, weak_alive, status]() {
                auto alive = weak_alive.lock();
                if (!alive || !*alive) return;
                set_status_text(status.c_str());
            });
        });

    spdlog::info("[CameraWidget] Stream started (stream={}, snapshot={})", stream_url, snapshot_url);
}

void CameraWidget::stop_stream() {
    if (!stream_) {
        return;
    }

    // Invalidate alive guard FIRST — queued UI callbacks check this and
    // become no-ops, preventing use-after-free on freed draw buffers
    *alive_ = false;

    // Clear image sources before stopping — stop() frees the draw buffers
    // that LVGL may still reference for rendering
    if (lv_is_initialized()) {
        if (fullscreen_image_) {
            lv_image_set_src(fullscreen_image_, nullptr);
        }
        if (camera_image_) {
            lv_image_set_src(camera_image_, nullptr);
        }
    }

    stream_->stop();
    stream_.reset();

    // Reset alive guard so the stream can be restarted (on_activate)
    alive_ = std::make_shared<bool>(true);

    spdlog::debug("[CameraWidget] Stream stopped");
}

void CameraWidget::set_status_text(const char* text) {
    if (s_subjects_initialized) {
        lv_subject_copy_string(&s_camera_status_subject, text);
    }
}

void CameraWidget::open_fullscreen() {
    if (fullscreen_overlay_ || !parent_screen_ || !stream_) {
        return;
    }

    // Create overlay as child of active screen (standard overlay pattern)
    lv_obj_t* screen = lv_display_get_screen_active(nullptr);
    if (!screen) {
        return;
    }

    auto* overlay =
        static_cast<lv_obj_t*>(lv_xml_create(screen, "camera_fullscreen", nullptr));
    if (!overlay) {
        spdlog::warn("[CameraWidget] Failed to create camera_fullscreen component");
        return;
    }

    // Start hidden — push_overlay handles showing it
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

    fullscreen_overlay_ = overlay;
    fullscreen_image_ = lv_obj_find_by_name(overlay, "fullscreen_camera_image");
    s_fullscreen_owner = this;

    if (fullscreen_image_) {
        lv_image_set_inner_align(fullscreen_image_, LV_IMAGE_ALIGN_COVER);
    }

    // Register close callback so NavigationManager cleanup works
    NavigationManager::instance().register_overlay_close_callback(overlay, [this]() {
        fullscreen_overlay_ = nullptr;
        fullscreen_image_ = nullptr;
        s_fullscreen_owner = nullptr;
        spdlog::debug("[CameraWidget] Fullscreen overlay closed via navigation");
    });

    // Use zoom animation from the widget's position
    lv_area_t source_rect;
    lv_obj_get_coords(widget_obj_, &source_rect);
    NavigationManager::instance().push_overlay_zoom_from(overlay, source_rect);

    spdlog::info("[CameraWidget] Opened fullscreen camera");
}

void CameraWidget::close_fullscreen() {
    if (!fullscreen_overlay_) {
        return;
    }

    // go_back() triggers the registered close callback which clears our pointers
    NavigationManager::instance().go_back();

    spdlog::debug("[CameraWidget] Closed fullscreen camera");
}

} // namespace helix

#endif // HELIX_HAS_CAMERA
