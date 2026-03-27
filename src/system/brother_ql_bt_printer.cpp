// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_LABEL_PRINTER

#include "brother_ql_bt_printer.h"
#include "bluetooth_loader.h"
#include "brother_ql_printer.h"
#include "brother_ql_protocol.h"
#include "bt_print_utils.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <thread>

namespace helix::label {

void BrotherQLBluetoothPrinter::set_device(const std::string& mac, int channel) {
    mac_ = mac;
    channel_ = channel;
}

std::string BrotherQLBluetoothPrinter::name() const {
    return "Brother QL (Bluetooth)";
}

std::vector<LabelSize> BrotherQLBluetoothPrinter::supported_sizes() const {
    return helix::BrotherQLPrinter::supported_sizes_static();
}

void BrotherQLBluetoothPrinter::print(const LabelBitmap& bitmap, const LabelSize& size,
                                       PrintCallback callback) {
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (!loader.is_available()) {
        spdlog::error("Brother QL BT: Bluetooth not available");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth not available");
        });
        return;
    }

    if (mac_.empty()) {
        spdlog::error("Brother QL BT: No device configured");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth device not configured");
        });
        return;
    }

    auto commands = brother_ql_build_raster(bitmap, size);
    spdlog::info("Brother QL BT: sending {} bytes to {} ch{}", commands.size(), mac_, channel_);

    std::string mac = mac_;
    int channel = channel_;

    std::thread([mac, channel, commands = std::move(commands), callback]() {
        auto result = helix::bluetooth::rfcomm_send(mac, channel, commands, "Brother QL BT");

        helix::ui::queue_update([callback, result]() {
            if (callback) callback(result.success, result.error);
        });
    }).detach();
}

}  // namespace helix::label

#endif // HELIX_HAS_LABEL_PRINTER
