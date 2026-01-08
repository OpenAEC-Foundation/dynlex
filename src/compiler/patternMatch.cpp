#include "compiler/patternMatch.hpp"
#include "compiler/patternResolver.hpp"
#include <iostream>

namespace tbx {

void PatternMatch::print(int indent) const {
  std::string pad(indent, ' ');
  std::cout << pad << "matches: " << patternTypeToString(pattern->type) << " \""
            << pattern->pattern << "\"\n";
  std::cout << pad << "arguments: {";
  bool first = true;
  for (const auto &[name, info] : arguments) {
    if (!first)
      std::cout << ", ";
    first = false;
    std::cout << name << ": ";

    const auto &value = info.value;
    // Print the value based on its type
    if (std::holds_alternative<int64_t>(value)) {
      std::cout << std::get<int64_t>(value);
    } else if (std::holds_alternative<double>(value)) {
      std::cout << std::get<double>(value);
    } else if (std::holds_alternative<std::string>(value)) {
      std::cout << "\"" << std::get<std::string>(value) << "\"";
    } else if (std::holds_alternative<std::shared_ptr<Section>>(value)) {
      std::cout << "[section]";
    }
  }
  std::cout << "}\n";
}

} // namespace tbx
