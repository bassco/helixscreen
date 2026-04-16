#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: AD5M with ForgeX firmware
#
# ForgeX runs BusyBox init (SysV-style) with a chroot environment at
# /data/.mod/.forge-x containing Python utilities for hardware control.
# The AD5M has only ~107MB RAM, so boot sequencing matters greatly.
#
# Key coordination point: /tmp/helixscreen_active
#   ForgeX's S99root script checks this flag. When present, S99root skips
#   its own screen output (boot logo, status messages) so it doesn't stomp
#   on HelixScreen's framebuffer. ForgeX's screen.sh also checks this flag
#   to skip backlight dimming (eco mode) while HelixScreen is running.
#   The flag is set in platform_pre_start() and removed in platform_post_stop().

# shellcheck disable=SC3043  # local is supported by BusyBox ash

# ForgeX chroot location
FORGEX_CHROOT="/data/.mod/.forge-x"

# Backlight control script path (INSIDE the chroot, not on the host)
FORGEX_BACKLIGHT="/root/printer_data/py/backlight.py"

# Stop stock FlashForge UI and competing screen UIs.
# The AD5M stock firmware runs ffstartup-arm which launches firmwareExe
# (the stock Qt touchscreen UI). Both must be killed for HelixScreen to
# have exclusive framebuffer access.
platform_stop_competing_uis() {
    # Stop stock FlashForge firmware UI (ffstartup-arm -> firmwareExe)
    if [ -f /opt/PROGRAM/ffstartup-arm ]; then
        echo "Stopping stock FlashForge UI..."
        if command -v killall >/dev/null 2>&1; then
            killall firmwareExe 2>/dev/null || true
            killall ffstartup-arm 2>/dev/null || true
        else
            for pid in $(pidof firmwareExe 2>/dev/null); do
                kill "$pid" 2>/dev/null || true
            done
            for pid in $(pidof ffstartup-arm 2>/dev/null); do
                kill "$pid" 2>/dev/null || true
            done
        fi
    fi

    # Stop any known competing third-party UIs
    for ui in guppyscreen GuppyScreen KlipperScreen klipperscreen featherscreen FeatherScreen; do
        # Stop via init scripts if they exist
        for initscript in /etc/init.d/S*"${ui}"* /opt/config/mod/.root/S*"${ui}"*; do
            if [ -x "$initscript" ] 2>/dev/null; then
                echo "Stopping competing UI: $initscript"
                "$initscript" stop 2>/dev/null || true
            fi
        done
        # Kill remaining processes
        if command -v killall >/dev/null 2>&1; then
            killall "$ui" 2>/dev/null || true
        else
            for pid in $(pidof "$ui" 2>/dev/null); do
                kill "$pid" 2>/dev/null || true
            done
        fi
    done

    # Brief pause to let processes exit
    sleep 1
}

# Enable the display backlight via ForgeX's chroot Python utility.
# ForgeX may leave the backlight off when display mode is STOCK or during
# boot transitions. We explicitly set it to 100% before starting HelixScreen.
# The backlight.py script uses ioctl calls that must run inside the chroot.
platform_enable_backlight() {
    local full_backlight_path="${FORGEX_CHROOT}${FORGEX_BACKLIGHT}"
    if [ -d "$FORGEX_CHROOT" ] && [ -x "$full_backlight_path" ]; then
        echo "Enabling backlight via ForgeX chroot..."
        /usr/sbin/chroot "$FORGEX_CHROOT" "$FORGEX_BACKLIGHT" 100 2>/dev/null || true
        return 0
    fi

    echo "Warning: Could not enable backlight (chroot=$FORGEX_CHROOT, script=$full_backlight_path)"
    return 1
}

# Wait for Moonraker to become responsive before starting HelixScreen.
# On the AD5M's ~107MB RAM, launching helix-screen while moonraker is still
# initializing causes severe swap thrashing. By waiting here with only the
# lightweight splash screen running, moonraker can start without memory
# competition, dramatically improving total boot time.
platform_wait_for_services() {
    # Only wait on ForgeX -- it has the memory constraints
    if [ ! -d "$FORGEX_CHROOT" ]; then
        return 0
    fi

    # Check if moonraker is even enabled (ForgeX can disable it)
    local moonraker_disabled
    moonraker_disabled=$(/usr/sbin/chroot "$FORGEX_CHROOT" /bin/sh -c \
        'cd / 2>/dev/null; /opt/config/mod/.shell/commands/zconf.sh /opt/config/mod_data/variables.cfg --get "disable_moonraker" "0"' 2>/dev/null) || true
    if [ "$moonraker_disabled" = "1" ]; then
        echo "Moonraker disabled, skipping wait"
        return 0
    fi

    echo "Waiting for Moonraker (reduces memory pressure)..."
    local timeout=120
    local waited=0
    while [ "$waited" -lt "$timeout" ]; do
        # Use wget since curl may not be available on BusyBox base system
        if wget -q -O /dev/null --timeout=1 http://localhost:7125/server/info 2>/dev/null; then
            echo "Moonraker ready after ${waited}s"
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
        # Progress indicator every 10 seconds
        if [ $((waited % 10)) -eq 0 ]; then
            echo "  Still waiting for Moonraker... (${waited}s)"
        fi
    done

    echo "Warning: Moonraker not ready after ${timeout}s, starting anyway"
    return 1
}

# Pre-start setup: set the active flag so ForgeX knows HelixScreen owns the display.
# This must happen BEFORE stopping competing UIs or enabling backlight, because
# ForgeX's screen.sh could run at any time via Klipper's delayed_gcode.
platform_pre_start() {
    export HELIX_CACHE_DIR="/data/helixscreen/cache"
    touch /tmp/helixscreen_active
}

# Wait for ForgeX boot sequence to complete before starting helix-screen.
# S99root runs AFTER S90helixscreen and writes directly to /dev/fb0 (boot logos,
# status messages, logged binary). Even with screen.sh patches, S99root can
# outlive Moonraker startup and stomp on the framebuffer after helix-screen launches.
# By waiting for S99root to exit, we guarantee a clean handoff.
platform_wait_for_boot_complete() {
    local s99root="/opt/config/mod/.root/S99root"
    if [ ! -f "$s99root" ]; then
        return 0
    fi

    echo "Waiting for ForgeX boot to complete..."
    local timeout=60
    local waited=0

    while [ "$waited" -lt "$timeout" ]; do
        # BusyBox-compatible process check for S99root script
        # shellcheck disable=SC2009  # pgrep not available on all BusyBox builds
        if ! ps w 2>/dev/null | grep -v grep | grep -q "S99root"; then
            echo "ForgeX boot complete after ${waited}s"
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
        if [ $((waited % 10)) -eq 0 ]; then
            echo "  Still waiting for ForgeX boot... (${waited}s)"
        fi
    done

    echo "Warning: ForgeX boot still running after ${timeout}s, starting anyway"
    return 1
}

# Post-stop cleanup: remove the active flag so ForgeX can resume normal display control.
# After this, S99root and screen.sh will behave as if no third-party UI is present.
platform_post_stop() {
    rm -f /tmp/helixscreen_active
}
