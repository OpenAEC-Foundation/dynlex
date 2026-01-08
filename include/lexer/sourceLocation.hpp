#pragma once

#include <string>

namespace tbx {

struct SourceLocation {
  size_t line{};
  size_t column{};
  std::string filename;
};

} // namespace tbx
