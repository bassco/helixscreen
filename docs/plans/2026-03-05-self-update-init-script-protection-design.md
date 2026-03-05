# Self-Update Init Script Protection

**Issue:** #314 — Built-in update breaks init scripts on Flashforge/Z-Mod
**Date:** 2026-03-05

## Problem

The in-app OTA self-update (HELIX_SELF_UPDATE=1) unconditionally modifies init scripts:

1. `stop_competing_uis()` runs `chmod -x` on competing UI init scripts (e.g., S80guppyscreen) even though they were already disabled during initial install
2. `install_service_sysv()` overwrites the platform's init script with a fresh template, destroying platform-specific customizations (ZMOD, Klipper Mod)

On ZMOD specifically, S80guppyscreen is managed by ZMOD and should never be touched. The generic glob loop in competing_uis.sh bypassed the ZMOD no-op case.

## Root Cause

- `competing_uis.sh`: ZMOD case was `;;` (no-op), but the generic glob loop after the case statement still ran and matched S80guppyscreen
- `service.sh`: No self-update guard on `install_service_sysv()` or `install_service_systemd()` — only `start_service` and `stop_service` had guards

## Solution

### 1. Self-update guards (all platforms)

Both `install_service_sysv()` and `install_service_systemd()` now early-return when `_is_self_update()` is true. The init script/service file was already installed during initial setup and doesn't need replacing when only binaries change.

`stop_competing_uis()` now early-returns when `_is_self_update()`. Competing UIs were already disabled during initial install.

### 2. ZMOD defense-in-depth

The ZMOD case in `stop_competing_uis()` now does `return 0` instead of `;;`, preventing the generic glob loop from ever running on ZMOD — even on fresh install.

### 3. _is_self_update() moved to common.sh

Moved from service.sh to common.sh since it's now used by both service.sh and competing_uis.sh (which is sourced before service.sh).

### Future: Init script migrations

When init script template changes are needed in the future, add version-gated migration functions (sed patches) rather than full overwrites. This preserves platform customizations while still propagating necessary changes.

## Files Changed

- `scripts/lib/installer/common.sh` — Added `_is_self_update()`
- `scripts/lib/installer/competing_uis.sh` — Self-update guard + ZMOD return 0
- `scripts/lib/installer/service.sh` — Self-update guards on install_service_sysv/systemd
- `scripts/install.sh` — Re-bundled from modules
- `tests/test_installer_self_update.sh` — 5 regression tests (13 assertions)

## Tests

| Test | Verifies |
|------|----------|
| Self-update skips competing UI disable | chmod -x NOT applied during self-update |
| Fresh install disables competing UIs | chmod -x IS applied during fresh install |
| ZMOD fresh install skips generic loop | S80guppyscreen untouched on ZMOD |
| Self-update skips init script overwrite | Init script content preserved during self-update |
| Fresh install writes init script | Init script IS written during fresh install |
