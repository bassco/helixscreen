// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file bt_discovery_utils.h
 * @brief Bluetooth discovery UUID classification helpers.
 *
 * Extracted from bt_discovery.cpp so the logic can be unit-tested without
 * pulling in sd-bus or BlueZ dependencies.
 */

#include <cstring>

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

namespace helix::bluetooth {

/// Target service UUID prefixes (8-char hex)
inline constexpr const char* SPP_UUID_PREFIX = "00001101";
inline constexpr const char* PHOMEMO_BLE_UUID_PREFIX = "0000ff00";

/// Check if a UUID string matches one of our target label printer service UUIDs.
/// Comparison is case-insensitive on the first 8 hex characters (the UUID16 prefix).
inline bool is_label_printer_uuid(const char* uuid)
{
    if (!uuid) return false;
    if (strncasecmp(uuid, SPP_UUID_PREFIX, 8) == 0) return true;
    if (strncasecmp(uuid, PHOMEMO_BLE_UUID_PREFIX, 8) == 0) return true;
    return false;
}

/// Determine if a UUID indicates a BLE device (Phomemo BLE service).
inline bool uuid_is_ble(const char* uuid)
{
    if (!uuid) return false;
    return strncasecmp(uuid, PHOMEMO_BLE_UUID_PREFIX, 8) == 0;
}

/// Known printer brand entry for unified matching.
struct PrinterBrand {
    const char* prefix;
    bool is_ble;     // true = BLE-only (no SPP/RFCOMM)
    bool is_brother;  // true = Brother QL protocol
};

// Shared table of known label printer brands.
// Phomemo M/Q series support both BLE and SPP but need SPP for printing.
inline constexpr PrinterBrand KNOWN_BRANDS[] = {
    // Brother QL/PT/TD/RJ series — SPP only
    {"QL-", false, true},
    {"PT-", false, true},
    {"TD-", false, true},
    {"RJ-", false, true},

    // Phomemo — dual-mode but prints via SPP
    {"Phomemo", false, false},

    // Dymo — SPP
    {"LW-", false, false},
    {"DYMO", false, false},

    // MUNBYN — SPP
    {"MUNBYN", false, false},

    // Niimbot — BLE only
    {"Niimbot", true, false},
    {"B21", true, false},
    {"D11", true, false},
    {"D110", true, false},

    // Supvan / KataSymbol — BLE only
    {"Supvan", true, false},
    {"KataSymbol", true, false},
    {"E10", true, false},
    {"E16", true, false},
    {"T50M", true, false},
};

/// Find a matching brand entry for a device name, or nullptr if unknown.
inline const PrinterBrand* find_brand(const char* name)
{
    if (!name || !name[0]) return nullptr;

    for (const auto& brand : KNOWN_BRANDS) {
        if (strncasecmp(name, brand.prefix, strlen(brand.prefix)) == 0) return &brand;
    }

    // Phomemo M/Q series: single letter + digits (e.g. M110, Q199)
    if ((name[0] == 'M' || name[0] == 'Q') && name[1] >= '0' && name[1] <= '9') {
        // Return a static entry for this pattern
        static constexpr PrinterBrand phomemo_mq = {"M/Q", false, false};
        return &phomemo_mq;
    }

    return nullptr;
}

/// Determine if a device name indicates a BLE-only printer (no SPP/RFCOMM).
/// Used when UUID-based detection isn't available (UUIDs resolve late on BLE).
inline bool name_suggests_ble(const char* name)
{
    auto* brand = find_brand(name);
    return brand && brand->is_ble;
}

/// Check if a device name looks like a label printer.
inline bool is_likely_label_printer(const char* name)
{
    if (find_brand(name)) return true;

    // Generic keyword match
    if (!name || !name[0]) return false;
    if (strcasestr(name, "printer") != nullptr) return true;
    if (strcasestr(name, "label") != nullptr) return true;
    return false;
}

/// Check if a device name indicates a Brother QL-family printer.
inline bool is_brother_printer(const char* name)
{
    auto* brand = find_brand(name);
    return brand && brand->is_brother;
}

}  // namespace helix::bluetooth
