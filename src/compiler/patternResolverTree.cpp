#include "compiler/patternResolver.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// ============================================================================
// Pattern Tree Integration Implementation
// ============================================================================

void SectionPatternResolver::buildPatternTrees() {
  // Clear existing trees
  effectTree.clear();
  sectionTree.clear();
  expressionTree.clear();

  // Add all resolved patterns to the appropriate trees
  for (auto &pattern : patternDefinitionsData) {
    if (!pattern || !pattern->sourceLine->isResolved) {
      continue;
    }

    // Skip private patterns - they'll be handled specially during matching
    // by checking file visibility

    switch (pattern->type) {
    case PatternType::Effect:
      effectTree.addPattern(pattern.get());
      break;
    case PatternType::Section:
      sectionTree.addPattern(pattern.get());
      break;
    case PatternType::Expression:
      expressionTree.addPattern(pattern.get());
      break;
    }
  }
}

PatternMatch *SectionPatternResolver::matchWithTree(CodeLine *line) {
  if (!line || line->isPatternDefinition) {
    return nullptr;
  }

  std::string referenceText = line->getPatternText();

  // Phase 1: Detect literals (already done during preprocessing)
  // Phase 2: Intrinsic arguments are parsed during literal detection

  // Phase 3: Determine which tree to use
  // Lines ending with ":" are sections/effects with sections
  // Lines without ":" are pure effects
  bool hasSection = line->hasChildSection();

  std::optional<TreePatternMatch> treeMatch;

  if (hasSection) {
    // Try section tree first
    treeMatch = sectionTree.match(referenceText);
    if (!treeMatch) {
      // Fall back to effect tree
      treeMatch = effectTree.match(referenceText);
    }
  } else {
    // Pure effect (no child section)
    treeMatch = effectTree.match(referenceText);
  }

  if (!treeMatch || !treeMatch->pattern) {
    return nullptr;
  }

  // Convert tree match to pattern match
  auto match = treeMatchToPatternMatch(*treeMatch, treeMatch->pattern);
  if (!match) {
    return nullptr;
  }

  lineToMatchData[line] = match.get();
  patternMatchesData.push_back(std::move(match));
  return patternMatchesData.back().get();
}

std::vector<SectionPatternResolver::ParsedLiteral>
SectionPatternResolver::detectLiterals(const std::string &input) {
  std::vector<ParsedLiteral> literals;
  size_t charIndex = 0;

  while (charIndex < input.size()) {
    // Skip whitespace
    if (std::isspace(input[charIndex])) {
      charIndex++;
      continue;
    }

    // Check for intrinsic call: @name(...)
    if (input[charIndex] == '@') {
      std::string name;
      std::vector<std::string> args;
      size_t end = parseIntrinsicCall(input, charIndex, name, args);
      if (end != std::string::npos) {
        ParsedLiteral lit;
        lit.type = ParsedLiteral::Type::Intrinsic;
        lit.text = input.substr(charIndex, end - charIndex);
        lit.startPos = charIndex;
        lit.endPos = end;
        lit.intrinsicArgs = std::move(args);
        literals.push_back(std::move(lit));
        charIndex = end;
        continue;
      }
    }

    // Check for string literal: "..." or '...'
    if (input[charIndex] == '"' || input[charIndex] == '\'') {
      char quote = input[charIndex];
      size_t start = charIndex;
      charIndex++; // Skip opening quote
      while (charIndex < input.size() && input[charIndex] != quote) {
        if (input[charIndex] == '\\' && charIndex + 1 < input.size()) {
          charIndex += 2; // Skip escape sequence
        } else {
          charIndex++;
        }
      }
      if (charIndex < input.size()) {
        charIndex++; // Skip closing quote
      }
      ParsedLiteral lit;
      lit.type = ParsedLiteral::Type::String;
      lit.text = input.substr(start, charIndex - start);
      lit.startPos = start;
      lit.endPos = charIndex;
      literals.push_back(std::move(lit));
      continue;
    }

    // Check for number literal: 123, 3.14, -7
    if (std::isdigit(input[charIndex]) ||
        (input[charIndex] == '-' && charIndex + 1 < input.size() &&
         std::isdigit(input[charIndex + 1]))) {
      size_t start = charIndex;
      if (input[charIndex] == '-')
        charIndex++;
      while (charIndex < input.size() && std::isdigit(input[charIndex]))
        charIndex++;
      if (charIndex < input.size() && input[charIndex] == '.') {
        charIndex++;
        while (charIndex < input.size() && std::isdigit(input[charIndex]))
          charIndex++;
      }
      ParsedLiteral lit;
      lit.type = ParsedLiteral::Type::Number;
      lit.text = input.substr(start, charIndex - start);
      lit.startPos = start;
      lit.endPos = charIndex;
      literals.push_back(std::move(lit));
      continue;
    }

    // Check for grouping parentheses: (...)
    if (input[charIndex] == '(') {
      size_t start = charIndex;
      int depth = 1;
      charIndex++;
      while (charIndex < input.size() && depth > 0) {
        if (input[charIndex] == '(')
          depth++;
        else if (input[charIndex] == ')')
          depth--;
        charIndex++;
      }
      ParsedLiteral lit;
      lit.type = ParsedLiteral::Type::Group;
      lit.text = input.substr(start, charIndex - start);
      lit.startPos = start;
      lit.endPos = charIndex;
      literals.push_back(std::move(lit));
      continue;
    }

    // Skip other characters (identifiers, operators, etc.)
    charIndex++;
  }

  return literals;
}

