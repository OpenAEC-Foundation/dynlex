#pragma once

#include <string>

namespace tbx {

// Represents an open document
struct TextDocument {
  std::string uri;
  std::string content;
  int version{};
};

} // namespace tbx
