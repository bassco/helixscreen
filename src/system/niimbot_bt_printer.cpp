// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_LABEL_PRINTER

#include "niimbot_bt_printer.h"
#include "bluetooth_loader.h"
#include "niimbot_protocol.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <mutex>
#include <thread>

namespace helix::label {

// Serialize BLE print jobs and protect persistent connection state
static std::mutex s_print_mutex;

// Persistent BLE connection — survives across prints
static helix_bt_context* s_ctx = nullptr;
static int s_handle = -1;
static std::string s_connected_mac;

// Last connect error, captured before ensure_connected tears down s_ctx so
// the caller can still surface the real BlueZ diagnostic instead of
// "unknown error".
static std::string s_last_connect_error;

/// Try a test write to check if the connection is still alive
static bool connection_alive(helix::bluetooth::BluetoothLoader& loader) {
    if (!s_ctx || s_handle < 0) return false;

    // Send a Heartbeat (0xDC) as a connection test — lightweight, always accepted
    auto pkt = niimbot_build_packet(NiimbotCmd::Heartbeat, uint8_t(0x01));
    int ret = loader.ble_write(s_ctx, s_handle, pkt.data(), static_cast<int>(pkt.size()));
    if (ret < 0) {
        spdlog::debug("Niimbot BT: persistent connection dead (write failed), will reconnect");
        return false;
    }

    // Verify heartbeat response — a successful write alone doesn't prove liveness
    if (loader.ble_read) {
        uint8_t resp[64];
        int n = loader.ble_read(s_ctx, s_handle, resp, sizeof(resp), 500);
        if (n <= 0) {
            spdlog::debug("Niimbot BT: persistent connection dead (no heartbeat response), will reconnect");
            return false;
        }
    }
    return true;
}

/// Ensure we have a live BLE connection to the given MAC, reusing existing if possible
static int ensure_connected(helix::bluetooth::BluetoothLoader& loader, const std::string& mac) {
    // Reuse existing connection if it's to the same device and still alive
    if (s_connected_mac == mac && connection_alive(loader)) {
        spdlog::debug("Niimbot BT: reusing persistent connection (handle={})", s_handle);
        return s_handle;
    }

    // Clean up stale connection
    if (s_ctx) {
        if (s_handle >= 0) {
            loader.disconnect(s_ctx, s_handle);
            s_handle = -1;
        }
        loader.deinit(s_ctx);
        s_ctx = nullptr;
    }
    s_connected_mac.clear();

    // Create fresh connection
    s_ctx = loader.init();
    if (!s_ctx) {
        s_last_connect_error = "bluetooth init failed";
        return -1;
    }

    s_handle = loader.connect_ble(s_ctx, mac.c_str(), NIIMBOT_WRITE_CHAR_UUID);
    if (s_handle < 0) {
        // Snapshot the real error before we tear down the context — the
        // caller needs it for the toast/log, and last_error(ctx) returns
        // nothing useful after deinit.
        if (loader.last_error) {
            const char* err = loader.last_error(s_ctx);
            s_last_connect_error = (err && *err) ? err : "connect failed";
        } else {
            s_last_connect_error = "connect failed";
        }
        loader.deinit(s_ctx);
        s_ctx = nullptr;
        return -1;
    }

    s_last_connect_error.clear();
    s_connected_mac = mac;
    spdlog::warn("Niimbot BT: new persistent connection (handle={})", s_handle);

    // BLE connection settle time — AcquireWrite uses write-without-response,
    // and the printer firmware may silently discard data received before its
    // BLE stack is fully initialized. Wait for the connection to stabilize.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Send Connect (0xC1) — printer-level session handshake.
    // This arms the thermal subsystem for printing.
    auto connect_pkt = niimbot_build_packet(NiimbotCmd::Connect, uint8_t(0x01));
    int connect_write = loader.ble_write(s_ctx, s_handle, connect_pkt.data(),
                                          static_cast<int>(connect_pkt.size()));
    if (connect_write < 0) {
        const char* err = loader.last_error ? loader.last_error(s_ctx) : "unknown";
        spdlog::warn("Niimbot BT: Connect handshake write failed: {}", err);
    }
    if (loader.ble_read) {
        uint8_t resp[64];
        int n = loader.ble_read(s_ctx, s_handle, resp, sizeof(resp), 2000);
        if (n > 0) {
            std::string hex;
            for (int i = 0; i < n; i++) {
                if (!hex.empty()) hex += ' ';
                hex += fmt::format("{:02X}", resp[i]);
            }
            spdlog::debug("Niimbot BT: Connect response: {}", hex);
        } else {
            spdlog::warn("Niimbot BT: Connect handshake got no response (n={})", n);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    spdlog::info("Niimbot BT: connection initialized");

    return s_handle;
}

void NiimbotBluetoothPrinter::set_device(const std::string& mac, const std::string& device_name) {
    mac_ = mac;
    name_ = device_name;
}

std::string NiimbotBluetoothPrinter::name() const {
    return "Niimbot (Bluetooth)";
}

std::vector<LabelSize> NiimbotBluetoothPrinter::supported_sizes() const {
    return niimbot_sizes_for_model(name_);
}

void NiimbotBluetoothPrinter::print(const LabelBitmap& bitmap, const LabelSize& size,
                                     PrintCallback callback) {
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (!loader.is_available()) {
        spdlog::error("Niimbot BT: Bluetooth not available");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth not available");
        });
        return;
    }

    if (mac_.empty()) {
        spdlog::error("Niimbot BT: No device configured");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth device not configured");
        });
        return;
    }

    // Log bitmap stats for debugging
    {
        int black_pixels = 0;
        for (int y = 0; y < bitmap.height(); y++) {
            const uint8_t* row = bitmap.row_data(y);
            for (int b = 0; b < bitmap.row_byte_width(); b++) {
                uint8_t v = row[b];
                while (v) { black_pixels++; v &= (v - 1); }
            }
        }
        spdlog::debug("Niimbot BT: bitmap {}x{}, {} black pixels",
                      bitmap.width(), bitmap.height(), black_pixels);
    }

    auto job = niimbot_build_print_job(bitmap, size);
    spdlog::warn("Niimbot BT: {} packets, {} rows to {} via BLE",
                 job.packets.size(), job.total_rows, mac_);

    std::string mac = mac_;

    // All state needed by the detached thread is captured by value/move.
    // The NiimbotBluetoothPrinter object may be destroyed before thread completes.
    // Wrap spawn in try/catch — pthread_create EAGAIN on resource-constrained ARM
    // (AD5M/CC1) throws std::system_error which aborts with std::terminate if it
    // escapes an LVGL event frame (#724, #837, [L083]).
    try {
        std::thread([mac, job = std::move(job), callback]() {
        std::lock_guard<std::mutex> lock(s_print_mutex);

        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        bool success = false;
        std::string error;

        // Helper: read and log a response from the printer (if read capability available)
        auto read_response = [&loader](helix_bt_context* ctx, int handle, const char* label) {
            if (!loader.ble_read) return;
            uint8_t resp[64];
            int n = loader.ble_read(ctx, handle, resp, sizeof(resp), 3000);
            if (n > 0) {
                std::string hex;
                for (int i = 0; i < n; i++) {
                    if (!hex.empty()) hex += ' ';
                    hex += fmt::format("{:02X}", resp[i]);
                }
                spdlog::debug("Niimbot BT: {} response ({} bytes): {}", label, n, hex);
            } else if (n == 0) {
                spdlog::warn("Niimbot BT: {} response timeout", label);
            } else {
                spdlog::warn("Niimbot BT: {} response read error: {}", label, n);
            }
        };

        int handle = ensure_connected(loader, mac);
        if (handle < 0) {
            std::string err =
                s_last_connect_error.empty() ? "unknown error" : s_last_connect_error;
            error = fmt::format("BLE connect failed: {}", err);
            spdlog::error("Niimbot BT: {}", error);
        } else {
            // Send all packets sequentially with inter-packet delay
            bool write_ok = true;
            int ret = 0;
            for (size_t i = 0; i < job.packets.size(); i++) {
                const auto& pkt = job.packets[i];
                ret = loader.ble_write(s_ctx, handle, pkt.data(),
                                        static_cast<int>(pkt.size()));
                if (ret < 0) {
                    const char* err = loader.last_error ? loader.last_error(s_ctx) : "unknown error";
                    error = fmt::format("BLE write failed at packet {}: {}", i, err);
                    spdlog::error("Niimbot BT: {}", error);
                    write_ok = false;
                    // Connection is broken — clear persistent state so we reconnect next time
                    s_handle = -1;
                    s_connected_mac.clear();
                    break;
                }

                // Determine if this is an image row packet (fire-and-forget, no response)
                uint8_t cmd = pkt[2]; // command byte is at index 2
                bool is_image = (cmd == static_cast<uint8_t>(NiimbotCmd::PrintBitmapRow) ||
                                 cmd == static_cast<uint8_t>(NiimbotCmd::PrintBitmapRowIndexed) ||
                                 cmd == static_cast<uint8_t>(NiimbotCmd::PrintEmptyRow));

                if (is_image) {
                    // Image rows: fire-and-forget for speed, printer buffers these
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } else {
                    // Command packets: read response (ACK)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    read_response(s_ctx, handle, fmt::format("cmd[{}] 0x{:02X}", i, cmd).c_str());
                }
            }

            // Poll PrintStatus and send PrintEnd as soon as we get first B3 response.
            // The D110 auto-repeats copies — sending PrintEnd early (at first B3)
            // stops it after the current page while preserving content.
            // Persistent connections are required — disconnect/reconnect causes blank output.
            if (write_ok) {
                spdlog::info("Niimbot BT: all {} packets sent, polling for completion",
                             job.packets.size());

                bool print_end_sent = false;

                if (loader.ble_read) {
                    auto status_pkt = niimbot_build_packet(NiimbotCmd::PrintStatus, uint8_t(0x01));
                    for (int poll = 0; poll < 60; poll++) {  // Up to 60 × 250ms = 15s
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        ret = loader.ble_write(s_ctx, handle, status_pkt.data(),
                                                static_cast<int>(status_pkt.size()));
                        if (ret < 0) {
                            spdlog::warn("Niimbot BT: PrintStatus write failed");
                            break;
                        }

                        uint8_t resp[64];
                        int n = loader.ble_read(s_ctx, handle, resp, sizeof(resp), 3000);
                        if (n > 0) {
                            std::string hex;
                            for (int j = 0; j < n; j++) {
                                if (!hex.empty()) hex += ' ';
                                hex += fmt::format("{:02X}", resp[j]);
                            }
                            spdlog::debug("Niimbot BT: PrintStatus[{}]: {}", poll, hex);

                            for (int j = 0; j < n - 4; j++) {
                                if (resp[j] == 0xB3) {
                                    uint8_t len = resp[j + 1];
                                    if (len >= 4 && j + 2 + len <= n) {
                                        uint16_t page = (resp[j + 2] << 8) | resp[j + 3];
                                        uint8_t print_progress = resp[j + 4];
                                        uint8_t feed_progress = resp[j + 5];
                                        spdlog::debug("Niimbot BT: page={}, printProgress={}, feedProgress={}",
                                                      page, print_progress, feed_progress);

                                        // Send PrintEnd at first B3 response — printer is
                                        // actively processing, so it will finish the current
                                        // page but not start another copy.
                                        if (!print_end_sent) {
                                            auto end_pkt = niimbot_build_packet(NiimbotCmd::PrintEnd, uint8_t(0x01));
                                            ret = loader.ble_write(s_ctx, handle, end_pkt.data(),
                                                                    static_cast<int>(end_pkt.size()));
                                            if (ret < 0) {
                                                spdlog::warn("Niimbot BT: PrintEnd write failed");
                                            } else {
                                                spdlog::info("Niimbot BT: PrintEnd sent at page={}, printProgress={}",
                                                             page, print_progress);
                                            }
                                            print_end_sent = true;
                                        }

                                        // Done when page >= 1 (copy completed and fed)
                                        if (page >= 1) {
                                            goto print_done;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }

                // Fallback: if polling didn't send PrintEnd, send it now
                if (!print_end_sent) {
                    spdlog::warn("Niimbot BT: polling timed out, sending PrintEnd");
                    auto end_pkt = niimbot_build_packet(NiimbotCmd::PrintEnd, uint8_t(0x01));
                    loader.ble_write(s_ctx, handle, end_pkt.data(),
                                      static_cast<int>(end_pkt.size()));
                }

                print_done:
                success = true;
                spdlog::warn("Niimbot BT: print job complete");
            }

            // Keep connection alive for subsequent prints.
            // Disconnect/reconnect causes blank output — the printer only prints
            // content correctly on a persistent BLE connection.
        }

        helix::ui::queue_update([callback, success, error]() {
            if (callback) callback(success, error);
        });
        }).detach();
    } catch (const std::system_error& e) {
        spdlog::error("Niimbot BT: failed to spawn print thread: {}", e.what());
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "System busy — please try again");
        });
    }
}

}  // namespace helix::label

#endif // HELIX_HAS_LABEL_PRINTER
