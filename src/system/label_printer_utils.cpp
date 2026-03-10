// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "label_printer_utils.h"

#include "brother_ql_bt_printer.h"
#include "brother_ql_printer.h"
#include "label_printer_settings.h"
#include "label_renderer.h"
#include "phomemo_bt_printer.h"
#include "phomemo_printer.h"
#include "ui_update_queue.h"
#include "usb_printer_detector.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace helix {

void print_spool_label(const SpoolInfo& spool, PrintCallback callback) {
    auto& settings = LabelPrinterSettingsManager::instance();
    const std::string type = settings.get_printer_type();
    const bool is_usb = (type == "usb");
    const bool is_bt = (type == "bluetooth");

    std::vector<LabelSize> sizes;
    if (is_usb) {
        sizes = PhomemoPrinter::supported_sizes_static();
    } else if (is_bt) {
        const auto transport = settings.get_bt_transport();
        sizes = (transport == "ble") ? PhomemoPrinter::supported_sizes_static()
                                     : BrotherQLPrinter::supported_sizes_static();
    } else {
        sizes = BrotherQLPrinter::supported_sizes_static();
    }

    int size_idx = std::clamp(settings.get_label_size_index(), 0,
                              static_cast<int>(sizes.size()) - 1);
    const auto& label_size = sizes[size_idx];
    auto preset = static_cast<LabelPreset>(
        std::clamp(settings.get_label_preset(), 0, static_cast<int>(LabelPreset::MINIMAL)));

    auto bitmap = LabelRenderer::render(spool, preset, label_size);
    if (bitmap.empty()) {
        if (callback) callback(false, "Failed to render label");
        return;
    }

    if (is_usb) {
        auto detected = UsbPrinterDetector().scan();
        uint16_t vid = settings.get_usb_vid();
        uint16_t pid = settings.get_usb_pid();

        bool found = false;
        for (const auto& d : detected) {
            if (d.vid == vid && d.pid == pid) {
                found = true;
                break;
            }
        }

        if (!found && !detected.empty()) {
            vid = detected[0].vid;
            pid = detected[0].pid;
            spdlog::info("[LabelPrinter] Configured USB printer not found, using {}",
                         detected[0].product_name);
        } else if (!found) {
            helix::ui::queue_update([callback]() {
                if (callback) callback(false, "No USB printer detected");
            });
            return;
        }

        static PhomemoPrinter usb_printer;
        usb_printer.set_device(vid, pid, settings.get_usb_serial());
        usb_printer.print(bitmap, label_size, callback);
    } else if (is_bt) {
        const auto transport = settings.get_bt_transport();
        const auto bt_address = settings.get_bt_address();

        if (transport == "ble") {
            static helix::label::PhomemoBluetoothPrinter bt_phomemo;
            bt_phomemo.set_device(bt_address);
            bt_phomemo.print(bitmap, label_size, callback);
        } else {
            static helix::label::BrotherQLBluetoothPrinter bt_brother;
            bt_brother.set_device(bt_address);
            bt_brother.print(bitmap, label_size, callback);
        }
    } else {
        static BrotherQLPrinter net_printer;
        net_printer.print_label(settings.get_printer_address(), settings.get_printer_port(),
                                bitmap, label_size, callback);
    }
}

} // namespace helix
