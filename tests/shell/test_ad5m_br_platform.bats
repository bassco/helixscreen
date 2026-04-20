#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later

# Verify PLATFORM_TARGET=ad5m-br is recognized by the Makefile and produces
# the expected CFLAGS/LDFLAGS (no -static, no strip, toolchain-neutral).

load helpers

@test "ad5m-br: make accepts PLATFORM_TARGET=ad5m-br without unknown-target error" {
    run make -n PLATFORM_TARGET=ad5m-br CROSS_COMPILE= CC=gcc CXX=g++ cross-info
    [ "$status" -eq 0 ] || {
        echo "stdout: $output"
        return 1
    }
}

@test "ad5m-br: does NOT set -static in LDFLAGS" {
    run make -n PLATFORM_TARGET=ad5m-br CROSS_COMPILE= CC=gcc CXX=g++ print-ldflags
    [ "$status" -eq 0 ]
    ! echo "$output" | grep -qE '(^|\s)-static(\s|$)' || {
        echo "Unexpected -static in ad5m-br LDFLAGS:"
        echo "$output"
        return 1
    }
}

@test "ad5m-br: STRIP_BINARY is not yes" {
    run make -n PLATFORM_TARGET=ad5m-br CROSS_COMPILE= CC=gcc CXX=g++ print-strip
    [ "$status" -eq 0 ]
    ! echo "$output" | grep -qE 'STRIP_BINARY=yes' || {
        echo "Unexpected STRIP_BINARY=yes in ad5m-br:"
        echo "$output"
        return 1
    }
}

@test "ad5m-br: inherits ad5m TARGET_CFLAGS cpu/fpu flags" {
    run make -n PLATFORM_TARGET=ad5m-br CROSS_COMPILE= CC=gcc CXX=g++ print-target-cflags
    [ "$status" -eq 0 ]
    echo "$output" | grep -qE 'mcpu=cortex-a7|mtune=cortex-a7' || {
        echo "Missing cortex-a7 tuning in ad5m-br TARGET_CFLAGS:"
        echo "$output"
        return 1
    }
    echo "$output" | grep -qE 'mfpu=neon-vfpv4' || {
        echo "Missing neon-vfpv4 in ad5m-br TARGET_CFLAGS:"
        echo "$output"
        return 1
    }
}
