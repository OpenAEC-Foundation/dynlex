#pragma once
#include <string>
#include <string_view>

std::string digest(std::string_view text);
std::string canonicalSourceKey(std::string_view path);
