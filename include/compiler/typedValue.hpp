#pragma once

#include "compiler/inferredType.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace tbx {

struct TypedValue {
  InferredType type = InferredType::Unknown;
  std::variant<std::monostate, int64_t, double, std::string> value;
  std::string variableName;
  bool isLiteral = false;

  static TypedValue fromInt(int64_t val);
  static TypedValue fromDouble(double val);
  static TypedValue fromString(const std::string &val);
  static TypedValue fromVariable(const std::string &name, InferredType type);

  void print(int indent = 0) const;
};

} // namespace tbx
