#pragma once

#include "compiler/inferredType.hpp"

#include <map>
#include <string>
#include <vector>

namespace tbx {

// Forward declaration
struct ResolvedPattern;

struct TypedPattern {
  ResolvedPattern *pattern = nullptr;
  std::map<std::string, InferredType> parameterTypes;
  InferredType returnType = InferredType::Unknown;
  std::vector<std::string> bodyIntrinsics;

  void print(int indent = 0) const;
};

} // namespace tbx
