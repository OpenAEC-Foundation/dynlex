#pragma once

#include <string>

namespace tbx {

// Variable information
struct Variable {
  std::string name;
  std::string value;
  std::string type;
  int variablesReference{}; // 0 = no children, >0 = has children
};

} // namespace tbx
