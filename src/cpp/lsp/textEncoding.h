#pragma once
#include <string_view>

namespace lsp {

// Columns clamp to the line end, or to the start of a partially addressed code point.
int utf16Column(std::string_view text, int byteColumn);
int byteColumn(std::string_view text, int utf16Column);

} // namespace lsp
