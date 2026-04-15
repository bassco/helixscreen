#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for stop_cc1_competing_uis() in competing_uis.sh
# Specifically the grumpyscreen-wrapper substitution that works around
# OpenCentauri config-manager's hardcoded screen_ui allowlist
# (see OpenCentauri/cosmos#145).

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    log_info() { echo "INFO: $*"; }
    log_warn() { echo "WARN: $*"; }
    export -f log_info log_warn

    # Mock root mirroring CC1 layout. SUDO stays empty so file ops happen
    # directly under MOCK_ROOT instead of the real /etc.
    export MOCK_ROOT="$BATS_TEST_TMPDIR/cc1"
    mkdir -p "$MOCK_ROOT/etc/init.d" "$MOCK_ROOT/etc/klipper/config" "$MOCK_ROOT/usr/bin"

    # Stock COSMOS grumpyscreen init script (faked content + executable bit)
    cat > "$MOCK_ROOT/etc/init.d/grumpyscreen" <<'EOF'
#!/bin/sh
# Stock COSMOS grumpyscreen init (mocked)
echo "stock grumpyscreen $1"
EOF
    chmod +x "$MOCK_ROOT/etc/init.d/grumpyscreen"

    # HelixScreen init script (placeholder — installer normally drops this)
    cat > "$MOCK_ROOT/etc/init.d/helixscreen" <<'EOF'
#!/bin/sh
echo "helixscreen $1"
EOF
    chmod +x "$MOCK_ROOT/etc/init.d/helixscreen"

    # Minimal cosmos.conf
    cat > "$MOCK_ROOT/etc/klipper/config/cosmos.conf" <<'EOF'
[ui]
screen_ui = grumpyscreen
web_ui = mainsail
EOF

    # config-manager that mimics the upstream allowlist behavior
    cat > "$MOCK_ROOT/usr/bin/config-manager" <<'EOF'
#!/bin/sh
# Fake matching upstream behavior: exits 1 on the 3-arg "set" form
# (upstream only supports 2-arg get) and reports "grumpyscreen" on get,
# modeling the validator silently rejecting helixscreen.
[ $# -ne 2 ] && exit 1
echo grumpyscreen
EOF
    chmod +x "$MOCK_ROOT/usr/bin/config-manager"
    export PATH="$MOCK_ROOT/usr/bin:$PATH"

    # Read the production module body, substitute absolute COSMOS paths to
    # MOCK_ROOT, then source the patched copy. This keeps the test exercising
    # the real wrapper-install code rather than a re-implementation.
    local patched="$BATS_TEST_TMPDIR/competing_uis.sh"
    sed -e "s|/etc/init.d/grumpyscreen|$MOCK_ROOT/etc/init.d/grumpyscreen|g" \
        -e "s|/etc/init.d/helixscreen|$MOCK_ROOT/etc/init.d/helixscreen|g" \
        -e "s|/etc/klipper/config/cosmos.conf|$MOCK_ROOT/etc/klipper/config/cosmos.conf|g" \
        "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh" > "$patched"
    unset _HELIX_COMPETING_UIS_SOURCED
    # shellcheck disable=SC1090
    . "$patched"
}

@test "cc1: substitutes grumpyscreen with helixscreen wrapper" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    # Wrapper must contain the marker that exec's into helixscreen
    grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/grumpyscreen"
    grep -q "exec .*/etc/init.d/helixscreen" "$MOCK_ROOT/etc/init.d/grumpyscreen"
    [ -x "$MOCK_ROOT/etc/init.d/grumpyscreen" ]
}

@test "cc1: backs up original grumpyscreen to .helix-bak" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    [ -f "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak" ]
    grep -q "Stock COSMOS grumpyscreen" "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak"
}

@test "cc1: idempotent — does not re-wrap an already-wrapped grumpyscreen" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    # Capture wrapper content + backup mtime after first run
    cp -p "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak" "$MOCK_ROOT/bak1"
    sleep 1
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    # Backup must NOT have been overwritten with the wrapper itself
    diff "$MOCK_ROOT/bak1" "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak"
    # Backup still contains the original content, not the wrapper marker
    ! grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak"
}

@test "cc1: gui-switcher invoking grumpyscreen now reaches helixscreen" {
    found_any=false
    stop_cc1_competing_uis >/dev/null
    # Simulate gui-switcher's call path: exec /etc/init.d/<config-manager value> start
    run "$MOCK_ROOT/etc/init.d/grumpyscreen" start
    [ "$status" -eq 0 ]
    [[ "$output" == *"helixscreen start"* ]]
}

@test "cc1: cosmos.conf is updated to screen_ui = helixscreen" {
    found_any=false
    stop_cc1_competing_uis >/dev/null
    grep -q "^screen_ui = helixscreen" "$MOCK_ROOT/etc/klipper/config/cosmos.conf"
}

@test "cc1: missing grumpyscreen init script is a no-op (no wrapper, no error)" {
    rm -f "$MOCK_ROOT/etc/init.d/grumpyscreen"
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    [ ! -f "$MOCK_ROOT/etc/init.d/grumpyscreen" ]
    [ ! -f "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak" ]
}
