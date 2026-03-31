#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: Snapmaker U1 (Extended Firmware)
#
# The Snapmaker U1 runs Debian Trixie with its own touchscreen UI application.
# HelixScreen renders directly to the framebuffer (/dev/fb0), so the stock
# Snapmaker touchscreen UI must be stopped to release the display.
#
# The stock touchscreen UI binary is /usr/bin/unisrv. The camera service is /usr/bin/lmd.
# SSH access: lava@<ip> (password: snapmaker) via extended firmware
# Touch input: /dev/input/event0

# Stop Snapmaker's stock touchscreen UI so HelixScreen can access the framebuffer.
platform_stop_competing_uis() {
    # Stop Snapmaker's stock UI service (name may vary with firmware version)
    for svc in unisrv lmd snapmaker-ui snapmaker-screen snapmaker-touch; do
        if systemctl is-active "$svc" >/dev/null 2>&1; then
            echo "Stopping $svc..."
            systemctl stop "$svc" 2>/dev/null || true
        fi
    done

    # Kill any remaining UI processes
    for ui in unisrv lmd snapmaker-ui snapmaker-screen KlipperScreen klipperscreen; do
        if command -v killall >/dev/null 2>&1; then
            killall "$ui" 2>/dev/null || true
        else
            for pid in $(pidof "$ui" 2>/dev/null); do
                kill "$pid" 2>/dev/null || true
            done
        fi
    done

    # Kill python-based KlipperScreen if running
    # shellcheck disable=SC2009
    for pid in $(ps aux 2>/dev/null | grep -E 'python.*screen\.py' | grep -v grep | awk '{print $2}'); do
        echo "Killing KlipperScreen python process (PID $pid)"
        kill "$pid" 2>/dev/null || true
    done

    # Brief pause to let processes exit
    sleep 1
}

# The U1 display backlight is managed by the kernel/hardware.
platform_enable_backlight() {
    return 0
}

# Debian Trixie manages services via systemd - Klipper/Moonraker should be
# available by the time HelixScreen starts.
platform_wait_for_services() {
    return 0
}

platform_pre_start() {
    export HELIX_CACHE_DIR="/opt/helixscreen/cache"
    return 0
}

platform_post_stop() {
    return 0
}
