#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: Creality K1 / K1C / K1 Max / K1 SE

platform_stop_competing_uis() {
    # Stop and persistently disable stock Creality UI (display-server, Monitor, etc.)
    # S99start_app launches the entire stock UI stack; if it remains executable it
    # will respawn every boot since it runs after S99helixscreen alphabetically.
    if [ -f /etc/init.d/S99start_app ]; then
        if [ -x /etc/init.d/S99start_app ]; then
            /etc/init.d/S99start_app stop 2>/dev/null || true
            # Persistently disable (reversible with chmod +x)
            chmod a-x /etc/init.d/S99start_app 2>/dev/null || true
        fi
        # Kill any remaining stock UI processes (full list from S99start_app)
        for proc in display-server Monitor master-server audio-server \
                    wifi-server app-server upgrade-server web-server; do
            killall "$proc" 2>/dev/null || true
        done
    fi
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
