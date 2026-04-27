// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "platform_capabilities.h"

#include <optional>

namespace helix::config_testing {

/// Force a platform tier for v15→v16 screensaver migration testing.
/// Pass std::nullopt to restore live detection. Production code must never call this.
void set_forced_tier_for_migration(std::optional<helix::PlatformTier> tier);

} // namespace helix::config_testing
