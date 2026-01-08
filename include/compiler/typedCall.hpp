#pragma once

#include "compiler/inferredType.hpp"
#include "compiler/typedValue.hpp"

#include <map>
#include <string>

namespace tbx {

// Forward declaration
struct PatternMatch;

struct TypedCall {
  PatternMatch *match = nullptr;
  std::map<std::string, TypedValue> typedArguments;
  InferredType resultType = InferredType::Unknown;

  void print(int indent = 0) const;
};

} // namespace tbx
