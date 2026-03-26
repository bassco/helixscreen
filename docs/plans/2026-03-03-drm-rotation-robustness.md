# DRM Rotation & Atomic Commit Robustness — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate DRM rotation flicker by auto-falling back to fbdev when hardware rotation is unavailable, and add legacy DRM fallback when atomic modesetting fails.

**Architecture:** Two independent changes: (1) DisplayManager checks DRM rotation capability after display creation, falls back to fbdev for software rotation cases. (2) LVGL DRM driver patch gains legacy `drmModeSetCrtc` fallback when `drmModeAtomicCommit` fails, plus actionable error logging.

**Tech Stack:** C++ (DisplayManager/backend), C (LVGL DRM driver patch), Catch2 (tests)

**Design:** `docs/plans/2026-03-03-drm-rotation-robustness-design.md`

---

### Task 1: Add `supports_hardware_rotation()` to DRM backend

**Files:**
- Modify: `include/display_backend_drm.h`
- Modify: `src/api/display_backend_drm.cpp`
- Modify: `include/display_backend.h` (add virtual base method)

**Step 1: Add virtual method to base class**

In `include/display_backend.h`, add after `set_display_rotation()` (line ~341):

```cpp
    /**
     * @brief Check if hardware can rotate without software fallback
     *
     * Returns true if the backend can handle the requested rotation
     * without CPU-based pixel manipulation. Used by DisplayManager
     * to decide whether to fall back to a different backend.
     *
     * @param rot Requested rotation
     * @return true if hardware rotation is supported, false if software needed
     */
    virtual bool supports_hardware_rotation(lv_display_rotation_t rot) const {
        (void)rot;
        return true; // Most backends handle rotation natively
    }
```

**Step 2: Implement in DRM backend header**

In `include/display_backend_drm.h`, add to public section (after line 66):

```cpp
    /// Check if DRM plane supports hardware rotation for the given angle
    bool supports_hardware_rotation(lv_display_rotation_t rot) const override;
```

**Step 3: Implement in DRM backend source**

In `src/api/display_backend_drm.cpp`, add after `set_display_rotation()` (after line 462):

```cpp
bool DisplayBackendDRM::supports_hardware_rotation(lv_display_rotation_t rot) const {
    if (rot == LV_DISPLAY_ROTATION_0) {
        return true; // No rotation = always supported
    }

    if (display_ == nullptr) {
        return false; // Can't check without display
    }

    // Map LVGL rotation to DRM constant
    uint64_t drm_rot = DRM_MODE_ROTATE_0;
    switch (rot) {
    case LV_DISPLAY_ROTATION_90:
        drm_rot = DRM_MODE_ROTATE_90;
        break;
    case LV_DISPLAY_ROTATION_180:
        drm_rot = DRM_MODE_ROTATE_180;
        break;
    case LV_DISPLAY_ROTATION_270:
        drm_rot = DRM_MODE_ROTATE_270;
        break;
    default:
        return true;
    }

#ifdef HELIX_ENABLE_OPENGLES
    // EGL path has no plane rotation API — always needs software
    return false;
#else
    uint64_t supported_mask = lv_linux_drm_get_plane_rotation_mask(display_);
    return choose_drm_rotation_strategy(drm_rot, supported_mask) == DrmRotationStrategy::HARDWARE;
#endif
}
```

**Step 4: Build to verify compilation**

Run: `make -j`
Expected: Clean build

**Step 5: Commit**

```
feat(display): add supports_hardware_rotation() to DRM backend
```

---

### Task 2: Auto-fallback to fbdev when software rotation needed

**Files:**
- Modify: `src/application/display_manager.cpp:226-269` (rotation section)

**Step 1: Write the failing test**

In `tests/unit/test_drm_rotation_fallback.cpp`, add:

