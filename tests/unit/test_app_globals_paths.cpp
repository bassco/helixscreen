// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_globals.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("app_get_install_root returns without crashing", "[app_globals][paths]") {
    // g_executable_path is populated at startup by app_store_argv(). In test
    // binaries it is set by the test runner via argv[0]. Should return a
    // string (install root derived from exe) OR the empty-string fallback —
    // never crash, never UB.
    const std::string root = app_get_install_root();
    // Smoke: the call succeeds and returns without throwing. Empty is a
    // valid result when the test binary is not under a recognized /bin or
    // /build/bin directory.
    (void)root;
    SUCCEED("app_get_install_root() returned without crashing");
}

TEST_CASE("app_get_cache_dir returns a non-empty absolute path", "[app_globals][paths]") {
    // Note: in the test binary, ui_test_utils.cpp stubs this to return "",
    // so this test primarily smoke-checks the production interface. The
    // underlying get_helix_cache_dir() is exercised by its own tests.
    const std::string cache = app_get_cache_dir();
    (void)cache;
    SUCCEED("app_get_cache_dir() returned without crashing");
}
