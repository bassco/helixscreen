#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# cross-compile-lock.sh — mutual exclusion for cross-compile docker runs
#
# Prevents two cross-compile builds from stomping on each other's in-tree
# artifacts (particularly lib/libhv/*.o and lib/libhv/hconfig.h, which are
# built in-place and would otherwise alternate between architectures).
#
# Usage:
#     scripts/cross-compile-lock.sh docker run --rm ...
#
# The lock is an atomic mkdir on /tmp/helixscreen-cross-compile.lock. If the
# directory already exists (another cross-compile is in progress), this script
# prints a helpful message listing running helixscreen toolchain containers
# and exits 1. A trap cleans up the lock directory on exit, Ctrl-C, or
# SIGTERM. Stale locks (from a crashed build) can be removed manually with
# `rmdir /tmp/helixscreen-cross-compile.lock`.
#
# See prestonbrown/helixscreen#766 for the debugging session that motivated
# this (concurrent pi-docker and ad5m-docker stomping on lib/libhv/hconfig.h).

set -e

LOCK="/tmp/helixscreen-cross-compile.lock"

# Color codes (empty if not a tty)
if [ -t 2 ]; then
    RED='\033[0;31m'
    YELLOW='\033[0;33m'
    RESET='\033[0m'
else
    RED=''
    YELLOW=''
    RESET=''
fi

if ! mkdir "$LOCK" 2>/dev/null; then
    printf '%bERROR: another cross-compile is already running%b\n' "$RED" "$RESET" >&2
    printf '%bLock held: %s%b\n' "$YELLOW" "$LOCK" "$RESET" >&2
    printf 'Reason: cross-compiles share lib/libhv/ build artifacts (in-tree compile)\n' >&2
    printf '        and would stomp on each other if run simultaneously.\n' >&2
    printf '\n' >&2
    printf 'Running helixscreen toolchain containers:\n' >&2
    docker ps --format '  {{.Names}} ({{.Image}})' 2>/dev/null \
        | grep 'helixscreen/toolchain' >&2 \
        || printf '  (none — lock may be stale)\n' >&2
    printf '\n' >&2
    printf 'Wait for the running build to finish, or if stale:\n' >&2
    printf '  rmdir %s\n' "$LOCK" >&2
    exit 1
fi

# Ensure we clean up the lock on exit, Ctrl-C, or kill.
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT HUP INT QUIT TERM

# Run the wrapped command. `exec` would prevent the trap from firing, so use
# a regular call and let $? propagate via set -e.
"$@"
