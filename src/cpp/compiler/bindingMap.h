#pragma once

#include <string>
#include <unordered_map>

struct Expression;

using BindingMap = std::unordered_map<std::string, Expression *>;
