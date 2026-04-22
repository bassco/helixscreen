#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Guardrail for the in-app self-update platform picker.
#
# Silent brick history:
#   v0.99.41: HELIX_PLATFORM_SNAPMAKER_U1 missing from get_platform_key() →
#             U1 devices downloaded helixscreen-pi-*.tar.gz on self-update,
#             ended up with a binary that NEEDED libsystemd/libinput/libEGL/
#             libGLESv2/libgbm — none present on U1 → exec failure.
#   Earlier:  HELIX_PLATFORM_X86 missing from the same function (fixed by
#             640b2a6ab).
#
# These tests enforce that:
#   1. Every -DHELIX_PLATFORM_* define in mk/cross.mk has a matching branch
#      in src/system/update_checker.cpp::get_platform_key().
#   2. Every PLATFORM_TARGET in the release.yml matrix has a corresponding
#      entry in the known_platforms allowlist in test_update_checker.cpp
#      (so a missing branch fails unit tests instead of silently shipping).
#   3. Every platform key returned by get_platform_key() is also in the
#      known_platforms allowlist (catches typos in the return string).

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
CROSS_MK="$WORKTREE_ROOT/mk/cross.mk"
UPDATE_CHECKER_CPP="$WORKTREE_ROOT/src/system/update_checker.cpp"
TEST_UPDATE_CHECKER_CPP="$WORKTREE_ROOT/tests/unit/test_update_checker.cpp"
RELEASE_YML="$WORKTREE_ROOT/.github/workflows/release.yml"

@test "every HELIX_PLATFORM_* in cross.mk has a branch in get_platform_key()" {
    # Extract the -DHELIX_PLATFORM_X tokens from mk/cross.mk. The set is
    # authoritative: every platform we build for is tagged with one of these.
    local platforms
    platforms=$(grep -oE -- '-DHELIX_PLATFORM_[A-Z0-9_]+' "$CROSS_MK" \
                | sed 's/^-D//' | sort -u)

    [ -n "$platforms" ]  # sanity: we expect at least one

    local missing=""
    for p in $platforms; do
        # Accept either `#ifdef HELIX_PLATFORM_X` or `defined(HELIX_PLATFORM_X)`.
        if ! grep -qE "(ifdef $p\b|defined\($p\))" "$UPDATE_CHECKER_CPP"; then
            missing="$missing $p"
        fi
    done

    if [ -n "$missing" ]; then
        echo "The following HELIX_PLATFORM_* macros are built by mk/cross.mk"
        echo "but have no branch in UpdateChecker::get_platform_key():"
        echo "   $missing"
        echo ""
        echo "Add matching '#elif defined(...)' branches returning the correct"
        echo "asset key, OR the device will silently fall through to 'pi' on"
        echo "self-update and download the wrong release tarball."
        false
    fi
}

@test "every release.yml platform has a known_platforms entry in the unit test" {
    # Pull `platform: [...]` list from the release matrix.
    local matrix_line
    matrix_line=$(grep -E 'platform: *\[' "$RELEASE_YML" | head -1)
    [ -n "$matrix_line" ]

    # Strip everything outside the brackets, split on commas.
    local platforms
    platforms=$(echo "$matrix_line" | sed -E 's/.*\[([^]]+)\].*/\1/' \
                | tr ',' '\n' | tr -d ' ')

    [ -n "$platforms" ]

    # Pull known_platforms vector from test_update_checker.cpp.
    # The literal is split across lines; slurp a window and keep only quoted
    # tokens.
    local known
    known=$(awk '/std::vector<std::string> known_platforms = \{/,/\};/' \
            "$TEST_UPDATE_CHECKER_CPP" | grep -oE '"[a-z0-9_-]+"' | tr -d '"')

    [ -n "$known" ]

    local missing=""
    for p in $platforms; do
        # The k1 build target is "mips" → platform key "k1"; normalize.
        local key="$p"
        if [ "$key" = "mips" ]; then key="k1"; fi
        if ! echo "$known" | grep -qx "$key"; then
            missing="$missing $key"
        fi
    done

    if [ -n "$missing" ]; then
        echo "Release matrix ships these platforms but known_platforms in"
        echo "tests/unit/test_update_checker.cpp is missing:"
        echo "   $missing"
        echo ""
        echo "Add them to known_platforms so get_platform_key() tests catch"
        echo "the missing #elif branch that would ship the wrong tarball."
        false
    fi
}

@test "every string returned by get_platform_key() is in known_platforms" {
    # Extract the quoted return values from get_platform_key()'s body.
    local returns
    returns=$(awk '/std::string UpdateChecker::get_platform_key\(\)/,/^}/' \
              "$UPDATE_CHECKER_CPP" | grep -oE 'return "[a-z0-9_-]+"' \
              | grep -oE '"[a-z0-9_-]+"' | tr -d '"' | sort -u)

    [ -n "$returns" ]

    local known
    known=$(awk '/std::vector<std::string> known_platforms = \{/,/\};/' \
            "$TEST_UPDATE_CHECKER_CPP" | grep -oE '"[a-z0-9_-]+"' | tr -d '"')

    [ -n "$known" ]

    local bad=""
    for r in $returns; do
        if ! echo "$known" | grep -qx "$r"; then
            bad="$bad $r"
        fi
    done

    if [ -n "$bad" ]; then
        echo "get_platform_key() returns strings that are not in the test's"
        echo "known_platforms allowlist:"
        echo "   $bad"
        echo ""
        echo "Either fix the typo in the return string or add the key to"
        echo "known_platforms in tests/unit/test_update_checker.cpp."
        false
    fi
}
