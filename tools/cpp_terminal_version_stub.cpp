// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for cpp-terminal version functions (normally cmake-generated)

#include <cstdint>
#include <string>

namespace Term {
namespace Version {
std::uint16_t major() noexcept { return 0; }
std::uint16_t minor() noexcept { return 0; }
std::uint16_t patch() noexcept { return 0; }
std::string string() noexcept { return "0.0.0"; }
}  // namespace Version
std::string homepage() noexcept { return "https://github.com/jupyter-xeus/cpp-terminal"; }
}  // namespace Term
