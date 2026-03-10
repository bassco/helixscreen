// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace helix::bluetooth {

struct RfcommSendResult {
    bool success = false;
    std::string error;
};

/// Send raw data over RFCOMM to a Bluetooth device.
/// Handles the full lifecycle: init context, connect, write loop, drain, disconnect, deinit.
/// Thread-safe: serializes all RFCOMM prints through a shared mutex.
/// @param mac      Bluetooth MAC address (e.g. "AA:BB:CC:DD:EE:FF")
/// @param channel  RFCOMM channel (typically 1)
/// @param data     Raw bytes to send
/// @param log_tag  Short prefix for log messages (e.g. "Phomemo BT")
RfcommSendResult rfcomm_send(const std::string& mac, int channel,
                              const std::vector<uint8_t>& data,
                              const std::string& log_tag);

}  // namespace helix::bluetooth
