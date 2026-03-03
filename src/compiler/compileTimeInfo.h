#pragma once
#include <string>
#include <variant>

using CompileTimeValue = std::variant<std::monostate, double, std::string, bool>;
