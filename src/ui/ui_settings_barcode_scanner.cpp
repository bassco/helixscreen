// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_settings_barcode_scanner.h"

#include "bluetooth_loader.h"
#include "bt_scanner_discovery_utils.h"
#include "input_device_scanner.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "ui_callback_helpers.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_toast_manager.h"
#include "ui_update_queue.h"
#include "usb_scanner_monitor.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>

namespace helix::ui {

BarcodeScannerSettingsOverlay* BarcodeScannerSettingsOverlay::s_active_instance_ = nullptr;

namespace {
std::unique_ptr<BarcodeScannerSettingsOverlay> g_barcode_scanner_overlay;

struct RowData {
    std::string vendor_product; // empty = auto-detect
    std::string device_name;
    std::string bt_mac;         // empty for USB rows
};

// Heap-allocated user_data for the BT pairing confirmation modal callbacks.
struct PairData {
    std::string mac;
    std::string name;
};
}

BarcodeScannerSettingsOverlay& get_barcode_scanner_settings_overlay() {
    if (!g_barcode_scanner_overlay) {
        g_barcode_scanner_overlay = std::make_unique<BarcodeScannerSettingsOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "BarcodeScannerSettingsOverlay", []() { g_barcode_scanner_overlay.reset(); });
    }
    return *g_barcode_scanner_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

BarcodeScannerSettingsOverlay::BarcodeScannerSettingsOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

BarcodeScannerSettingsOverlay::~BarcodeScannerSettingsOverlay() {
    stop_bt_discovery();

    if (subjects_initialized_) {
        lv_subject_deinit(&bt_available_subject_);
        lv_subject_deinit(&bt_discovering_subject_);
        lv_subject_deinit(&keymap_index_subject_);
        lv_subject_deinit(&has_devices_subject_);
        lv_subject_deinit(&current_device_label_subject_);
    }

    if (bt_ctx_) {
        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        if (loader.deinit) {
            loader.deinit(bt_ctx_);
        }
        bt_ctx_ = nullptr;
    }

    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void BarcodeScannerSettingsOverlay::init_subjects() {
    if (subjects_initialized_) return;

    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    lv_subject_init_int(&bt_available_subject_, loader.is_available() ? 1 : 0);
    lv_xml_register_subject(nullptr, "scanner_bt_available", &bt_available_subject_);

    lv_subject_init_int(&bt_discovering_subject_, 0);
    lv_xml_register_subject(nullptr, "scanner_bt_discovering", &bt_discovering_subject_);

    lv_subject_init_int(&has_devices_subject_, 0);
    lv_xml_register_subject(nullptr, "scanner_has_devices", &has_devices_subject_);

    const std::string km = helix::SettingsManager::instance().get_scanner_keymap();
    int km_idx = 0;
    if (km == "qwertz") km_idx = 1;
    else if (km == "azerty") km_idx = 2;
    lv_subject_init_int(&keymap_index_subject_, km_idx);
    lv_xml_register_subject(nullptr, "scanner_keymap_index", &keymap_index_subject_);

    current_device_label_buf_[0] = '\0';
    lv_subject_init_string(&current_device_label_subject_, current_device_label_buf_, nullptr,
                           sizeof(current_device_label_buf_), current_device_label_buf_);
    lv_xml_register_subject(nullptr, "scanner_current_device_label",
                            &current_device_label_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void BarcodeScannerSettingsOverlay::register_callbacks() {
    register_xml_callbacks({
        {"on_bs_scan_bluetooth", on_bs_scan_bluetooth},
        {"on_bs_refresh_usb", on_bs_refresh_usb},
        {"on_bs_keymap_changed", on_bs_keymap_changed},
        {"on_bs_row_clicked", on_bs_row_clicked},
        {"on_bs_row_forget", on_bs_row_forget},
        {"on_bs_auto_detect_clicked", on_bs_auto_detect_clicked},
        {"on_bs_pair_confirm", on_bs_pair_confirm},
        {"on_bs_pair_cancel", on_bs_pair_cancel},
    });

    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* BarcodeScannerSettingsOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    overlay_root_ = static_cast<lv_obj_t*>(
        lv_xml_create(parent, "barcode_scanner_settings", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void BarcodeScannerSettingsOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    NavigationManager::instance().push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void BarcodeScannerSettingsOverlay::on_activate() {
    OverlayBase::on_activate();
    s_active_instance_ = this;

    usb_list_ = lv_obj_find_by_name(overlay_root_, "usb_device_list");
    bt_list_  = lv_obj_find_by_name(overlay_root_, "bt_device_list");

    refresh_current_selection_label();
    populate_device_list();
}

void BarcodeScannerSettingsOverlay::on_deactivate() {
    stop_bt_discovery();
    s_active_instance_ = nullptr;
    usb_list_ = nullptr;
    bt_list_ = nullptr;
    OverlayBase::on_deactivate();
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void BarcodeScannerSettingsOverlay::refresh_current_selection_label() {
    auto id = helix::SettingsManager::instance().get_scanner_device_id();
    auto name = helix::SettingsManager::instance().get_scanner_device_name();
    const char* text = id.empty() ? "Auto-detect" : name.c_str();
    lv_subject_copy_string(&current_device_label_subject_, text);
}

// ============================================================================
// DEVICE LIST POPULATION
// ============================================================================

void BarcodeScannerSettingsOverlay::add_usb_row(lv_obj_t* container, const std::string& label,
                                                 const std::string& sublabel,
                                                 const std::string& vendor_product) {
    if (!container) return;

    const bool selected =
        !vendor_product.empty() &&
        vendor_product == helix::SettingsManager::instance().get_scanner_device_id();

    const char* attrs[] = {
        "row_icon",     "usb",
        "row_label",    label.c_str(),
        "row_sublabel", sublabel.c_str(),
        "hide_check",   selected ? "false" : "true",
        "hide_forget",  "true",
        nullptr,
    };
    lv_obj_t* row = static_cast<lv_obj_t*>(
        lv_xml_create(container, "barcode_scanner_device_row", attrs));
    if (!row) return;

    auto* data = new RowData{vendor_product, label, std::string{}};
    lv_obj_set_user_data(row, data);

    // LV_EVENT_DELETE cleanup: frees heap-allocated RowData for this row.
    // lv_obj_add_event_cb is intentionally used here — LV_EVENT_DELETE cannot
    // be expressed in XML, and this is the same pattern used in ScannerPickerModal.
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
        delete static_cast<RowData*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
    }, LV_EVENT_DELETE, nullptr);
}

void BarcodeScannerSettingsOverlay::add_auto_detect_row(lv_obj_t* container) {
    if (!container) return;

    const bool selected =
        helix::SettingsManager::instance().get_scanner_device_id().empty() &&
        helix::SettingsManager::instance().get_scanner_bt_address().empty();

    const char* attrs[] = {
        "row_icon",     "magnify",
        "row_label",    "Auto-detect",
        "row_sublabel", "Use first HID scanner found",
        "hide_check",   selected ? "false" : "true",
        "hide_forget",  "true",
        nullptr,
    };
    lv_obj_t* row = static_cast<lv_obj_t*>(
        lv_xml_create(container, "barcode_scanner_device_row", attrs));
    if (!row) return;

    auto* data = new RowData{std::string{}, "Auto-detect", std::string{}};
    lv_obj_set_user_data(row, data);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
        delete static_cast<RowData*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
    }, LV_EVENT_DELETE, nullptr);
}

void BarcodeScannerSettingsOverlay::add_bt_row(lv_obj_t* container, const std::string& label,
                                                const std::string& sublabel,
                                                const std::string& vendor_product,
                                                const std::string& bt_mac, bool paired) {
    if (!container) return;

    const auto& saved_bt_mac =
        helix::SettingsManager::instance().get_scanner_bt_address();
    const auto& saved_vp =
        helix::SettingsManager::instance().get_scanner_device_id();
    const bool selected =
        (!vendor_product.empty() && vendor_product == saved_vp) ||
        (!bt_mac.empty() && bt_mac == saved_bt_mac);

    const char* attrs[] = {
        "row_icon",     "bluetooth",
        "row_label",    label.c_str(),
        "row_sublabel", sublabel.c_str(),
        "hide_check",   selected ? "false" : "true",
        "hide_forget",  paired ? "false" : "true",
        nullptr,
    };
    lv_obj_t* row = static_cast<lv_obj_t*>(
        lv_xml_create(container, "barcode_scanner_device_row", attrs));
    if (!row) return;

    auto* data = new RowData{vendor_product, label, bt_mac};
    lv_obj_set_user_data(row, data);

    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
        delete static_cast<RowData*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
    }, LV_EVENT_DELETE, nullptr);
}

void BarcodeScannerSettingsOverlay::populate_device_list() {
    if (usb_list_) lv_obj_clean(usb_list_);
    if (bt_list_)  lv_obj_clean(bt_list_);

    if (!usb_list_) return;

    // Auto-detect always first
    add_auto_detect_row(usb_list_);

    // Enumerate sysfs HID devices. bus_type == 0x05 = Bluetooth (already connected as HID).
    // Unpaired BT discovery devices appear in bt_list_ (populated by Task 7 logic).
    auto devices = helix::input::enumerate_usb_hid_devices();
    spdlog::debug("[{}] Enumerated {} HID devices", get_name(), devices.size());

    int usb_count = 0;
    int bt_connected_count = 0;
    for (const auto& dev : devices) {
        const std::string vendor_product = dev.vendor_id + ":" + dev.product_id;
        const std::string sublabel = vendor_product + "  " + dev.event_path;
        const bool is_bt = (dev.bus_type == 0x05);
        if (is_bt && bt_list_) {
            add_bt_row(bt_list_, dev.name, sublabel, vendor_product, std::string{}, true);
            bt_connected_count++;
        } else {
            add_usb_row(usb_list_, dev.name, sublabel, vendor_product);
            usb_count++;
        }
    }

    // bt_devices_ (discovery) is populated in Task 7; we still iterate so the
    // body is complete and Task 7 just needs to uncomment or leave as-is.
    for (const auto& bt_dev : bt_devices_) {
        // Skip if already represented by a sysfs (connected) BT device above
        bool already_shown = false;
        for (const auto& dev : devices) {
            if (dev.bus_type == 0x05 && dev.name == bt_dev.name) {
                already_shown = true;
                break;
            }
        }
        if (already_shown) continue;
        if (bt_list_) {
            add_bt_row(bt_list_, bt_dev.name, bt_dev.mac, std::string{}, bt_dev.mac,
                       bt_dev.paired);
        }
    }

    const bool any = usb_count > 0 || bt_connected_count > 0 || !bt_devices_.empty();
    lv_subject_set_int(&has_devices_subject_, any ? 1 : 0);
}

void BarcodeScannerSettingsOverlay::handle_device_selected(const std::string& vendor_product,
                                                            const std::string& device_name,
                                                            const std::string& bt_mac) {
    auto& s = helix::SettingsManager::instance();

    // If unpaired BT-only device (Task 7 handles pairing)
    if (!bt_mac.empty() && vendor_product.empty()) {
        bool already_paired = false;
        for (const auto& d : bt_devices_) {
            if (d.mac == bt_mac && d.paired) { already_paired = true; break; }
        }
        if (!already_paired) {
            pair_bt_device(bt_mac, device_name);  // stub until Task 7
            return;
        }
    }

    spdlog::info("[{}] Selected: '{}' ({}{})", get_name(), device_name,
                 vendor_product.empty() ? "auto-detect" : vendor_product,
                 bt_mac.empty() ? "" : " bt:" + bt_mac);

    s.set_scanner_device_id(vendor_product);
    s.set_scanner_device_name(
        vendor_product.empty() && bt_mac.empty() ? std::string{} : device_name);
    s.set_scanner_bt_address(bt_mac);

    refresh_current_selection_label();
    populate_device_list();  // redraw so checkmark moves to new selection
}

void BarcodeScannerSettingsOverlay::handle_keymap_changed(int dropdown_index) {
    const char* values[] = {"qwerty", "qwertz", "azerty"};
    if (dropdown_index < 0 || dropdown_index >= 3) return;
    helix::SettingsManager::instance().set_scanner_keymap(values[dropdown_index]);
    helix::UsbScannerMonitor::set_active_layout(
        helix::UsbScannerMonitor::parse_keymap(values[dropdown_index]));
}

void BarcodeScannerSettingsOverlay::start_bt_discovery() {
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (!loader.is_available() || !loader.discover) {
        spdlog::warn("[{}] BT discovery unavailable", get_name());
        return;
    }

    if (bt_discovering_) {
        spdlog::debug("[{}] BT discovery already in progress", get_name());
        return;
    }

    // Initialize BT context if needed
    if (!bt_ctx_ && loader.init) {
        bt_ctx_ = loader.init();
        if (!bt_ctx_) {
            spdlog::error("[{}] Failed to init BT context", get_name());
            ToastManager::instance().show(ToastSeverity::ERROR,
                                          lv_tr("Bluetooth initialization failed"));
            return;
        }
    }

    bt_discovering_ = true;

    // Keep already-paired devices; remove unpaired (stale discovery results)
    bt_devices_.erase(std::remove_if(bt_devices_.begin(), bt_devices_.end(),
                                     [](const BtDeviceInfo& d) { return !d.paired; }),
                      bt_devices_.end());

    lv_subject_set_int(&bt_discovering_subject_, 1);

    // Refresh device list immediately so paired BT devices are visible
    // while discovery is in progress.
    populate_device_list();

    // Set up discovery context (shared_ptr so the detached thread keeps it alive)
    bt_discovery_ctx_ = std::make_shared<BtDiscoveryContext>();
    bt_discovery_ctx_->alive.store(true);
    bt_discovery_ctx_->overlay = this;
    bt_discovery_ctx_->token = lifetime_.token();

    auto disc_ctx = bt_discovery_ctx_; // shared_ptr copy for thread
    auto* ctx = bt_ctx_;

    // Wrap spawn in try/catch per feedback_no_bare_threads_arm.md (#724) —
    // thread creation can fail on memory-constrained ARM targets.
    try {
        std::thread([ctx, disc_ctx]() {
            auto& ldr = helix::bluetooth::BluetoothLoader::instance();
            ldr.discover(
                ctx, 15000,
                [](const helix_bt_device* dev, void* user_data) {
                    auto* dctx = static_cast<BtDiscoveryContext*>(user_data);
                    if (!dctx->alive.load())
                        return;

                    // Filter: only include devices that look like HID scanners
                    bool looks_like_scanner =
                        helix::bluetooth::is_hid_scanner_uuid(dev->service_uuid) ||
                        helix::bluetooth::is_likely_bt_scanner(dev->name);

                    if (!looks_like_scanner) {
                        spdlog::trace("[BarcodeScannerSettings] Skipping non-scanner BT: {}",
                                      dev->name ? dev->name : "(null)");
                        return;
                    }

                    BtDeviceInfo info;
                    info.mac     = dev->mac    ? dev->mac    : "";
                    info.name    = dev->name   ? dev->name   : "Unknown";
                    info.paired  = dev->paired;
                    info.is_ble  = dev->is_ble;

                    // Use tok.defer() — token holds its own shared_ptr; safe if
                    // overlay is destroyed before the callback fires (#707).
                    dctx->token->defer([dctx, info]() {
                        if (!dctx->alive.load())
                            return;
                        auto* overlay = dctx->overlay;

                        // Avoid duplicates by MAC
                        bool found = false;
                        for (const auto& existing : overlay->bt_devices_) {
                            if (existing.mac == info.mac) { found = true; break; }
                        }
                        if (!found) {
                            overlay->bt_devices_.push_back(info);
                            spdlog::debug("[BarcodeScannerSettings] BT discovered: {} ({})",
                                          info.name, info.mac);
                            overlay->populate_device_list();
                        }
                    });
                },
                disc_ctx.get());

            // Discovery completed (timeout or stopped)
            disc_ctx->token->defer([disc_ctx]() {
                if (!disc_ctx->alive.load())
                    return;
                auto* overlay = disc_ctx->overlay;
                overlay->bt_discovering_ = false;
                lv_subject_set_int(&overlay->bt_discovering_subject_, 0);
                spdlog::info("[BarcodeScannerSettings] BT discovery finished, {} scanner(s) found",
                             overlay->bt_devices_.size());
                overlay->populate_device_list();
            });
        }).detach();
    } catch (const std::system_error& e) {
        spdlog::error("[{}] Failed to spawn BT discovery thread: {}", get_name(), e.what());
        bt_discovery_ctx_->alive.store(false);
        bt_discovering_ = false;
        lv_subject_set_int(&bt_discovering_subject_, 0);
        ToastManager::instance().show(ToastSeverity::ERROR,
                                      lv_tr("Could not start Bluetooth discovery"), 3000);
        return;
    }

    spdlog::info("[{}] Started Bluetooth scanner discovery", get_name());
}

void BarcodeScannerSettingsOverlay::stop_bt_discovery() {
    if (!bt_discovering_)
        return;

    if (bt_discovery_ctx_)
        bt_discovery_ctx_->alive.store(false);

    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (bt_ctx_ && loader.stop_discovery)
        loader.stop_discovery(bt_ctx_);

    bt_discovering_ = false;
    lv_subject_set_int(&bt_discovering_subject_, 0);

    spdlog::debug("[{}] Stopped Bluetooth discovery", get_name());
}

void BarcodeScannerSettingsOverlay::pair_bt_device(const std::string& mac,
                                                    const std::string& name) {
    auto* pair_data = new PairData{mac, name};

    auto msg = fmt::format("{} {}?", lv_tr("Pair with"), name);
    auto* dialog =
        helix::ui::modal_show_confirmation(lv_tr("Pair Bluetooth Scanner"), msg.c_str(),
                                           ModalSeverity::Info, lv_tr("Pair"),
                                           on_bs_pair_confirm, on_bs_pair_cancel, pair_data);
    if (!dialog) {
        delete pair_data;
        spdlog::warn("[{}] Failed to show pairing confirmation modal", get_name());
    }
}

void BarcodeScannerSettingsOverlay::handle_bt_forget(const std::string& mac) {
    spdlog::info("[{}] Forgetting BT scanner {}", get_name(), mac);

    auto tok = lifetime_.token();
    // Wrap spawn in try/catch per feedback_no_bare_threads_arm.md (#724).
    try {
        std::thread([this, tok, mac]() {
            auto& loader = helix::bluetooth::BluetoothLoader::instance();
            if (!loader.is_available() || !loader.remove_device) {
                spdlog::error("[BarcodeScannerSettings] remove_device symbol missing");
            } else {
                auto* ctx = loader.get_or_create_context();
                if (!ctx) {
                    spdlog::error("[BarcodeScannerSettings] Failed to get BT context for remove_device");
                } else {
                    int r = loader.remove_device(ctx, mac.c_str());
                    if (r < 0) {
                        const char* err = loader.last_error ? loader.last_error(ctx) : "unknown";
                        spdlog::error("[BarcodeScannerSettings] remove_device failed for {}: r={} err={}",
                                      mac, r, err);
                        // Fall through — clear settings so UI doesn't show a stale entry
                    } else {
                        spdlog::info("[BarcodeScannerSettings] BlueZ unpair succeeded for {}", mac);
                    }
                }
            }

            if (tok.expired())
                return;
            tok.defer([this, mac]() {
                auto& settings = helix::SettingsManager::instance();
                if (settings.get_scanner_bt_address() == mac) {
                    settings.set_scanner_bt_address("");
                    settings.set_scanner_device_id("");
                    settings.set_scanner_device_name("");
                }

                bt_devices_.erase(
                    std::remove_if(bt_devices_.begin(), bt_devices_.end(),
                                   [&mac](const BtDeviceInfo& d) { return d.mac == mac; }),
                    bt_devices_.end());

                populate_device_list();

                ToastManager::instance().show(ToastSeverity::SUCCESS,
                                              lv_tr("Bluetooth scanner forgotten"), 2000);
            });
        }).detach();
    } catch (const std::system_error& e) {
        spdlog::error("[{}] Failed to spawn forget thread: {}", get_name(), e.what());
        ToastManager::instance().show(ToastSeverity::ERROR, lv_tr("Could not forget device"), 3000);
    }
}

// ============================================================================
// Static XML event callbacks
// ============================================================================

void BarcodeScannerSettingsOverlay::on_bs_scan_bluetooth(lv_event_t*) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_scan_bluetooth");
    if (!s_active_instance_) return;
    if (s_active_instance_->bt_discovering_)
        s_active_instance_->stop_bt_discovery();
    else
        s_active_instance_->start_bt_discovery();
    LVGL_SAFE_EVENT_CB_END();
}

void BarcodeScannerSettingsOverlay::on_bs_refresh_usb(lv_event_t*) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_refresh_usb");
    if (s_active_instance_) s_active_instance_->populate_device_list();
    LVGL_SAFE_EVENT_CB_END();
}

