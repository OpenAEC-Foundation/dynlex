#pragma once

#include <string>

namespace tbx {

// Stack frame information
struct StackFrame {
  int id{};
  std::string name;
  std::string source;
  int line{};
  int column{};
};

} // namespace tbx
