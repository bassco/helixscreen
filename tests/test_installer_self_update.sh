#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for self-update init script protection (issue #314)
#
# Verifies that self-update mode (HELIX_SELF_UPDATE=1) skips init script
# modifications that would corrupt platform-customized scripts.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[31m'
GREEN='\033[32m'
CYAN='\033[36m'
BOLD='\033[1m'
RESET='\033[0m'

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
declare -a FAILED_TESTS

print_test() {
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -e "\n${CYAN}[TEST $TESTS_RUN]${RESET} $1"
}

assert() {
    local condition="$1"
    local message="$2"
    if eval "$condition"; then
        echo -e "${GREEN}  ✓${RESET} $message"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}  ✗${RESET} $message"
        FAILED_TESTS+=("TEST $TESTS_RUN: $message")
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Create a mock environment for testing
setup_mock_env() {
    local tmpdir
    tmpdir=$(mktemp -d)
    mkdir -p "$tmpdir/etc/init.d"
    mkdir -p "$tmpdir/install/config"

    # Create mock init scripts (competing UIs)
    echo '#!/bin/sh' > "$tmpdir/etc/init.d/S80guppyscreen"
    chmod +x "$tmpdir/etc/init.d/S80guppyscreen"

    echo '#!/bin/sh' > "$tmpdir/etc/init.d/S80GuppyScreen"
    chmod +x "$tmpdir/etc/init.d/S80GuppyScreen"

    # Create mock helixscreen init script (as if platform customized it)
    echo '#!/bin/sh
# ZMOD-customized init script
DAEMON_DIR="/srv/helixscreen"
start() { echo "starting"; }
stop() { echo "stopping"; }' > "$tmpdir/etc/init.d/S80helixscreen"
    chmod +x "$tmpdir/etc/init.d/S80helixscreen"

    # Create mock init script template (what the installer would copy)
    echo '#!/bin/sh
# Default template
DAEMON_DIR="/opt/helixscreen"
start() { echo "starting"; }
stop() { echo "stopping"; }' > "$tmpdir/install/config/helixscreen.init"

    echo "$tmpdir"
}

cleanup_mock() {
    rm -rf "$1"
}

# Create a test harness that sources the real modules with stubs
create_competing_uis_harness() {
    local tmpdir="$1"
    local harness="$tmpdir/_competing_uis_harness.sh"

    cat > "$harness" << HEOF
#!/bin/sh
# Stub logging functions
log_success() { echo "[SUCCESS] \$*"; }
log_warn() { echo "[WARN] \$*"; }
log_error() { echo "[ERROR] \$*"; }
log_info() { echo "[INFO] \$*"; }

# Stub functions used by competing_uis.sh
kill_process_by_name() { return 1; }
file_sudo() { echo ""; }
record_disabled_service() { echo "[RECORDED] \$1 \$2"; }

SUDO=""
INSTALL_DIR="$tmpdir/install"
INIT_SYSTEM="sysv"
PREVIOUS_UI_SCRIPT=""
AD5M_FIRMWARE="\${1:-}"
COMPETING_UIS="guppyscreen GuppyScreen"

# Source _is_self_update from common.sh
_HELIX_COMMON_SOURCED=""
HEOF

    # Extract _is_self_update from common.sh
    sed -n '/_is_self_update()/,/^}/p' "$PROJECT_ROOT/scripts/lib/installer/common.sh" >> "$harness"

    # Source the real competing_uis.sh
    echo '_HELIX_COMPETING_UIS_SOURCED=""' >> "$harness"
    cat "$PROJECT_ROOT/scripts/lib/installer/competing_uis.sh" >> "$harness"

    # Override the init script glob paths to use our tmpdir
    # The real code globs /etc/init.d/ but we need to test with our mock dir
    cat >> "$harness" << HEOF2

# Override stop_competing_uis to use mock paths
_original_stop_competing_uis() { stop_competing_uis; }

stop_competing_uis_mock() {
    # Override record_disabled_service after competing_uis.sh redefined it
    record_disabled_service() { echo "[RECORDED] \$1 \$2"; }

    # Re-implement the function with mock paths for testing
    if _is_self_update; then
        log_info "Skipping competing UI check (self-update; already handled at install)"
        return 0
    fi

    log_info "Checking for competing screen UIs..."
    found_any=false

    case "\$AD5M_FIRMWARE" in
        forge_x)    stop_forgex_competing_uis ;;
        klipper_mod) stop_kmod_competing_uis ;;
        zmod)
            log_info "ZMOD platform: skipping generic UI disabling (ZMOD-managed)"
            return 0
            ;;
    esac

    for ui in \$COMPETING_UIS; do
        for initscript in $tmpdir/etc/init.d/S*\${ui}* $tmpdir/etc/init.d/\${ui}*; do
            [ -e "\$initscript" ] || continue
            if [ "\$initscript" = "\$PREVIOUS_UI_SCRIPT" ]; then
                continue
            fi
            if [ -x "\$initscript" ]; then
                log_info "Stopping \$ui (\$initscript)..."
                chmod -x "\$initscript" 2>/dev/null || true
                record_disabled_service "sysv-chmod" "\$initscript"
                found_any=true
            fi
        done
    done

    if [ "\$found_any" = true ]; then
        log_info "Waiting for competing UIs to stop..."
    else
        log_info "No competing UIs found"
    fi
}

