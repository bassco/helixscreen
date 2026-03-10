// SPDX-License-Identifier: GPL-3.0-or-later

#include "phomemo_bt_printer.h"
#include "bluetooth_loader.h"
#include "phomemo_printer.h"
#include "phomemo_protocol.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <thread>

namespace helix::label {

void PhomemoBluetoothPrinter::set_device(const std::string& mac) {
    mac_ = mac;
}

std::string PhomemoBluetoothPrinter::name() const {
    return "Phomemo M110 (Bluetooth)";
}

std::vector<LabelSize> PhomemoBluetoothPrinter::supported_sizes() const {
    return helix::PhomemoPrinter::supported_sizes_static();
}

void PhomemoBluetoothPrinter::print(const LabelBitmap& bitmap, const LabelSize& size,
                                     PrintCallback callback) {
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (!loader.is_available()) {
        spdlog::error("Phomemo BT: Bluetooth not available");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth not available");
        });
        return;
    }

    if (mac_.empty()) {
        spdlog::error("Phomemo BT: No device configured");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth device not configured");
        });
        return;
    }

    auto commands = phomemo_build_raster(bitmap, size);
    spdlog::info("Phomemo BT: sending {} bytes to {} via BLE", commands.size(), mac_);

    std::string mac = mac_;

    std::thread([mac, commands = std::move(commands), callback]() {
        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        bool success = false;
        std::string error;

        // Per-connection context
        auto* ctx = loader.init();
        if (!ctx) {
            error = "Failed to initialize Bluetooth context";
            spdlog::error("Phomemo BT: {}", error);
        } else {
            int handle = loader.connect_ble(ctx, mac.c_str(), PHOMEMO_WRITE_UUID);
            if (handle < 0) {
                const char* err = loader.last_error ? loader.last_error(ctx) : "unknown error";
                error = fmt::format("BLE connect failed: {}", err);
                spdlog::error("Phomemo BT: {}", error);
            } else {
                // Plugin handles MTU chunking internally
                int ret = loader.ble_write(ctx, handle,
                                           commands.data(),
                                           static_cast<int>(commands.size()));
                if (ret < 0) {
                    const char* err = loader.last_error ? loader.last_error(ctx) : "unknown error";
                    error = fmt::format("BLE write failed: {}", err);
                    spdlog::error("Phomemo BT: {}", error);
                } else {
                    success = true;
                    spdlog::info("Phomemo BT: sent {} bytes successfully", commands.size());
                }

                loader.disconnect(ctx, handle);
            }

            loader.deinit(ctx);
        }

        helix::ui::queue_update([callback, success, error]() {
            if (callback) callback(success, error);
        });
    }).detach();
}

}  // namespace helix::label
