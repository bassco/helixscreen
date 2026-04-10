// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_drm_mode_matching.cpp
 * @brief Unit tests for DRM mode matching helper used by the DRM backend
 *        to honor -s (requested resolution) against the connector's modelist.
 */

#include "../../include/drm_mode_matching.h"

#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("find_matching_mode: empty list returns kNoMatch", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes;
    REQUIRE(find_matching_mode(modes, 1024, 600) == DrmModeMatch::kNoMatch);
}

TEST_CASE("find_matching_mode: exact match at index 0", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1024, 600, 60, false},
        {800, 480, 60, true},
    };
    REQUIRE(find_matching_mode(modes, 1024, 600) == 0);
}

TEST_CASE("find_matching_mode: exact match mid-list", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1920, 1080, 60, false},
        {1280, 720, 60, false},
        {1024, 600, 60, false},
        {800, 480, 60, true},
    };
    REQUIRE(find_matching_mode(modes, 1024, 600) == 2);
}

TEST_CASE("find_matching_mode: exact match at end", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1920, 1080, 60, true},
        {800, 480, 60, false},
    };
    REQUIRE(find_matching_mode(modes, 800, 480) == 1);
}

TEST_CASE("find_matching_mode: no match returns kNoMatch", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1920, 1080, 60, true},
        {800, 480, 60, false},
    };
    REQUIRE(find_matching_mode(modes, 1234, 567) == DrmModeMatch::kNoMatch);
}

TEST_CASE("find_matching_mode: refresh rate is ignored when matching", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1024, 600, 30, false}, // different refresh, same pixels
    };
    REQUIRE(find_matching_mode(modes, 1024, 600) == 0);
}

TEST_CASE("find_preferred_mode_index: returns index of first preferred", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1920, 1080, 60, false},
        {1280, 720, 60, true},
        {800, 480, 60, true},
    };
    REQUIRE(find_preferred_mode_index(modes) == 1);
}

TEST_CASE("find_preferred_mode_index: no preferred falls back to 0", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes = {
        {1920, 1080, 60, false},
        {800, 480, 60, false},
    };
    REQUIRE(find_preferred_mode_index(modes) == 0);
}

TEST_CASE("find_preferred_mode_index: empty list returns kNoMatch", "[drm_mode_matching]") {
    std::vector<DrmModeInfo> modes;
    REQUIRE(find_preferred_mode_index(modes) == DrmModeMatch::kNoMatch);
}
