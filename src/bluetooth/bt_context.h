// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file bt_context.h
 * @brief Internal context structure for the Bluetooth plugin.
 *
 * This header is NOT installed — it is private to the plugin .so.
 * The main binary only sees the opaque helix_bt_context typedef from bluetooth_plugin.h.
 */

#include <systemd/sd-bus.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "bt_bus_thread.h"

/// Convert MAC address "AA:BB:CC:DD:EE:FF" to BlueZ D-Bus object path
/// "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF"
std::string mac_to_dbus_path(const char* mac);

/// BLE handle offset — handles >= this value are BLE connections, below are RFCOMM fds
static constexpr int BLE_HANDLE_OFFSET = 1000;

struct helix_bt_context {
    sd_bus* bus = nullptr;
    std::mutex mutex;
    std::string last_error;
    std::atomic<bool> discovering{false};
    sd_bus_slot* discovery_slot = nullptr;
    std::unique_ptr<helix::bluetooth::BusThread> bus_thread;

    // RFCOMM fd tracking (for safe disconnect)
    std::set<int> rfcomm_fds;

    // BLE connections
    struct BleConnection {
        std::string device_path;
        std::string char_path;
        int acquired_fd = -1;
        int notify_fd = -1;
        uint16_t mtu = 20;
        bool active = false;
    };
    std::mutex ble_mutex;
    std::vector<BleConnection> ble_connections;
};
