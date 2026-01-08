#pragma once

#include "compiler/inferredType.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace tbx {

struct IntrinsicInfo {
  std::string name;
  std::vector<std::string> arguments;
  bool hasReturn = false;

  InferredType
  getReturnType(const std::map<std::string, InferredType> &argTypes) const;
  InferredType
  getArgumentType(size_t index,
                  const std::map<std::string, InferredType> &knownTypes) const;
};

} // namespace tbx