size_t
SectionPatternResolver::parseIntrinsicCall(const std::string &input,
                                           size_t startPos, std::string &name,
                                           std::vector<std::string> &args) {
  if (startPos >= input.size() || input[startPos] != '@') {
    return std::string::npos;
  }

  // Parse name: @name
  size_t charIndex = startPos + 1;
  size_t nameStart = charIndex;
  while (charIndex < input.size() &&
         (std::isalnum(input[charIndex]) || input[charIndex] == '_')) {
    charIndex++;
  }
  if (charIndex == nameStart) {
    return std::string::npos; // No name
  }
  name = input.substr(nameStart, charIndex - nameStart);

  // Expect opening paren
  if (charIndex >= input.size() || input[charIndex] != '(') {
    return std::string::npos;
  }
  charIndex++; // Skip '('
  // Parse arguments
  args.clear();
  std::string currentArg;
  int parenDepth = 1;
  bool inString = false;
  char stringChar = '\0';

  while (charIndex < input.size() && parenDepth > 0) {
    char character = input[charIndex];

    if (inString) {
      currentArg += character;
      if (character == stringChar &&
          (currentArg.size() < 2 ||
           currentArg[currentArg.size() - 2] != '\\')) {
        inString = false;
      }
    } else if (character == '"' || character == '\'') {
      inString = true;
      stringChar = character;
      currentArg += character;
    } else if (character == '(') {
      parenDepth++;
      currentArg += character;
    } else if (character == ')') {
      parenDepth--;
      if (parenDepth > 0) {
        currentArg += character;
      }
    } else if (character == ',' && parenDepth == 1) {
      // Argument separator
      // Trim whitespace
      size_t startIdx = currentArg.find_first_not_of(" \t");
      size_t endIdx = currentArg.find_last_not_of(" \t");
      if (startIdx != std::string::npos) {
        args.push_back(currentArg.substr(startIdx, endIdx - startIdx + 1));
      }
      currentArg.clear();
    } else {
      currentArg += character;
    }
    charIndex++;
  }

  // Add last argument
  if (!currentArg.empty()) {
    size_t startIdx = currentArg.find_first_not_of(" \t");
    size_t endIdx = currentArg.find_last_not_of(" \t");
    if (startIdx != std::string::npos) {
      args.push_back(currentArg.substr(startIdx, endIdx - startIdx + 1));
    }
  }

  if (parenDepth != 0) {
    return std::string::npos; // Unbalanced parens
  }

  return charIndex;
}

std::unique_ptr<PatternMatch> SectionPatternResolver::treeMatchToPatternMatch(
    const TreePatternMatch &treeMatch, const ResolvedPattern *pattern) {
  if (!pattern) {
    return nullptr;
  }

  auto match = std::make_unique<PatternMatch>();
  match->pattern = const_cast<ResolvedPattern *>(pattern);

  // Map treeMatch.arguments to named variables
  size_t argIdx = 0;
  for (const auto &varName : pattern->variables) {
    if (argIdx >= treeMatch.arguments.size()) {
      break;
    }

    const auto &arg = treeMatch.arguments[argIdx];

    // Convert MatchedValue to ResolvedValue
    if (std::holds_alternative<int64_t>(arg)) {
      match->arguments[varName] =
          PatternMatch::ArgumentInfo(std::get<int64_t>(arg));
    } else if (std::holds_alternative<double>(arg)) {
      match->arguments[varName] =
          PatternMatch::ArgumentInfo(std::get<double>(arg));
    } else if (std::holds_alternative<std::string>(arg)) {
      match->arguments[varName] =
          PatternMatch::ArgumentInfo(std::get<std::string>(arg));
    } else if (std::holds_alternative<std::shared_ptr<ExpressionMatch>>(arg)) {
      // For nested expressions, store as string for now
      // TODO: Store the actual ExpressionMatch for proper code generation
      auto exprMatch = std::get<std::shared_ptr<ExpressionMatch>>(arg);
      if (exprMatch) {
        match->arguments[varName] =
            PatternMatch::ArgumentInfo(exprMatch->matchedText);
      }
    }

    argIdx++;
  }

  return match;
}

} // namespace tbx
