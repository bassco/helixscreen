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
