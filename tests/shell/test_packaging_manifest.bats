#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regression tests for scripts/gen-packaging-manifest.sh — the single source
# of truth for which source-tree directories ship in HelixScreen packages.
#
# The Android extractor, cross.mk RELEASE_ASSETS, and (eventually) package.sh
# all consume this manifest. Prior incidents (e0840a4b6, 87452586f, the
# Android extractor drift that motivated this file) all had the same shape:
# a hand-maintained list of directories falls out of sync with the source
# tree and a release silently ships without its RO seed bundle.
#
# These tests catch that class of bug at CI time.

SCRIPT="scripts/gen-packaging-manifest.sh"

@test "manifest script is executable" {
    [ -x "$SCRIPT" ]
}

@test "manifest runs cleanly and produces non-empty output" {
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    [ -n "$output" ]
}

@test "manifest output is sorted with no duplicates" {
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    # Compare to itself sorted+deduped — any diff means drift.
    local sorted
    sorted=$(printf '%s\n' "$output" | LC_ALL=C sort -u)
    [ "$output" = "$sorted" ]
}

@test "manifest includes all source-tree dirs under ui_xml/, assets/, config/" {
    local manifest
    manifest=$("$SCRIPT")

    # Walk the source tree the same way the script claims to — skipping hidden.
    local expected
    expected=$(find ui_xml assets config -type d \( -name '.*' -prune \) -o -type d -print 2>/dev/null | LC_ALL=C sort)

    if ! diff <(printf '%s\n' "$manifest") <(printf '%s\n' "$expected") >/dev/null; then
        echo "Manifest drift detected:"
        diff <(printf '%s\n' "$manifest") <(printf '%s\n' "$expected") || true
        return 1
    fi
}

@test "manifest includes critical seed dirs introduced by bfeba7c26" {
    # If any of these drop out of the manifest, the Android APK and/or release
    # tarballs will ship without printer detection, themes, presets, etc.
    local required=(
        assets/config
        assets/config/presets
        assets/config/print_start_profiles
        assets/config/themes/defaults
        assets/config/platform
        assets/config/panel_widgets
        assets/fonts
        assets/images
        assets/sounds
        ui_xml
        ui_xml/components
        ui_xml/translations
    )
    local manifest
    manifest=$("$SCRIPT")
    for dir in "${required[@]}"; do
        if ! grep -qxF "$dir" <<<"$manifest"; then
            echo "Manifest missing required directory: $dir"
            return 1
        fi
    done
}

@test "manifest excludes hidden directories (.git, .claude-recall, ...)" {
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    # No line should contain a path component starting with '.'
    ! grep -qE '(^|/)\.[^/]' <<<"$output"
}

@test "manifest accepts explicit project root argument" {
    run "$SCRIPT" "$PWD"
    [ "$status" -eq 0 ]
    [ -n "$output" ]
}

@test "manifest rejects non-project-root argument" {
    run "$SCRIPT" /tmp
    [ "$status" -ne 0 ]
}
