// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace helix {

/// Compute SHA256 hex digest of a file. Returns empty string on error.
std::string compute_file_sha256(const std::string& file_path);

} // namespace helix
