#pragma once

#include <string>
#include <unordered_map>

struct Function;

using BindingMap = std::unordered_map<std::string, Function *>;
