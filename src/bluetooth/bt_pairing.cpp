// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file bt_pairing.cpp
 * @brief BlueZ device pairing via D-Bus.
 *
 * Implements helix_bt_pair() and helix_bt_is_paired() using
 * org.bluez.Device1.Pair() and the Device1.Paired property.
 *
 * Most label printers use "Just Works" pairing (no PIN required).
 */

#include "bluetooth_plugin.h"
#include "bt_context.h"

#include <cstdio>
#include <cstring>

/// Mark a device as trusted so future connections don't require re-authorization
static void trust_device(sd_bus* bus, const char* path) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r =
        sd_bus_set_property(bus, "org.bluez", path, "org.bluez.Device1", "Trusted", &error, "b", 1);
    if (r < 0) {
        fprintf(stderr, "[bt] failed to set Trusted on %s: %s\n", path,
                error.message ? error.message : strerror(-r));
    } else {
        fprintf(stderr, "[bt] device %s marked as trusted\n", path);
    }
    sd_bus_error_free(&error);
}

extern "C" int helix_bt_pair(helix_bt_context* ctx, const char* mac) {
    if (!ctx)
        return -EINVAL;
    if (!mac) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null MAC address";
        return -EINVAL;
    }
    if (!ctx->bus || !ctx->bus_thread) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "bus not initialized";
        return -ENODEV;
    }

    std::string path = mac_to_dbus_path(mac);
    fprintf(stderr, "[bt] pairing with %s (%s)\n", mac, path.c_str());

    int r = 0;
    std::string err;

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            // Try Pair() first (Classic SPP devices)
            sd_bus_error error = SD_BUS_ERROR_NULL;
            r = sd_bus_call_method(bus, "org.bluez", path.c_str(), "org.bluez.Device1", "Pair",
                                   &error, nullptr, "");
            if (r >= 0) {
                sd_bus_error_free(&error);
                trust_device(bus, path.c_str());
                fprintf(stderr, "[bt] paired successfully with %s\n", mac);
                r = 0;
                return;
            }

            // AlreadyExists means already paired — treat as success
            if (sd_bus_error_has_name(&error, "org.bluez.Error.AlreadyExists")) {
                fprintf(stderr, "[bt] device %s already paired\n", mac);
                trust_device(bus, path.c_str());
                sd_bus_error_free(&error);
                r = 0;
                return;
            }

            // BLE devices often don't support traditional Pair() — fall back to Connect()
            // which implicitly bonds on BLE devices. Different BlueZ versions return different
            // errors (UnknownMethod, NotAvailable, BadMessage, ConnectionTimeout), so try
            // Connect() on any Pair() failure.
            fprintf(stderr, "[bt] Pair() failed for %s (%s), trying Connect() (BLE)\n", mac,
                    error.message ? error.message : strerror(-r));
            sd_bus_error_free(&error);

            sd_bus_error error2 = SD_BUS_ERROR_NULL;
            r = sd_bus_call_method(bus, "org.bluez", path.c_str(), "org.bluez.Device1", "Connect",
                                   &error2, nullptr, "");
            if (r >= 0) {
                sd_bus_error_free(&error2);
                trust_device(bus, path.c_str());
                fprintf(stderr, "[bt] connected (BLE) successfully with %s\n", mac);
                r = 0;
                return;
            }

            // AlreadyConnected is also success for our purposes
            if (sd_bus_error_has_name(&error2, "org.bluez.Error.AlreadyConnected")) {
                fprintf(stderr, "[bt] device %s already connected\n", mac);
                trust_device(bus, path.c_str());
                sd_bus_error_free(&error2);
                r = 0;
                return;
            }

            fprintf(stderr, "[bt] Connect() also failed for %s: %s\n", mac,
                    error2.message ? error2.message : strerror(-r));
            err = error2.message ? error2.message : "connect failed";
            sd_bus_error_free(&error2);
        });
    } catch (const std::exception& e) {
        err = e.what();
        r = -EIO;
    }

    if (r < 0) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = err.empty() ? strerror(-r) : err;
    }
    return r;
}

