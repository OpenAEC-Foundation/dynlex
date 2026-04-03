#pragma once

#include <string>
#include <unordered_map>

struct Expression;
struct VariableReference;

using BindingMap = std::unordered_map<std::string, Expression *>;
using ParameterBindingMap = std::unordered_map<VariableReference *, Expression *>;
