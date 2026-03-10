// SPDX-License-Identifier: GPL-3.0-or-later

#include "brother_ql_bt_printer.h"
#include "bluetooth_loader.h"
#include "brother_ql_printer.h"
#include "brother_ql_protocol.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <unistd.h>

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
        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        bool success = false;
        std::string error;

        // Per-connection context
        auto* ctx = loader.init();
        if (!ctx) {
            error = "Failed to initialize Bluetooth context";
            spdlog::error("Brother QL BT: {}", error);
        } else {
            int fd = loader.connect_rfcomm(ctx, mac.c_str(), channel);
            if (fd < 0) {
                const char* err = loader.last_error ? loader.last_error(ctx) : "unknown error";
                error = fmt::format("RFCOMM connect failed: {}", err);
                spdlog::error("Brother QL BT: {}", error);
            } else {
                // Write all command data to the RFCOMM fd
                size_t total_sent = 0;
                while (total_sent < commands.size()) {
                    ssize_t sent = ::write(fd, commands.data() + total_sent,
                                           commands.size() - total_sent);
                    if (sent < 0) {
                        error = fmt::format("Write failed: {}", strerror(errno));
                        spdlog::error("Brother QL BT: {}", error);
                        break;
                    }
                    total_sent += static_cast<size_t>(sent);
                }
                if (total_sent == commands.size()) {
                    success = true;
                    spdlog::info("Brother QL BT: sent {} bytes successfully", total_sent);
                }

                loader.disconnect(ctx, fd);
            }

            loader.deinit(ctx);
        }

        helix::ui::queue_update([callback, success, error]() {
            if (callback) callback(success, error);
        });
    }).detach();
}

}  // namespace helix::label
