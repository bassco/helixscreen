// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file bt_ble.cpp
 * @brief BLE GATT connect, write, and disconnect via BlueZ D-Bus.
 *
 * Used by Phomemo printers that expose a BLE GATT write characteristic.
 * Attempts AcquireWrite for fast fd-based writes, falls back to WriteValue
 * D-Bus method calls per chunk.
 *
 * Also implements the unified helix_bt_disconnect() that handles both
 * RFCOMM fds (< BLE_HANDLE_OFFSET) and BLE handles (>= BLE_HANDLE_OFFSET).
 */

#include "bt_context.h"
#include "bluetooth_plugin.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

/// Wait for ServicesResolved to become true (poll D-Bus property, timeout 10s)
static int wait_services_resolved(sd_bus* bus, const char* device_path)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (true) {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        int resolved = 0;
        int r = sd_bus_get_property_trivial(bus,
                                             "org.bluez",
                                             device_path,
                                             "org.bluez.Device1",
                                             "ServicesResolved",
                                             &error,
                                             'b',
                                             &resolved);
        sd_bus_error_free(&error);

        if (r >= 0 && resolved) {
            return 0;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= 10000) {
            return -ETIMEDOUT;
        }

        // Process pending D-Bus messages and wait
        sd_bus_process(bus, nullptr);
        sd_bus_wait(bus, 200000);  // 200ms
    }
}

/// Find a GATT characteristic by UUID under the device path
static std::string find_gatt_char(sd_bus* bus, const char* device_path,
                                   const char* target_uuid)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::string char_path;

    int r = sd_bus_call_method(bus,
                               "org.bluez",
                               "/",
                               "org.freedesktop.DBus.ObjectManager",
                               "GetManagedObjects",
                               &error,
                               &reply,
                               "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return {};
    }

    std::string dev_prefix(device_path);

    // Parse: a{oa{sa{sv}}}
    r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    if (r < 0) goto done;

    while ((r = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0) {
        const char* path = nullptr;
        r = sd_bus_message_read(reply, "o", &path);
        if (r < 0) {
            sd_bus_message_exit_container(reply);
            continue;
        }

        // Only look at objects under our device path
        std::string p(path ? path : "");
        if (p.find(dev_prefix) != 0) {
            sd_bus_message_skip(reply, "a{sa{sv}}");
            sd_bus_message_exit_container(reply);
            continue;
        }

        // Check interfaces
        r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
        if (r < 0) {
            sd_bus_message_exit_container(reply);
            continue;
        }

        while ((r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
            const char* iface = nullptr;
            r = sd_bus_message_read(reply, "s", &iface);
            if (r < 0 || !iface ||
                strcmp(iface, "org.bluez.GattCharacteristic1") != 0) {
                sd_bus_message_skip(reply, "a{sv}");
                sd_bus_message_exit_container(reply);
                continue;
            }

            // Parse properties to find UUID
            r = sd_bus_message_enter_container(reply, 'a', "{sv}");
            if (r < 0) {
                sd_bus_message_exit_container(reply);
                continue;
            }

            while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
                const char* prop = nullptr;
                r = sd_bus_message_read(reply, "s", &prop);
                if (r >= 0 && prop && strcmp(prop, "UUID") == 0) {
                    const char* val = nullptr;
                    r = sd_bus_message_enter_container(reply, 'v', "s");
                    if (r >= 0) {
                        sd_bus_message_read(reply, "s", &val);
                        if (val && strcasecmp(val, target_uuid) == 0) {
                            char_path = p;
                        }
                        sd_bus_message_exit_container(reply);
                    }
                } else {
                    sd_bus_message_skip(reply, "v");
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);  // properties array
            sd_bus_message_exit_container(reply);  // interface entry

            if (!char_path.empty()) break;
        }
        sd_bus_message_exit_container(reply);  // interfaces array
        sd_bus_message_exit_container(reply);  // object entry

        if (!char_path.empty()) break;
    }
    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return char_path;
}

/// Try AcquireWrite on a GATT characteristic (BlueZ 5.46+)
/// Returns fd on success, -1 on failure (fallback to WriteValue)
static int try_acquire_write(sd_bus* bus, const char* char_path, uint16_t* out_mtu)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    // AcquireWrite takes options dict: a{sv}
    int r = sd_bus_call_method(bus,
                               "org.bluez",
                               char_path,
                               "org.bluez.GattCharacteristic1",
                               "AcquireWrite",
                               &error,
                               &reply,
                               "a{sv}", 0);  // empty options dict
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    int fd = -1;
    uint16_t mtu = 20;
    r = sd_bus_message_read(reply, "hq", &fd, &mtu);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return -1;
    }

    // fd from sd-bus is borrowed — dup it to own it
    int owned_fd = dup(fd);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if (owned_fd < 0) return -1;

    if (out_mtu) *out_mtu = mtu;
    fprintf(stderr, "[bt] AcquireWrite succeeded (fd=%d, mtu=%u)\n", owned_fd, mtu);
    return owned_fd;
}

