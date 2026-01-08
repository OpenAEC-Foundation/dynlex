#pragma once

#include <string>

namespace tbx {

// Scope information
struct Scope {
  std::string name;
  int variablesReference{};
  bool expensive{};
};

} // namespace tbx
