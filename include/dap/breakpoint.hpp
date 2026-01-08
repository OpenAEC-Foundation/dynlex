#pragma once

#include <string>

namespace tbx {

// Breakpoint information
struct Breakpoint {
  int id{};
  std::string source;
  int line{};
  bool verified{};
};

} // namespace tbx
