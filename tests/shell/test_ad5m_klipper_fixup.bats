#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for fix_ad5m_klipper_config() in platform.sh
# Verifies the AD5M screw_thread fixup is safe and correct.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Reset globals
    KLIPPER_USER=""
    KLIPPER_HOME=""
    AD5M_FIRMWARE=""
    INSTALL_DIR="/opt/helixscreen"
    SUDO=""

    # Source platform.sh
    unset _HELIX_PLATFORM_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"

    # Create fake config directory
    KLIPPER_HOME="$BATS_TEST_TMPDIR"
    mkdir -p "$KLIPPER_HOME/printer_data/config"
}

# Helper: create a printer.base.cfg with screws_tilt_adjust
_create_config() {
    local screw_thread="${1:-CW-M4}"
    cat > "$KLIPPER_HOME/printer_data/config/printer.base.cfg" << EOF
[printer]
kinematics: corexy

[screws_tilt_adjust]
screw1: -94, -94
screw1_name: Left Near
screw_thread: ${screw_thread}
EOF
}

# =========================================================
# Guard tests: must NOT modify non-AD5M configs
# =========================================================

@test "skips on non-ad5m platform" {
    _create_config "CW-M4"
    platform="pi"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    # Config unchanged
    grep -q 'screw_thread: CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "skips on ad5m forge_x firmware" {
    _create_config "CW-M4"
    platform="ad5m"
    AD5M_FIRMWARE="forge_x"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "skips on ad5m zmod firmware" {
    _create_config "CW-M4"
    platform="ad5m"
    AD5M_FIRMWARE="zmod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "skips when platform variable is unset" {
    _create_config "CW-M4"
    unset platform
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "skips when config dir does not exist" {
    rm -rf "$KLIPPER_HOME/printer_data/config"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
}

# =========================================================
# Fix application tests
# =========================================================

@test "fixes CW-M4 to CCW-M4 on ad5m klipper_mod" {
    _create_config "CW-M4"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CCW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
    ! grep -qw 'CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "creates backup before modifying" {
    _create_config "CW-M4"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    [ -f "$KLIPPER_HOME/printer_data/config/printer.base.cfg.pre-helix" ]
    grep -q 'screw_thread: CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg.pre-helix"
}

@test "does not overwrite existing backup" {
    _create_config "CW-M4"
    echo "original backup" > "$KLIPPER_HOME/printer_data/config/printer.base.cfg.pre-helix"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    # Backup should still be the original, not overwritten
    grep -q 'original backup' "$KLIPPER_HOME/printer_data/config/printer.base.cfg.pre-helix"
}

# =========================================================
# Safety: must NOT modify already-correct configs
# =========================================================

@test "skips config already set to CCW-M4" {
    _create_config "CCW-M4"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CCW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
    # No backup created since no change needed
    [ ! -f "$KLIPPER_HOME/printer_data/config/printer.base.cfg.pre-helix" ]
}

@test "does not modify CW-M3 configs (different thread size)" {
    _create_config "CW-M3"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CW-M3' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
    [ ! -f "$KLIPPER_HOME/printer_data/config/printer.base.cfg.pre-helix" ]
}

@test "does not modify CW-M5 configs (different thread size)" {
    _create_config "CW-M5"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CW-M5' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "does not modify config without screws_tilt_adjust section" {
    # Config with CW-M4 but NOT in a screws_tilt_adjust section
    cat > "$KLIPPER_HOME/printer_data/config/printer.base.cfg" << 'EOF'
[printer]
kinematics: corexy

[some_other_section]
screw_thread: CW-M4
EOF
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

# =========================================================
# Idempotency: running twice must be safe
# =========================================================

@test "idempotent — running twice does not corrupt config" {
    _create_config "CW-M4"
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    # Run again
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    # Should still be CCW-M4, not CCCW-M4 or mangled
    grep -q 'screw_thread: CCW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
    # Only one CCW-M4 line
    count=$(grep -c 'screw_thread' "$KLIPPER_HOME/printer_data/config/printer.base.cfg")
    [ "$count" -eq 1 ]
}

# =========================================================
# Edge cases
# =========================================================

@test "handles config in printer.cfg (not printer.base.cfg)" {
    # No printer.base.cfg, section is in printer.cfg
    cat > "$KLIPPER_HOME/printer_data/config/printer.cfg" << 'EOF'
[screws_tilt_adjust]
screw1: -94, -94
screw_thread: CW-M4
EOF
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CCW-M4' "$KLIPPER_HOME/printer_data/config/printer.cfg"
}

@test "prefers printer.base.cfg over printer.cfg" {
    # Both files exist, only printer.base.cfg has the wrong value
    _create_config "CW-M4"
    cat > "$KLIPPER_HOME/printer_data/config/printer.cfg" << 'EOF'
[include printer.base.cfg]
EOF
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'screw_thread: CCW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}

@test "handles no-space after colon" {
    cat > "$KLIPPER_HOME/printer_data/config/printer.base.cfg" << 'EOF'
[screws_tilt_adjust]
screw_thread:CW-M4
EOF
    platform="ad5m"
    AD5M_FIRMWARE="klipper_mod"
    run fix_ad5m_klipper_config
    [ "$status" -eq 0 ]
    grep -q 'CCW-M4' "$KLIPPER_HOME/printer_data/config/printer.base.cfg"
}