void BarcodeScannerSettingsOverlay::on_bs_keymap_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_keymap_changed");
    if (!s_active_instance_) return;
    auto* dd = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    s_active_instance_->handle_keymap_changed(static_cast<int>(lv_dropdown_get_selected(dd)));
    LVGL_SAFE_EVENT_CB_END();
}

void BarcodeScannerSettingsOverlay::on_bs_row_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_row_clicked");
    if (!s_active_instance_) return;
    auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (!row) return;
    auto* data = static_cast<RowData*>(lv_obj_get_user_data(row));
    if (!data) return;
    s_active_instance_->handle_device_selected(data->vendor_product, data->device_name,
                                                data->bt_mac);
    LVGL_SAFE_EVENT_CB_END();
}

void BarcodeScannerSettingsOverlay::on_bs_row_forget(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_row_forget");
    if (!s_active_instance_) return;
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (!btn) return;
    // Walk up until the parent is bt_list_. That node is the row.
    lv_obj_t* row = lv_obj_get_parent(btn);
    while (row && lv_obj_get_parent(row) &&
           lv_obj_get_parent(row) != s_active_instance_->bt_list_) {
        row = lv_obj_get_parent(row);
    }
    if (!row) return;
    auto* data = static_cast<RowData*>(lv_obj_get_user_data(row));
    if (!data || data->bt_mac.empty()) return;
    s_active_instance_->handle_bt_forget(data->bt_mac);
    LVGL_SAFE_EVENT_CB_END();
}

