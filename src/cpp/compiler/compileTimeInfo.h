#pragma once
#include "typeConstraint.h"
#include "typeReferenceValue.h"
#include <string>
#include <variant>

using CompileTimeValue = std::variant<std::monostate, double, std::string, bool, TypeReferenceValue, TypeConstraint>;
