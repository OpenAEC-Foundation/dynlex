#pragma once

#include <string>

namespace tbx {

enum class InferredType { Unknown, Void, I1, I64, F64, String };

std::string typeToString(InferredType type);

} // namespace tbx
