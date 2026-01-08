#pragma once

namespace tbx {

// Represents a position in a document (0-indexed)
struct LspPosition {
  int line{};
  int character{};
};

} // namespace tbx