/// Read MTU property from the device (fallback value if not available)
static uint16_t read_mtu(sd_bus* bus, const char* device_path)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    uint16_t mtu = 20;

    // Try to read Mtu from GattCharacteristic1 or the device
    // BlueZ exposes MTU on the characteristic after AcquireWrite, but if we
    // didn't get it that way, use the default BLE minimum (20).
    (void)bus;
    (void)device_path;
    sd_bus_error_free(&error);
    return mtu;
}

// ---------------------------------------------------------------------------
// Public API: BLE Connect
// ---------------------------------------------------------------------------

extern "C" int helix_bt_connect_ble(helix_bt_context* ctx, const char* mac,
                                     const char* write_uuid)
{
    if (!ctx) return -EINVAL;
    if (!mac || !write_uuid) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null MAC or UUID";
        return -EINVAL;
    }
    if (!ctx->bus) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "bus not initialized";
        return -ENODEV;
    }

    std::string device_path = mac_to_dbus_path(mac);
    fprintf(stderr, "[bt] BLE connecting to %s (%s)\n", mac, device_path.c_str());

    // Connect to the device
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(ctx->bus,
                               "org.bluez",
                               device_path.c_str(),
                               "org.bluez.Device1",
                               "Connect",
                               &error,
                               nullptr,
                               "");
    if (r < 0) {
        // Already connected is fine
        if (!sd_bus_error_has_name(&error, "org.bluez.Error.AlreadyConnected")) {
            fprintf(stderr, "[bt] BLE connect failed for %s: %s\n", mac,
                    error.message ? error.message : strerror(-r));
            {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                ctx->last_error = error.message ? error.message : "BLE connect failed";
            }
            sd_bus_error_free(&error);
            return r;
        }
    }
    sd_bus_error_free(&error);

    // Wait for services to be resolved
    r = wait_services_resolved(ctx->bus, device_path.c_str());
    if (r < 0) {
        fprintf(stderr, "[bt] timeout waiting for ServicesResolved on %s\n", mac);
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "timeout waiting for BLE services";
        return r;
    }

    // Find the GATT characteristic by UUID
    std::string char_path = find_gatt_char(ctx->bus, device_path.c_str(), write_uuid);
    if (char_path.empty()) {
        fprintf(stderr, "[bt] GATT characteristic %s not found on %s\n", write_uuid, mac);
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "GATT characteristic not found";
        return -ENOENT;
    }

    fprintf(stderr, "[bt] found GATT char: %s\n", char_path.c_str());

    // Try AcquireWrite (fast fd path)
    uint16_t mtu = 20;
    int acquired_fd = try_acquire_write(ctx->bus, char_path.c_str(), &mtu);

    if (acquired_fd < 0) {
        // Fallback: use WriteValue D-Bus calls
        mtu = read_mtu(ctx->bus, device_path.c_str());
        fprintf(stderr, "[bt] AcquireWrite unavailable, using WriteValue (mtu=%u)\n", mtu);
    }

    // Store the connection
    std::lock_guard<std::mutex> lock(ctx->ble_mutex);
    int index = static_cast<int>(ctx->ble_connections.size());

    helix_bt_context::BleConnection conn;
    conn.device_path = device_path;
    conn.char_path = char_path;
    conn.acquired_fd = acquired_fd;
    conn.mtu = mtu;
    conn.active = true;
    ctx->ble_connections.push_back(std::move(conn));

    int handle = BLE_HANDLE_OFFSET + index;
    fprintf(stderr, "[bt] BLE connected to %s (handle=%d, fd=%d, mtu=%u)\n",
            mac, handle, acquired_fd, mtu);
    return handle;
}

// ---------------------------------------------------------------------------
// Public API: BLE Write
// ---------------------------------------------------------------------------