extern "C" int helix_bt_is_connected(helix_bt_context* ctx, const char* mac) {
    if (!ctx)
        return -EINVAL;
    if (!mac) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null MAC address";
        return -EINVAL;
    }
    if (!ctx->bus || !ctx->bus_thread) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "bus not initialized";
        return -ENODEV;
    }

    std::string path = mac_to_dbus_path(mac);
    int r = 0;
    int connected = 0;
    std::string err;

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            sd_bus_error error = SD_BUS_ERROR_NULL;
            r = sd_bus_get_property_trivial(bus, "org.bluez", path.c_str(), "org.bluez.Device1",
                                            "Connected", &error, 'b', &connected);
            if (r < 0) {
                fprintf(stderr, "[bt] is_connected check failed for %s: %s\n", mac,
                        error.message ? error.message : strerror(-r));
                err = error.message ? error.message : "failed to read Connected property";
            }
            sd_bus_error_free(&error);
        });
    } catch (const std::exception& e) {
        err = e.what();
        r = -EIO;
    }

    if (r < 0) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = err.empty() ? strerror(-r) : err;
        return r;
    }
    return connected ? 1 : 0;
}

extern "C" int helix_bt_is_paired(helix_bt_context* ctx, const char* mac) {
    if (!ctx)
        return -EINVAL;
    if (!mac) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null MAC address";
        return -EINVAL;
    }
    if (!ctx->bus || !ctx->bus_thread) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "bus not initialized";
        return -ENODEV;
    }

    std::string path = mac_to_dbus_path(mac);
    int r = 0;
    int paired = 0;
    std::string err;

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            sd_bus_error error = SD_BUS_ERROR_NULL;
            r = sd_bus_get_property_trivial(bus, "org.bluez", path.c_str(), "org.bluez.Device1",
                                            "Paired", &error, 'b', &paired);
            if (r < 0) {
                fprintf(stderr, "[bt] is_paired check failed for %s: %s\n", mac,
                        error.message ? error.message : strerror(-r));
                err = error.message ? error.message : "failed to read Paired property";
            }
            sd_bus_error_free(&error);
        });
    } catch (const std::exception& e) {
        err = e.what();
        r = -EIO;
    }

    if (r < 0) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = err.empty() ? strerror(-r) : err;
        return r;
    }
    return paired ? 1 : 0;
}

extern "C" int helix_bt_remove_device(helix_bt_context* ctx, const char* mac) {
    if (!ctx)
        return -EINVAL;
    if (!mac) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null MAC address";
        return -EINVAL;
    }
    if (!ctx->bus || !ctx->bus_thread) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "bus not initialized";
        return -ENODEV;
    }

    // RemoveDevice is a method on the adapter (org.bluez.Adapter1), taking
    // the device's object path as its single argument.
    std::string device_path = mac_to_dbus_path(mac);
    const char* adapter_path = "/org/bluez/hci0";
    fprintf(stderr, "[bt] removing device %s (%s)\n", mac, device_path.c_str());

    int r = 0;
    std::string err;

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            sd_bus_error error = SD_BUS_ERROR_NULL;
            const char* device_path_cstr = device_path.c_str();
            r = sd_bus_call_method(bus, "org.bluez", adapter_path, "org.bluez.Adapter1",
                                   "RemoveDevice", &error, nullptr, "o", device_path_cstr);
            if (r < 0) {
                fprintf(stderr, "[bt] RemoveDevice failed for %s: %s\n", mac,
                        error.message ? error.message : strerror(-r));
                err = error.message ? error.message : "RemoveDevice failed";
            }
            sd_bus_error_free(&error);
        });
    } catch (const std::exception& e) {
        err = e.what();
        r = -EIO;
    }

    if (r < 0) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = err.empty() ? strerror(-r) : err;
        return r;
    }

    fprintf(stderr, "[bt] device %s removed successfully\n", mac);
    return 0;
}
