// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file bluetooth_plugin.h
 * @brief C ABI for the Bluetooth plugin shared library (libhelix-bluetooth.so)
 *
 * This header defines the interface between the main HelixScreen binary and the
 * optional Bluetooth plugin. The main binary loads the plugin via dlopen() and
 * resolves these function pointers via dlsym().
 *
 * The plugin handles all BlueZ/D-Bus interaction — the main binary never links
 * libsystemd or libbluetooth for Bluetooth support.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HELIX_BT_API_VERSION 1

/// Plugin metadata returned by helix_bt_get_info()
typedef struct {
    int api_version;
    const char* name;
    bool has_classic;  ///< Supports SPP/RFCOMM
    bool has_ble;      ///< Supports BLE GATT
} helix_bt_plugin_info;

/// Discovered Bluetooth device
typedef struct {
    const char* mac;           ///< "AA:BB:CC:DD:EE:FF"
    const char* name;          ///< Human-readable name
    bool paired;
    bool is_ble;               ///< false=Classic, true=BLE
    const char* service_uuid;  ///< Primary service UUID (SPP or vendor)
} helix_bt_device;

/// Opaque context — allocated by plugin, freed by plugin
typedef struct helix_bt_context helix_bt_context;

/// Discovery callback — called per device found. user_data is pass-through.
typedef void (*helix_bt_discover_cb)(const helix_bt_device* dev, void* user_data);

// ============================================================================
// Function pointer typedefs for dlsym() resolution
// ============================================================================

/// Return plugin metadata. Must return api_version == HELIX_BT_API_VERSION.
typedef helix_bt_plugin_info* (*helix_bt_get_info_fn)(void);

/// Initialize plugin. Starts internal sd-bus event loop thread.
/// Returns context on success, NULL on failure.
typedef helix_bt_context* (*helix_bt_init_fn)(void);

/// Shut down plugin. Stops event loop, frees resources.
typedef void (*helix_bt_deinit_fn)(helix_bt_context*);

/// Start discovery. Calls cb per device found. Stops after timeout_ms.
/// Returns 0 on success, negative on error.
typedef int (*helix_bt_discover_fn)(helix_bt_context*, int timeout_ms,
                                    helix_bt_discover_cb cb, void* user_data);

/// Stop an in-progress discovery early.
typedef void (*helix_bt_stop_discovery_fn)(helix_bt_context*);

/// Pair with a device. Blocks until pairing completes or fails.
/// Returns 0 on success, negative on error.
typedef int (*helix_bt_pair_fn)(helix_bt_context*, const char* mac);

/// Check if device is paired. Returns 1=paired, 0=not paired, negative=error.
typedef int (*helix_bt_is_paired_fn)(helix_bt_context*, const char* mac);

/// Connect via RFCOMM (SPP). Returns fd on success, negative on error.
typedef int (*helix_bt_connect_rfcomm_fn)(helix_bt_context*, const char* mac, int channel);

/// Connect via BLE GATT. Returns handle on success, negative on error.
typedef int (*helix_bt_connect_ble_fn)(helix_bt_context*, const char* mac,
                                       const char* write_uuid);

/// Write data to BLE GATT characteristic. Handles chunking to MTU internally.
/// Returns 0 on success, negative on error.
typedef int (*helix_bt_ble_write_fn)(helix_bt_context*, int handle,
                                     const uint8_t* data, int len);

/// Disconnect a connection (RFCOMM fd or BLE handle).
typedef void (*helix_bt_disconnect_fn)(helix_bt_context*, int handle);

/// Get human-readable error string for last failure.
typedef const char* (*helix_bt_last_error_fn)(helix_bt_context*);

// ============================================================================
// Symbol name macros for dlsym()
// ============================================================================

#define HELIX_BT_SYM_GET_INFO       "helix_bt_get_info"
#define HELIX_BT_SYM_INIT           "helix_bt_init"
#define HELIX_BT_SYM_DEINIT         "helix_bt_deinit"
#define HELIX_BT_SYM_DISCOVER       "helix_bt_discover"
#define HELIX_BT_SYM_STOP_DISCOVERY "helix_bt_stop_discovery"
#define HELIX_BT_SYM_PAIR           "helix_bt_pair"
#define HELIX_BT_SYM_IS_PAIRED      "helix_bt_is_paired"
#define HELIX_BT_SYM_CONNECT_RFCOMM "helix_bt_connect_rfcomm"
#define HELIX_BT_SYM_CONNECT_BLE    "helix_bt_connect_ble"
#define HELIX_BT_SYM_BLE_WRITE      "helix_bt_ble_write"
#define HELIX_BT_SYM_DISCONNECT     "helix_bt_disconnect"
#define HELIX_BT_SYM_LAST_ERROR     "helix_bt_last_error"

#ifdef __cplusplus
}
#endif