extern "C" int helix_bt_ble_write(helix_bt_context* ctx, int handle,
                                   const uint8_t* data, int len)
{
    if (!ctx) return -EINVAL;
    if (!data || len <= 0) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null data or zero length";
        return -EINVAL;
    }
    if (handle < BLE_HANDLE_OFFSET) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "invalid BLE handle (expected >= 1000)";
        return -EINVAL;
    }

    int index = handle - BLE_HANDLE_OFFSET;

    std::lock_guard<std::mutex> lock(ctx->ble_mutex);
    if (index < 0 || index >= static_cast<int>(ctx->ble_connections.size())) {
        std::lock_guard<std::mutex> lock2(ctx->mutex);
        ctx->last_error = "BLE handle out of range";
        return -EINVAL;
    }

    auto& conn = ctx->ble_connections[static_cast<size_t>(index)];
    if (!conn.active) {
        std::lock_guard<std::mutex> lock2(ctx->mutex);
        ctx->last_error = "BLE connection not active";
        return -ENOTCONN;
    }

    // Effective payload per chunk: MTU - 3 (ATT header)
    int chunk_size = conn.mtu > 3 ? conn.mtu - 3 : conn.mtu;
    if (chunk_size <= 0) chunk_size = 20;

    int offset = 0;
    while (offset < len) {
        int remaining = len - offset;
        int to_write = remaining < chunk_size ? remaining : chunk_size;

        if (conn.acquired_fd >= 0) {
            // Fast path: write directly to acquired fd
            ssize_t written = write(conn.acquired_fd, data + offset, static_cast<size_t>(to_write));
            if (written < 0) {
                int err = errno;
                fprintf(stderr, "[bt] BLE fd write failed: %s\n", strerror(err));
                std::lock_guard<std::mutex> lock2(ctx->mutex);
                ctx->last_error = std::string("BLE write failed: ") + strerror(err);
                return -err;
            }
            // BLE printers need inter-chunk delay to avoid buffer overflow
            if (offset + to_write < len) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            // Slow path: WriteValue D-Bus method call per chunk
            sd_bus_error error = SD_BUS_ERROR_NULL;
            sd_bus_message* msg = nullptr;

            int r = sd_bus_message_new_method_call(ctx->bus, &msg,
                                                    "org.bluez",
                                                    conn.char_path.c_str(),
                                                    "org.bluez.GattCharacteristic1",
                                                    "WriteValue");
            if (r < 0) {
                fprintf(stderr, "[bt] failed to create WriteValue message: %s\n", strerror(-r));
                std::lock_guard<std::mutex> lock2(ctx->mutex);
                ctx->last_error = "failed to create WriteValue message";
                return r;
            }

            // Append data as byte array: ay
            r = sd_bus_message_append_array(msg, 'y', data + offset,
                                             static_cast<size_t>(to_write));
            if (r < 0) {
                sd_bus_message_unref(msg);
                fprintf(stderr, "[bt] failed to append data: %s\n", strerror(-r));
                std::lock_guard<std::mutex> lock2(ctx->mutex);
                ctx->last_error = "failed to append write data";
                return r;
            }

            // Append options dict: a{sv} (empty)
            r = sd_bus_message_open_container(msg, 'a', "{sv}");
            if (r >= 0) r = sd_bus_message_close_container(msg);
            if (r < 0) {
                sd_bus_message_unref(msg);
                fprintf(stderr, "[bt] failed to build options: %s\n", strerror(-r));
                std::lock_guard<std::mutex> lock2(ctx->mutex);
                ctx->last_error = "failed to build WriteValue options";
                return r;
            }

            sd_bus_message* reply = nullptr;
            r = sd_bus_call(ctx->bus, msg, 5000000, &error, &reply);  // 5s timeout
            sd_bus_message_unref(msg);
            if (reply) sd_bus_message_unref(reply);

            if (r < 0) {
                fprintf(stderr, "[bt] WriteValue failed: %s\n",
                        error.message ? error.message : strerror(-r));
                {
                    std::lock_guard<std::mutex> lock2(ctx->mutex);
                    ctx->last_error = error.message ? error.message : "WriteValue failed";
                }
                sd_bus_error_free(&error);
                return r;
            }
            sd_bus_error_free(&error);

            // BLE printers need inter-chunk delay to avoid buffer overflow
            if (offset + to_write < len) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        offset += to_write;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Public API: Disconnect (unified for RFCOMM and BLE)
// ---------------------------------------------------------------------------

extern "C" void helix_bt_disconnect(helix_bt_context* ctx, int handle)
{
    if (!ctx) return;

    if (handle >= BLE_HANDLE_OFFSET) {
        // BLE disconnect
        int index = handle - BLE_HANDLE_OFFSET;

        std::lock_guard<std::mutex> lock(ctx->ble_mutex);
        if (index < 0 || index >= static_cast<int>(ctx->ble_connections.size())) {
            return;
        }

        auto& conn = ctx->ble_connections[static_cast<size_t>(index)];
        if (!conn.active) return;

        // Close acquired fd if held
        if (conn.acquired_fd >= 0) {
            close(conn.acquired_fd);
            conn.acquired_fd = -1;
        }

        // Disconnect the device via D-Bus
        if (ctx->bus && !conn.device_path.empty()) {
            sd_bus_error error = SD_BUS_ERROR_NULL;
            sd_bus_call_method(ctx->bus,
                               "org.bluez",
                               conn.device_path.c_str(),
                               "org.bluez.Device1",
                               "Disconnect",
                               &error,
                               nullptr,
                               "");
            sd_bus_error_free(&error);
        }

        conn.active = false;
        fprintf(stderr, "[bt] BLE disconnected (handle=%d)\n", handle);
    } else {
        // RFCOMM disconnect — handle is the fd
        std::lock_guard<std::mutex> lock(ctx->mutex);
        auto it = ctx->rfcomm_fds.find(handle);
        if (it != ctx->rfcomm_fds.end()) {
            close(handle);
            ctx->rfcomm_fds.erase(it);
            fprintf(stderr, "[bt] RFCOMM disconnected (fd=%d)\n", handle);
        }
    }
}