stop_competing_uis_mock
HEOF2

    chmod +x "$harness"
    echo "$harness"
}

create_service_harness() {
    local tmpdir="$1"
    local harness="$tmpdir/_service_harness.sh"

    cat > "$harness" << HEOF
#!/bin/sh
# Stub logging functions
log_success() { echo "[SUCCESS] \$*"; }
log_warn() { echo "[WARN] \$*"; }
log_error() { echo "[ERROR] \$*"; }
log_info() { echo "[INFO] \$*"; }

SUDO=""
INSTALL_DIR="$tmpdir/install"
INIT_SCRIPT_DEST="$tmpdir/etc/init.d/S80helixscreen"
SERVICE_NAME="helixscreen"
CLEANUP_SERVICE=false

# Source _is_self_update from common.sh
HEOF

    sed -n '/_is_self_update()/,/^}/p' "$PROJECT_ROOT/scripts/lib/installer/common.sh" >> "$harness"

    # Extract install_service_sysv from service.sh
    echo '_HELIX_SERVICE_SOURCED=""' >> "$harness"

    # Also need _has_no_new_privs stub
    cat >> "$harness" << 'HEOF2'
_has_no_new_privs() { return 1; }
HEOF2

    sed -n '/^install_service_sysv()/,/^}/p' "$PROJECT_ROOT/scripts/lib/installer/service.sh" >> "$harness"

    echo 'install_service_sysv' >> "$harness"

    chmod +x "$harness"
    echo "$harness"
}

# ============================================================
# Test: Self-update skips chmod -x on competing UI init scripts
# ============================================================
test_self_update_skips_competing_ui_disable() {
    print_test "Self-update skips chmod -x on competing UI init scripts"
    local tmpdir
    tmpdir=$(setup_mock_env)

    local harness
    harness=$(create_competing_uis_harness "$tmpdir")

    # Run with HELIX_SELF_UPDATE=1
    local output
    output=$(HELIX_SELF_UPDATE=1 sh "$harness" "" 2>&1)

    # Init scripts should still be executable (not chmod -x'd)
    assert "[ -x '$tmpdir/etc/init.d/S80guppyscreen' ]" \
        "S80guppyscreen still executable after self-update"
    assert "[ -x '$tmpdir/etc/init.d/S80GuppyScreen' ]" \
        "S80GuppyScreen still executable after self-update"
    assert "[[ '$output' == *'self-update'* ]]" \
        "Output mentions self-update skip"
    assert "[[ '$output' != *'RECORDED'* ]]" \
        "No services were recorded as disabled"

    cleanup_mock "$tmpdir"
}

# ============================================================
# Test: Fresh install DOES disable competing UI init scripts
# ============================================================
test_fresh_install_disables_competing_uis() {
    print_test "Fresh install disables competing UI init scripts"
    local tmpdir
    tmpdir=$(setup_mock_env)

    local harness
    harness=$(create_competing_uis_harness "$tmpdir")

    # Run WITHOUT HELIX_SELF_UPDATE (fresh install)
    local output
    output=$(sh "$harness" "" 2>&1)

    # Init scripts should have been chmod -x'd
    assert "[ ! -x '$tmpdir/etc/init.d/S80guppyscreen' ]" \
        "S80guppyscreen disabled (not executable) after fresh install"
    assert "[[ '$output' == *'RECORDED'* ]]" \
        "Disabled services were recorded"

    cleanup_mock "$tmpdir"
}

