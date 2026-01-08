#pragma once

#include "compiler/resolvedPattern.hpp"
#include "compiler/sectionAnalyzer.hpp"
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace tbx {

/**
 * Represents a match of a code line against a pattern
 */
struct PatternMatch {
  ResolvedPattern *pattern = nullptr;

  struct ArgumentInfo {
    ResolvedValue value;
    int startCol;
    int length;
    bool isLiteral;

    ArgumentInfo() : value(""), startCol(0), length(0), isLiteral(false) {}
    ArgumentInfo(ResolvedValue val, int startColumn = 0, int len = 0,
                 bool lit = false)
        : value(val), startCol(startColumn), length(len), isLiteral(lit) {}
  };

  std::map<std::string, ArgumentInfo> arguments;

  // Print for debugging
  void print(int indent = 0) const;
};

} // namespace tbx