void BarcodeScannerSettingsOverlay::on_bs_auto_detect_clicked(lv_event_t*) {}

void BarcodeScannerSettingsOverlay::on_bs_pair_confirm(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_pair_confirm");

    auto* pair_data = static_cast<PairData*>(lv_event_get_user_data(e));
    if (!pair_data) {
        spdlog::warn("[BarcodeScannerSettings] on_bs_pair_confirm: null pair_data");
    } else {
        std::string mac  = pair_data->mac;
        std::string name = pair_data->name;
        delete pair_data;

        // Close the confirmation modal
        auto* top = Modal::get_top();
        if (top)
            Modal::hide(top);

        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        if (!loader.is_available() || !loader.pair) {
            ToastManager::instance().show(ToastSeverity::ERROR, lv_tr("Bluetooth not available"));
        } else if (!s_active_instance_ || !s_active_instance_->bt_ctx_) {
            ToastManager::instance().show(ToastSeverity::ERROR,
                                          lv_tr("Bluetooth not initialized"));
        } else {
            ToastManager::instance().show(ToastSeverity::INFO, lv_tr("Pairing..."), 5000);

            auto* bt_ctx = s_active_instance_->bt_ctx_;
            auto token   = s_active_instance_->lifetime_.token();

            // Wrap spawn in try/catch per feedback_no_bare_threads_arm.md (#724).
            try {
                std::thread([mac, name, bt_ctx, token]() {
                    auto& ldr   = helix::bluetooth::BluetoothLoader::instance();
                    int ret     = ldr.pair(bt_ctx, mac.c_str());
                    int paired_r = -1;
                    if (ret == 0) {
                        paired_r = ldr.is_paired ? ldr.is_paired(bt_ctx, mac.c_str()) : -1;
                        spdlog::info("[BarcodeScannerSettings] Post-pair: is_paired={}", paired_r);
                    }

                    helix::ui::queue_update([ret, mac, name, token, bt_ctx, paired_r]() {
                        if (token.expired())
                            return;

                        if (ret == 0) {
                            ToastManager::instance().show(ToastSeverity::SUCCESS,
                                                          lv_tr("Paired successfully"), 2000);

                            if (s_active_instance_) {
                                for (auto& dev : s_active_instance_->bt_devices_) {
                                    if (dev.mac == mac) {
                                        dev.paired = (paired_r == 1);
                                        break;
                                    }
                                }

                                helix::SettingsManager::instance().set_scanner_bt_address(mac);
                                helix::SettingsManager::instance().set_scanner_device_name(name);

                                s_active_instance_->refresh_current_selection_label();
                                s_active_instance_->populate_device_list();
                            }
                        } else {
                            auto& ldr2    = helix::bluetooth::BluetoothLoader::instance();
                            const char* err =
                                ldr2.last_error ? ldr2.last_error(bt_ctx) : "Unknown error";
                            spdlog::error("[BarcodeScannerSettings] Pairing failed: {}", err);
                            ToastManager::instance().show(ToastSeverity::ERROR,
                                                          lv_tr("Pairing failed"), 3000);
                        }
                    });
                }).detach();
            } catch (const std::system_error& ex) {
                spdlog::error("[BarcodeScannerSettings] Failed to spawn BT pair thread: {}",
                              ex.what());
                ToastManager::instance().show(ToastSeverity::ERROR, lv_tr("Pairing failed"), 3000);
            }
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

void BarcodeScannerSettingsOverlay::on_bs_pair_cancel(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BarcodeScannerSettings] on_bs_pair_cancel");

    auto* pair_data = static_cast<PairData*>(lv_event_get_user_data(e));
    delete pair_data;

    auto* top = Modal::get_top();
    if (top)
        Modal::hide(top);

    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