# ============================================================
# Test: ZMOD fresh install skips generic competing UI loop
# ============================================================
test_zmod_fresh_install_skips_generic_loop() {
    print_test "ZMOD fresh install skips generic competing UI loop (defense-in-depth)"
    local tmpdir
    tmpdir=$(setup_mock_env)

    local harness
    harness=$(create_competing_uis_harness "$tmpdir")

    # Run as ZMOD platform, fresh install (no HELIX_SELF_UPDATE)
    local output
    output=$(sh "$harness" "zmod" 2>&1)

    # S80guppyscreen should still be executable (ZMOD manages it)
    assert "[ -x '$tmpdir/etc/init.d/S80guppyscreen' ]" \
        "S80guppyscreen still executable on ZMOD fresh install"
    assert "[[ '$output' == *'ZMOD'* ]]" \
        "Output mentions ZMOD-managed skip"
    assert "[[ '$output' != *'RECORDED'* ]]" \
        "No services were recorded as disabled"

    cleanup_mock "$tmpdir"
}

# ============================================================
# Test: Self-update skips SysV init script overwrite
# ============================================================
test_self_update_skips_init_script_overwrite() {
    print_test "Self-update skips SysV init script overwrite"
    local tmpdir
    tmpdir=$(setup_mock_env)

    # Save original content
    local original_content
    original_content=$(cat "$tmpdir/etc/init.d/S80helixscreen")

    local harness
    harness=$(create_service_harness "$tmpdir")

    # Run with HELIX_SELF_UPDATE=1
    local output
    output=$(HELIX_SELF_UPDATE=1 sh "$harness" 2>&1)

    # Init script should NOT have been overwritten
    local current_content
    current_content=$(cat "$tmpdir/etc/init.d/S80helixscreen")
    assert "[ '$original_content' = '$current_content' ]" \
        "Init script content unchanged after self-update"
    assert "[[ '$output' == *'self-update'* ]]" \
        "Output mentions self-update skip"

    cleanup_mock "$tmpdir"
}

# ============================================================
# Test: Fresh install DOES write SysV init script
# ============================================================
test_fresh_install_writes_init_script() {
    print_test "Fresh install writes SysV init script"
    local tmpdir
    tmpdir=$(setup_mock_env)

    # Save original content
    local original_content
    original_content=$(cat "$tmpdir/etc/init.d/S80helixscreen")

    local harness
    harness=$(create_service_harness "$tmpdir")

    # Run WITHOUT HELIX_SELF_UPDATE (fresh install)
    local output
    output=$(sh "$harness" 2>&1)

    # Init script should have been overwritten with the template
    local current_content
    current_content=$(cat "$tmpdir/etc/init.d/S80helixscreen")
    assert "[ '$original_content' != '$current_content' ]" \
        "Init script was updated during fresh install"
    assert "[[ '$output' == *'Installed SysV init script'* ]]" \
        "Output confirms init script installation"

    cleanup_mock "$tmpdir"
}

# ============================================================
# Main
# ============================================================
main() {
    echo -e "${BOLD}${CYAN}Self-Update Init Script Protection Tests (issue #314)${RESET}"
    echo -e "${CYAN}Project:${RESET} $PROJECT_ROOT"
    echo ""

    test_self_update_skips_competing_ui_disable
    test_fresh_install_disables_competing_uis
    test_zmod_fresh_install_skips_generic_loop
    test_self_update_skips_init_script_overwrite
    test_fresh_install_writes_init_script

    echo ""
    echo -e "${BOLD}${CYAN}Test Summary${RESET}"
    echo -e "${CYAN}────────────────────────────────────────${RESET}"
    echo -e "Total tests: $TESTS_RUN"
    echo -e "${GREEN}Passed: $TESTS_PASSED${RESET}"

    if [ $TESTS_FAILED -gt 0 ]; then
        echo -e "${RED}Failed: $TESTS_FAILED${RESET}"
        echo ""
        echo -e "${RED}${BOLD}Failed tests:${RESET}"
        for test in "${FAILED_TESTS[@]}"; do
            echo -e "  ${RED}✗${RESET} $test"
        done
        exit 1
    else
        echo -e "${GREEN}${BOLD}✓ All tests passed!${RESET}"
        exit 0
    fi
}

main
