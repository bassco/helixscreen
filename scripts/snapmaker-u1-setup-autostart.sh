#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Set up HelixScreen auto-start on Snapmaker U1
# Run on the device: ./snapmaker-u1-setup-autostart.sh /userdata/helixscreen
#
# This script:
# 1. Creates /oem/.debug to prevent overlay wipe on boot
# 2. Patches /etc/init.d/S99screen to start HelixScreen instead of stock GUI
#
# The patch is regenerated and compared to the on-disk version every run; if
# they differ we rewrite. This is self-healing: a legacy patch from an older
# helixscreen version (e.g. one that hardcoded the init path before
# helixscreen.init moved into config/) gets repaired on the next self-update,
# and the previous "skip if first 5 lines mention HelixScreen" heuristic that
# silently locked users to a broken patch is gone.
#
# To revert: rm -rf /userdata/helixscreen && reboot
# (S99screen falls back to stock GUI when HelixScreen is not installed)

set -e

DEPLOY_DIR="${1:-/userdata/helixscreen}"

INIT_SCRIPT="$DEPLOY_DIR/config/helixscreen.init"
if [ ! -f "$INIT_SCRIPT" ]; then
    echo "Error: $INIT_SCRIPT not found"
    echo "Deploy HelixScreen first, then run this script"
    exit 1
fi

# Step 1: Create /oem/.debug to prevent overlay wipe on boot
# Without this, S01aoverlayfs runs: rm -rf /oem/overlay/*
if [ ! -f /oem/.debug ]; then
    touch /oem/.debug
    echo "Created /oem/.debug (overlay persistence enabled)"
else
    echo "/oem/.debug already exists"
fi

# Step 2: Render desired S99screen patch into a temp file
S99_TARGET=/etc/init.d/S99screen
TMP_PATCH=$(mktemp)
trap 'rm -f "$TMP_PATCH"' EXIT

cat > "$TMP_PATCH" << 'PATCH'
#!/bin/sh
#
# Start/stop GUI process
# Modified by HelixScreen: delegates to HelixScreen init when installed
#

GUI="/usr/bin/gui"
PIDFILE=/var/run/gui.pid

log()
{
	logger -p user.info -t "GUI[$$]" -- "$1"
	echo "$1"
}

# If HelixScreen is installed, delegate to its init script
for helix_init in /userdata/helixscreen/config/helixscreen.init /opt/helixscreen/config/helixscreen.init; do
    if [ -x "$helix_init" ]; then
        case "$1" in
          start)
            log "HelixScreen detected, starting instead of stock GUI"
            "$helix_init" start
            ;;
          stop)
            "$helix_init" stop
            ;;
          restart)
            "$helix_init" stop
            sleep 1
            "$helix_init" start
            ;;
          *)
            echo "Usage: $0 {start|stop|restart}"
            exit 1
        esac
        exit 0
    fi
done

# Stock GUI fallback (no HelixScreen installed)
case "$1" in
  start)
	log "Starting GUI process..."
	ulimit -c 102400
	start-stop-daemon -S -b -x "$GUI" -m -p "$PIDFILE"
	;;
  stop)
	log "Stopping GUI process..."
	start-stop-daemon -K -x "$GUI" -p "$PIDFILE" -o
	;;
  restart)
	"$0" stop
	sleep 1
	"$0" start
	;;
  *)
	echo "Usage: $0 {start|stop|restart}"
	exit 1
esac
PATCH

# Step 3: If the on-disk script already matches, nothing to do
if [ -f "$S99_TARGET" ] && cmp -s "$TMP_PATCH" "$S99_TARGET"; then
    echo "S99screen already patched (current version)"
    exit 0
fi

# Step 4: Preserve the original stock S99screen the first time we replace it.
# Detect "stock" by absence of any HelixScreen marker; once we've saved a
# .stock copy we don't overwrite it.
if [ -f "$S99_TARGET" ] && [ ! -f "$S99_TARGET.stock" ] && \
   ! grep -q HelixScreen "$S99_TARGET" 2>/dev/null; then
    cp "$S99_TARGET" "$S99_TARGET.stock"
    echo "Saved stock S99screen backup to $S99_TARGET.stock"
fi

cp "$TMP_PATCH" "$S99_TARGET"
chmod +x "$S99_TARGET"
echo "S99screen patched — HelixScreen will auto-start on boot"
echo "To revert: rm -rf $DEPLOY_DIR && reboot"
