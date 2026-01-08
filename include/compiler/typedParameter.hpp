#pragma once

#include "compiler/inferredType.hpp"

#include <string>

namespace tbx {

struct TypedParameter {
  std::string name;
  InferredType type = InferredType::Unknown;

  void print(int indent = 0) const;
};

} // namespace tbx
