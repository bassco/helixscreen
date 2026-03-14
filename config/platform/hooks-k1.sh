#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: Creality K1 / K1C / K1 Max / K1 SE

platform_stop_competing_uis() {
    # Stop stock Creality UI (display-server, Monitor, etc.)
    if [ -x /etc/init.d/S99start_app ]; then
        /etc/init.d/S99start_app stop 2>/dev/null || true
    fi
    # Kill any remaining stock UI processes
    for proc in display-server Monitor master-server audio-server; do
        killall "$proc" 2>/dev/null || true
    done
}

platform_enable_backlight() {
    :
}

platform_wait_for_services() {
    :
}

platform_pre_start() {
    export HELIX_CACHE_DIR="/usr/data/helixscreen/cache"
}

platform_post_stop() {
    :
}
