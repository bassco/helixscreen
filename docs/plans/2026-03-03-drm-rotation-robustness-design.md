# DRM Rotation & Atomic Commit Robustness

**Date:** 2026-03-03
**Issue:** prestonbrown/helixscreen#288
**Status:** Design approved

## Problem

Users with physically inverted displays (e.g., BTT TFT43 mounted upside-down) experience screen flicker when using `"rotate": 180` in settings.json on DRM/KMS backends. Two root causes:

1. **Software rotation race:** When the DRM plane lacks hardware rotation (mask `0x0`), HelixScreen does CPU-based pixel reversal in `drm_flush()`. While the current shadow-buffer approach is correct (rotates the render buffer, not the displayed buffer), it adds CPU overhead and complexity to a hot path.

2. **Atomic commit failures:** Some Pi 4 configurations fail `drmModeAtomicCommit` with `EACCES` (Permission denied), causing flush failures regardless of rotation. No fallback path exists.

## Changes

### Change 1: Auto-fallback to fbdev for software rotation

**Files:** `display_manager.cpp`, `display_backend_drm.cpp`, `display_backend_drm.h`

Add `DisplayBackendDRM::supports_hardware_rotation(lv_display_rotation_t)` which queries the plane rotation mask via the already-created display. In `DisplayManager::init()`, after DRM display creation but before applying rotation: if rotation is requested and DRM can't do it in hardware, destroy the DRM display and trigger the existing fbdev fallback path (lines 165-186 of display_manager.cpp).

fbdev uses LVGL's native software rotation which is flicker-free — no DRM scanout timing concerns.

### Change 2a: Better error logging for atomic commit failures

**Files:** `lv_linux_drm.c` (update existing patch)

When `drmModeAtomicCommit` fails with `EACCES`/`EPERM`, log a clear actionable message suggesting `HELIX_DISPLAY_BACKEND=fbdev`. Track consecutive failure count to avoid log spam — log at ERROR once, then suppress.

### Change 2b: Legacy DRM fallback when atomic fails

**Files:** `lv_linux_drm.c` (update existing patch)

Add `drm_legacy_set_crtc()` using `drmModeSetCrtc()` as fallback when atomic commit fails. In `drm_dmabuf_set_plane()`, if atomic fails, try legacy. If legacy succeeds, set a flag to use legacy for all future frames. If both fail, log error suggesting fbdev.

## Testing

- Unit test: `supports_hardware_rotation()` with mock plane masks
- Manual: Pi 4 + BTT TFT43 + rotation=180 → verify fbdev auto-fallback triggers
- Manual: verify legacy DRM fallback works when atomic modesetting unavailable
