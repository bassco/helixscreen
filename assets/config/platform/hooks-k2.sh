#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: Creality K2 / K2 Pro / K2 Plus
#
# K2 series runs OpenWrt 21.02 with procd init system.
# Stock UI is managed by /etc/init.d/app (procd service).
# Processes: display-server, web-server, Monitor, master-server, etc.

platform_stop_competing_uis() {
    # Stop the stock Creality UI via procd (clean shutdown)
    if [ -f /etc/init.d/app ]; then
        /etc/init.d/app stop 2>/dev/null || true
    fi

    # Kill any lingering stock UI processes
    for proc in display-server Monitor master-server audio-server \
                wifi-server app-server upgrade-server; do
        killall "$proc" 2>/dev/null || true
    done

    # Note: web-server is intentionally NOT killed — it serves the
    # Creality Cloud integration and camera stream (webrtc_local).
    # Stopping it would break remote monitoring via Creality app.

    # Persistently disable the stock UI service (reversible)
    if [ -x /etc/init.d/app ]; then
        /etc/init.d/app disable 2>/dev/null || true
    fi
}

platform_enable_backlight() {
    :
}

platform_wait_for_services() {
    # Wait for Moonraker to be ready (K2 Moonraker is on port 7125)
    local timeout=30
    local elapsed=0
    while [ $elapsed -lt $timeout ]; do
        if python3 -c "
import urllib.request
try:
    urllib.request.urlopen('http://127.0.0.1:7125/server/info', timeout=2)
    exit(0)
except:
    exit(1)
" 2>/dev/null; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    echo "[hooks-k2] Warning: Moonraker not ready after ${timeout}s"
}

platform_pre_start() {
    export HELIX_CACHE_DIR="/mnt/UDISK/helixscreen/cache"

    # K2 has no curl — ensure HelixScreen knows to skip HTTPS features
    # SSL is disabled in the K2 build, but set this for safety
    export HELIX_DISABLE_SSL=1

    # The stock wifi-server manages wpa_supplicant, but we killed it in
    # platform_stop_competing_uis(). Start wpa_supplicant directly so
    # WiFi stays up. Only needed if wlan0 exists and isn't already associated.
    if [ -e /sys/class/net/wlan0 ] && ! ip addr show wlan0 2>/dev/null | grep -q 'inet '; then
        local wpa_conf="/etc/wifi/wpa_supplicant/wpa_supplicant.conf"
        if [ -f "$wpa_conf" ]; then
            # Kill any stale wpa_supplicant (wifi-server may have left one)
            killall wpa_supplicant 2>/dev/null || true
            sleep 1
            wpa_supplicant -B -i wlan0 -c "$wpa_conf" -D nl80211 2>/dev/null || true
            # Wait briefly for association, then request DHCP
            sleep 3
            udhcpc -i wlan0 -n -q 2>/dev/null || true
        fi
    fi
}

platform_post_stop() {
    # Re-enable stock UI if HelixScreen is being uninstalled
    # (installer calls this; normal stop does NOT re-enable)
    :
}