```cpp
TEST_CASE("Software fallback signals no hardware support", "[display][drm][rotation]") {
    // When mask=0 (no rotation property), 180° returns SOFTWARE
    // which means supports_hardware_rotation() should return false
    // (tested via the strategy function since we can't mock DRM hardware)
    REQUIRE(choose_drm_rotation_strategy(ROT_180, MASK_NONE) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(ROT_180, MASK_0_ONLY) == DrmRotationStrategy::SOFTWARE);

    // When hardware supports it, returns HARDWARE
    REQUIRE(choose_drm_rotation_strategy(ROT_180, MASK_0_180) == DrmRotationStrategy::HARDWARE);
    REQUIRE(choose_drm_rotation_strategy(ROT_180, MASK_ALL) == DrmRotationStrategy::HARDWARE);
}
```

**Step 2: Run test to verify it passes (pure logic test)**

Run: `make test && ./build/bin/helix-tests "[rotation]" -v`
Expected: PASS (these are strategy logic tests that already work)

**Step 3: Add fbdev fallback to DisplayManager rotation section**

In `src/application/display_manager.cpp`, replace the rotation block (lines 242-267) with:

```cpp
        if (rotation_degrees != 0) {
#ifdef HELIX_DISPLAY_SDL
            // LVGL's SDL driver only supports software rotation in PARTIAL render mode,
            // but we use DIRECT mode for performance. Skip rotation on SDL — it's only
            // for desktop dev. On embedded (fbdev/DRM) rotation works correctly.
            spdlog::warn("[DisplayManager] Rotation {}° requested but SDL backend does not "
                         "support software rotation (DIRECT render mode). Ignoring on desktop.",
                         rotation_degrees);
#else
            // Capture physical dimensions before rotation changes them
            int phys_w = m_width;
            int phys_h = m_height;

            lv_display_rotation_t lv_rot = degrees_to_lv_rotation(rotation_degrees);

            // If DRM backend can't do hardware rotation, fall back to fbdev
            // which handles software rotation flicker-free via LVGL's native path.
            if (m_backend->type() == DisplayBackendType::DRM &&
                !m_backend->supports_hardware_rotation(lv_rot)) {
                spdlog::warn("[DisplayManager] DRM lacks hardware rotation for {}°, "
                             "falling back to fbdev (flicker-free software rotation)",
                             rotation_degrees);
                lv_display_delete(m_display);
                m_display = nullptr;
                m_backend.reset();
                m_backend = DisplayBackend::create(DisplayBackendType::FBDEV);
                if (m_backend && m_backend->is_available()) {
                    if (config.splash_active) {
                        m_backend->set_splash_active(true);
                    }
                    m_display = m_backend->create_display(m_width, m_height);
                }
                if (!m_display) {
                    spdlog::error("[DisplayManager] Fbdev fallback for rotation also failed");
                    m_backend.reset();
                    lv_xml_deinit();
                    lv_deinit();
                    return false;
                }
                spdlog::info("[DisplayManager] Fbdev fallback succeeded at {}x{}", m_width,
                             m_height);
            }

            lv_display_set_rotation(m_display, lv_rot);

            // Update tracked dimensions to match rotated resolution
            m_width = lv_display_get_horizontal_resolution(m_display);
            m_height = lv_display_get_vertical_resolution(m_display);

            // Auto-rotate touch coordinates to match display rotation
            m_backend->set_display_rotation(lv_rot, phys_w, phys_h);

            spdlog::info("[DisplayManager] Display rotated {}° — effective resolution: {}x{}",
                         rotation_degrees, m_width, m_height);
#endif
        }
```

**Step 4: Build to verify**

Run: `make -j`
Expected: Clean build

**Step 5: Commit**

```
feat(display): auto-fallback to fbdev when DRM needs software rotation (prestonbrown/helixscreen#288)
```

---

### Task 3: Legacy DRM fallback for atomic commit failures

**Files:**
- Modify: `patches/lvgl-drm-flush-rotation.patch` (update the patch applied to `lib/lvgl/src/drivers/display/drm/lv_linux_drm.c`)

**Step 1: Add legacy page flip function and atomic failure tracking**

