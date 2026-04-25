// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {

/// Initiates shutdown/reboot of the machine running helixscreen. Tries logind
/// via sd-bus first; falls back to systemctl if sd-bus is unavailable or the
/// call fails. Returns true if one of the mechanisms dispatched successfully.
class SystemPower {
  public:
    static bool reboot_local();
    static bool shutdown_local();
};

} // namespace helix
