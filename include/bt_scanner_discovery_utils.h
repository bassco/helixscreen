// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file bt_scanner_discovery_utils.h
 * @brief Bluetooth discovery UUID classification for HID barcode/QR scanners.
 *
 * Parallels bt_discovery_utils.h (label printers). Provides UUID-based and
 * name-based classification for BT HID scanners.
 */

#include "bt_discovery_utils.h"

#include <cstring>

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

namespace helix::bluetooth {

/// HID UUID prefix (Classic Bluetooth HID Profile)
inline constexpr const char* HID_UUID_PREFIX = "00001124";
/// HID-over-GATT UUID prefix (BLE HID)
inline constexpr const char* HOGP_UUID_PREFIX = "00001812";

/// Check if a UUID matches a HID scanner service UUID.
/// Matches classic HID (0x1124) and HID-over-GATT (0x1812).
inline bool is_hid_scanner_uuid(const char* uuid) {
    if (!uuid || !uuid[0])
        return false;
    if (strncasecmp(uuid, HID_UUID_PREFIX, 8) == 0)
        return true;
    if (strncasecmp(uuid, HOGP_UUID_PREFIX, 8) == 0)
        return true;
    return false;
}

/// Known scanner brand prefixes for name-based fallback detection.
inline constexpr const char* KNOWN_SCANNER_BRANDS[] = {
    "Tera", "Netum", "Symcode", "Inateck", "Eyoyo",
};

/// Check if a device name looks like a barcode/QR scanner.
/// Uses keyword matching ("barcode", "scanner") and known brand names.
/// Rejects known label printer names to avoid false positives.
inline bool is_likely_bt_scanner(const char* name) {
    if (!name || !name[0])
        return false;

    // Reject known label printers first (they may contain "scanner" in some configs)
    if (is_likely_label_printer(name))
        return false;

    // Keyword match
    if (strcasestr(name, "barcode") != nullptr)
        return true;
    if (strcasestr(name, "scanner") != nullptr)
        return true;

    // Known brand match
    for (const auto* brand : KNOWN_SCANNER_BRANDS) {
        if (strncasecmp(name, brand, strlen(brand)) == 0)
            return true;
    }

    return false;
}

} // namespace helix::bluetooth
