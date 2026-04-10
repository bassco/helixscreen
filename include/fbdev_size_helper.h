// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file fbdev_size_helper.h
 * @brief Pure helper that extracts physical framebuffer dimensions from a
 *        Linux fb_var_screeninfo struct.
 *
 * Used by the fbdev backend to compare the user's requested resolution
 * against what the kernel fb driver is actually serving. Split out for
 * unit-testability without needing a real /dev/fb device.
 */

#ifdef __linux__

#include <cstdint>

struct fb_var_screeninfo; // forward-declared; full def via <linux/fb.h>

namespace helix {

struct FbSize {
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

/**
 * @brief Return the physical (displayed) size of the framebuffer.
 *
 * Uses xres/yres, not xres_virtual/yres_virtual — the virtual fields can be
 * larger when the driver supports double-buffering, and we care about what
 * the user actually sees.
 *
 * @param vinfo A populated fb_var_screeninfo from FBIOGET_VSCREENINFO.
 * @return FbSize with valid=true if both dimensions are non-zero.
 */
FbSize fb_size_from_var_screeninfo(const struct fb_var_screeninfo& vinfo);

} // namespace helix

#endif // __linux__
