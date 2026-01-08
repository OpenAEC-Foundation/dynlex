#include "compiler/resolvedPattern.hpp"
#include "compiler/patternResolver.hpp"
#include <iostream>
#include <sstream>

namespace tbx {

bool ResolvedPattern::isSingleWord() const {
  // Count non-$ words in the pattern
  std::istringstream iss(pattern);
  std::string word;
  int wordCount = 0;
  while (iss >> word) {
    wordCount++;
  }
  return wordCount == 1 && pattern.find('$') == std::string::npos;
}

int ResolvedPattern::specificity() const {
  // Count literal words (non-$ elements)
  std::istringstream iss(pattern);
  std::string word;
  int literalCount = 0;
  while (iss >> word) {
    if (word != "$") {
      literalCount++;
    }
  }
  return literalCount;
}

void ResolvedPattern::print(int indent) const {
  std::string pad(indent, ' ');
  std::cout << pad << "- " << patternTypeToString(type) << " \"" << pattern
            << "\"\n";
  std::cout << pad << "    variables: [";
  for (size_t varIndex = 0; varIndex < variables.size(); varIndex++) {
    if (varIndex > 0)
      std::cout << ", ";
    std::cout << variables[varIndex];
  }
  std::cout << "]\n";
  if (body && !body->lines.empty()) {
    std::cout << pad << "    body:\n";
    for (const auto &line : body->lines) {
      std::cout << pad << "      " << line.text << "\n";
    }
  }
}

} // namespace tbx