In `lv_linux_drm.c`, add a new static function before `drm_dmabuf_set_plane()` and modify the existing function. The changes go into the patch file.

Add these statics near the top static variables section:

```c
static int use_legacy_flip = 0;        /* 1 = atomic failed, use drmModeSetCrtc */
static int atomic_fail_count = 0;      /* consecutive atomic failures */
```

Add a legacy flip function before `drm_dmabuf_set_plane()`:

```c
/**
 * Legacy page flip via drmModeSetCrtc — fallback when atomic modesetting
 * fails (e.g., EACCES on some Pi 4 configurations).
 */
static int drm_legacy_set_crtc(drm_dev_t * drm_dev, drm_buffer_t * buf)
{
    int ret = drmModeSetCrtc(drm_dev->fd, drm_dev->crtc_id, buf->fb_handle,
                              0, 0, &drm_dev->conn_id, 1, &drm_dev->mode);
    if(ret) {
        LV_LOG_ERROR("drmModeSetCrtc fallback failed: %s (%d)", strerror(errno), errno);
    }
    return ret;
}
```

**Step 2: Modify `drm_dmabuf_set_plane()` to try legacy on atomic failure**

Replace the atomic commit + error handling section in `drm_dmabuf_set_plane()`:

```c
    /* If atomic modesetting previously failed, skip to legacy path */
    if(use_legacy_flip) {
        if(drm_dev->req) {
            drmModeAtomicFree(drm_dev->req);
            drm_dev->req = NULL;
        }
        return drm_legacy_set_crtc(drm_dev, buf);
    }

    ret = drmModeAtomicCommit(drm_dev->fd, drm_dev->req, flags, drm_dev);
    if(ret) {
        int err = errno;
        atomic_fail_count++;

        if(err == EACCES || err == EPERM) {
            if(atomic_fail_count == 1) {
                LV_LOG_ERROR("drmModeAtomicCommit failed: Permission denied. "
                             "Trying legacy drmModeSetCrtc fallback. "
                             "If issues persist, try HELIX_DISPLAY_BACKEND=fbdev");
            }
        }
        else {
            LV_LOG_ERROR("drmModeAtomicCommit failed: %s (%d)", strerror(err), err);
        }

        /* Try legacy fallback */
        if(drm_dev->req) {
            drmModeAtomicFree(drm_dev->req);
            drm_dev->req = NULL;
        }
        ret = drm_legacy_set_crtc(drm_dev, buf);
        if(ret == 0) {
            LV_LOG_INFO("Legacy drmModeSetCrtc succeeded — using legacy path for future frames");
            use_legacy_flip = 1;
        }
        return ret;
    }
```

**Step 3: Regenerate the patch**

After editing `lib/lvgl/src/drivers/display/drm/lv_linux_drm.c` directly, regenerate the patch:

```bash
cd lib/lvgl && git diff src/drivers/display/drm/lv_linux_drm.c src/drivers/display/drm/lv_linux_drm.h > ../../patches/lvgl-drm-flush-rotation.patch
```

**Step 4: Build and verify**

Run: `make -j`
Expected: Clean build

**Step 5: Commit**

```
fix(drm): add legacy drmModeSetCrtc fallback when atomic commit fails (prestonbrown/helixscreen#288)
```

---

### Task 4: Manual verification on Pi

**Step 1: Deploy to Pi with rotation config**

```bash
PI_HOST=192.168.1.113 make pi-test
```

On the Pi, ensure `settings.json` has `"display": { "rotate": 180 }`.

**Step 2: Verify fbdev auto-fallback**

Check logs for: `"DRM lacks hardware rotation for 180°, falling back to fbdev"`
Verify: no flicker, display renders correctly upside-down.

**Step 3: Test atomic commit failure path**

This requires a system where atomic modesetting fails — may need to simulate by temporarily revoking DRM master. If not testable on current hardware, verify via code review that the legacy path is correct.

**Step 4: Commit test results / any fixes**

```
test(display): verify DRM rotation fbdev fallback on Pi 4
```
