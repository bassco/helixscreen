// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace helix {

/// True when the given Moonraker host string refers to the machine running
/// helixscreen. Checked in order: loopback literals → gethostname() →
/// getifaddrs() scan. Result cached per distinct host string.
bool is_moonraker_on_same_host(std::string_view host);

/// Drop the detection cache. Call when moonraker_host changes at runtime.
void invalidate_host_identity_cache();

} // namespace helix
