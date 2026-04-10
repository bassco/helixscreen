// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/drm_mode_matching.h"

#include <cstddef>

namespace helix {

int find_matching_mode(const std::vector<DrmModeInfo>& modes, uint32_t requested_w,
                       uint32_t requested_h) {
    for (size_t i = 0; i < modes.size(); i++) {
        if (modes[i].hdisplay == requested_w && modes[i].vdisplay == requested_h) {
            return static_cast<int>(i);
        }
    }
    return DrmModeMatch::kNoMatch;
}

int find_preferred_mode_index(const std::vector<DrmModeInfo>& modes) {
    if (modes.empty()) {
        return DrmModeMatch::kNoMatch;
    }
    for (size_t i = 0; i < modes.size(); i++) {
        if (modes[i].is_preferred) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

} // namespace helix
