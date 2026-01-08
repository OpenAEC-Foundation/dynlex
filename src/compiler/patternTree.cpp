#include "compiler/patternTree.hpp"
#include "compiler/patternResolver.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stack>

namespace tbx {

// ============================================================================
// PatternTree Implementation
// ============================================================================

PatternTree::PatternTree() = default;

void PatternTree::clear() {
  rootNode = PatternTreeNode();
  expressionPatterns.clear();
}

void PatternTree::addPattern(ResolvedPattern *pattern) {
  if (!pattern)
    return;

  // Track expression patterns separately for recursive matching
  if (pattern->type == PatternType::Expression) {
    expressionPatterns.push_back(pattern);
  }

  // Expand alternatives in the pattern (the one with $ slots)
  std::vector<std::string> expanded = expandAlternatives(pattern->pattern);

  // For each expanded variant, parse into elements and add to tree
  for (const auto &variant : expanded) {
    // Trim leading whitespace from variant (handles empty alternatives like
    // [|loop])
    std::string trimmedVariant = variant;
    size_t start = trimmedVariant.find_first_not_of(' ');
    if (start != std::string::npos && start > 0) {
      trimmedVariant = trimmedVariant.substr(start);
    }
    if (trimmedVariant.empty()) {
      continue; // Skip empty patterns
    }

    // Parse the expanded variant string directly
    std::vector<PatternElement> elements =
        parsePatternElementsFromString(trimmedVariant);
    addPatternPath(elements, pattern);
  }
}

std::vector<PatternElement>
PatternTree::parsePatternElements(const ResolvedPattern *pattern) {
  return parsePatternElementsFromString(pattern->pattern);
}

std::vector<PatternElement>
PatternTree::parsePatternElementsFromString(const std::string &text) {
  std::vector<PatternElement> elements;

  // Parse the pattern string which has $ for variable slots and {type:name} for
  // typed captures
  std::string currentLiteral;
  size_t charIndex = 0;

  auto flushLiteral = [&](bool addTrailingSpace) {
    if (!currentLiteral.empty()) {
      if (addTrailingSpace && currentLiteral.back() != ' ') {
        currentLiteral += " ";
      }
      PatternElement elem;
      elem.type = PatternElementType::Literal;
      elem.text = currentLiteral;
      elements.push_back(elem);
      currentLiteral.clear();
    }
  };

  auto isCaptureType = [](PatternElementType elementType) {
    return elementType == PatternElementType::Variable ||
           elementType == PatternElementType::ExpressionCapture ||
           elementType == PatternElementType::WordCapture;
  };

  while (charIndex < text.size()) {
    // Check for $ variable slot
    if (text[charIndex] == '$') {
      flushLiteral(true);
      PatternElement elem;
      elem.type = PatternElementType::Variable;
      elements.push_back(elem);
      charIndex++;
      // Skip trailing space
      if (charIndex < text.size() && text[charIndex] == ' ')
        charIndex++;
      continue;
    }

    // Check for {type:name} typed capture
    if (text[charIndex] == '{') {
      size_t braceEnd = text.find('}', charIndex);
      if (braceEnd != std::string::npos) {
        std::string captureContent =
            text.substr(charIndex + 1, braceEnd - charIndex - 1);
        size_t colonPos = captureContent.find(':');

        PatternElement elem;
        if (colonPos != std::string::npos) {
          // Typed capture: {type:name}
          std::string captureTypeString = captureContent.substr(0, colonPos);
          std::string captureNameString = captureContent.substr(colonPos + 1);

          if (captureTypeString == "expression") {
            elem.type = PatternElementType::ExpressionCapture;
          } else if (captureTypeString == "word") {
            elem.type = PatternElementType::WordCapture;
          } else {
            // Unknown type, treat as expression capture (legacy)
            elem.type = PatternElementType::ExpressionCapture;
          }
          elem.captureName = captureNameString;
        } else {
          // Legacy syntax: {name} without type = expression capture
          elem.type = PatternElementType::ExpressionCapture;
          elem.captureName = captureContent;
        }

        flushLiteral(true);
        elements.push_back(elem);
        charIndex = braceEnd + 1;
        // Skip trailing space
        if (charIndex < text.size() && text[charIndex] == ' ')
          charIndex++;
        continue;
      }
    }

    // Regular character - add to current literal
    // If we just finished a capture, add leading space
    if (currentLiteral.empty() && !elements.empty() &&
        isCaptureType(elements.back().type)) {
      currentLiteral = " ";
    }
    currentLiteral += text[charIndex];
    charIndex++;
  }

  // Flush remaining literal
  flushLiteral(false);

  return elements;
}

std::vector<std::string>
PatternTree::expandAlternatives(const std::string &patternText) {
  std::vector<std::string> results;
  results.push_back("");

  size_t charIndex = 0;
  while (charIndex < patternText.size()) {
    if (patternText[charIndex] == '[') {
      // Find matching ]
      size_t depth = 1;
      size_t start = charIndex + 1;
      size_t end = start;
      while (end < patternText.size() && depth > 0) {
        if (patternText[end] == '[')
          depth++;
        else if (patternText[end] == ']')
          depth--;
        end++;
      }
      end--; // Point to ]

      // Extract content between [ and ]
      std::string content = patternText.substr(start, end - start);

      // Split by | to get alternatives
      std::vector<std::string> alternatives;
      std::string current;
      size_t altDepth = 0;
      for (size_t contentIndex = 0; contentIndex < content.size();
           contentIndex++) {
        char character = content[contentIndex];
        if (character == '[')
          altDepth++;
        else if (character == ']')
          altDepth--;
        else if (character == '|' && altDepth == 0) {
          alternatives.push_back(current);
          current.clear();
          continue;
        }
        current += character;
      }
      alternatives.push_back(current);

      // Expand: for each existing result, create variants with each alternative
      std::vector<std::string> newResults;
      for (const auto &result : results) {
        for (const auto &alt : alternatives) {
          // Recursively expand alternatives within this alternative
          std::vector<std::string> expandedAlt = expandAlternatives(alt);
          for (const auto &exp : expandedAlt) {
            newResults.push_back(result + exp);
          }
        }
      }
      results = std::move(newResults);

      charIndex = end + 1;
    } else {
      // Regular character - append to all results
      for (auto &result : results) {
        result += patternText[charIndex];
      }
      charIndex++;
    }
  }

  return results;
}

void PatternTree::addPatternPath(const std::vector<PatternElement> &elements,
                                 ResolvedPattern *pattern) {
  PatternTreeNode *node = &rootNode;

  for (const auto &elem : elements) {
    switch (elem.type) {
    case PatternElementType::Literal: {
      // Check if this literal child exists
      auto it = node->children.find(elem.text);
      if (it == node->children.end()) {
        node->children[elem.text] = std::make_shared<PatternTreeNode>();
      }
      node = node->children[elem.text].get();
      break;
    }
    case PatternElementType::Variable: {
      // Create or follow expression child (eager evaluation)
      if (!node->expressionChild) {
        node->expressionChild = std::make_shared<PatternTreeNode>();
      }
      node = node->expressionChild.get();
      break;
    }
    case PatternElementType::ExpressionCapture: {
      // Create or follow expression capture child (lazy, greedy)
      if (!node->expressionCaptureChild) {
        node->expressionCaptureChild = std::make_shared<PatternTreeNode>();
      }
      node = node->expressionCaptureChild.get();
      break;
    }
    case PatternElementType::WordCapture: {
      // Create or follow word capture child (non-greedy, single identifier)
      if (!node->wordCaptureChild) {
        node->wordCaptureChild = std::make_shared<PatternTreeNode>();
      }
      node = node->wordCaptureChild.get();
      break;
    }
    }
  }

  // Mark pattern as ending here
  node->patternsEndedHere.push_back(pattern);
}

std::optional<TreePatternMatch> PatternTree::match(const std::string &input,
                                                   size_t startPos) {
  std::vector<MatchedValue> arguments;
  std::vector<TreePatternMatch> matches;

  matchRecursive(&rootNode, input, startPos, arguments, matches);

  if (matches.empty()) {
    return std::nullopt;
  }

  // Return best match (highest specificity, or longest consumed if equal)
  auto best = std::max_element(
      matches.begin(), matches.end(),
      [](const TreePatternMatch &leftMatch,
         const TreePatternMatch &rightMatch) {
        int specLeft = leftMatch.pattern ? leftMatch.pattern->specificity() : 0;
        int specRight =
            rightMatch.pattern ? rightMatch.pattern->specificity() : 0;
        if (specLeft != specRight)
          return specLeft < specRight;
        return leftMatch.consumedLength < rightMatch.consumedLength;
      });

  return *best;
}

std::optional<TreePatternMatch>
PatternTree::matchExpression(const std::string &input, size_t startPos) {
  // Only match expression patterns
  std::vector<TreePatternMatch> matches;

  // Try each expression pattern
  for (auto *pattern : expressionPatterns) {
    // Build a temporary tree with just this pattern
    PatternTree tempTree;
    std::vector<PatternElement> elements = parsePatternElements(pattern);

    PatternTreeNode *node = &tempTree.rootNode;
    for (const auto &elem : elements) {
      switch (elem.type) {
      case PatternElementType::Literal: {
        if (node->children.find(elem.text) == node->children.end()) {
          node->children[elem.text] = std::make_shared<PatternTreeNode>();
        }
        node = node->children[elem.text].get();
        break;
      }
      case PatternElementType::Variable: {
        if (!node->expressionChild) {
          node->expressionChild = std::make_shared<PatternTreeNode>();
        }
        node = node->expressionChild.get();
        break;
      }
      case PatternElementType::ExpressionCapture: {
        if (!node->expressionCaptureChild) {
          node->expressionCaptureChild = std::make_shared<PatternTreeNode>();
        }
        node = node->expressionCaptureChild.get();
        break;
      }
      case PatternElementType::WordCapture: {
        if (!node->wordCaptureChild) {
          node->wordCaptureChild = std::make_shared<PatternTreeNode>();
        }
        node = node->wordCaptureChild.get();
        break;
      }
      }
    }
    node->patternsEndedHere.push_back(pattern);

    // Match against this pattern
    std::vector<MatchedValue> args;
    std::vector<TreePatternMatch> patternMatchesFound;
    tempTree.matchRecursive(&tempTree.rootNode, input, startPos, args,
                            patternMatchesFound);

    for (auto &m : patternMatchesFound) {
      matches.push_back(std::move(m));
    }
  }

  if (matches.empty()) {
    return std::nullopt;
  }

  // Return best match
  auto best = std::max_element(
      matches.begin(), matches.end(),
      [](const TreePatternMatch &leftMatch,
         const TreePatternMatch &rightMatch) {
        int specLeft = leftMatch.pattern ? leftMatch.pattern->specificity() : 0;
        int specRight =
            rightMatch.pattern ? rightMatch.pattern->specificity() : 0;
        if (specLeft != specRight)
          return specLeft < specRight;
        return leftMatch.consumedLength < rightMatch.consumedLength;
      });

  return *best;
}

void PatternTree::matchRecursive(PatternTreeNode *node,
                                 const std::string &input, size_t pos,
                                 std::vector<MatchedValue> &arguments,
                                 std::vector<TreePatternMatch> &matches) {
  if (!node)
    return;

  // Check if patterns end here
  if (!node->patternsEndedHere.empty()) {
    for (auto *pattern : node->patternsEndedHere) {
      TreePatternMatch matchFound;
      matchFound.pattern = pattern;
      matchFound.arguments = arguments;
      matchFound.consumedLength = pos;
      matches.push_back(std::move(matchFound));
    }
  }

  // If we've consumed all input, we're done
  if (pos >= input.size()) {
    return;
  }

  // Try literal children (most specific first)
  for (const auto &[literal, child] : node->children) {
    // Check if input starts with this literal at current position
    if (pos + literal.size() <= input.size() &&
        input.substr(pos, literal.size()) == literal) {
      matchRecursive(child.get(), input, pos + literal.size(), arguments,
                     matches);
    }
  }

  // Try expression child ($ variable substitution - eager, greedy)
  if (node->expressionChild) {
    // Find possible expression boundaries
    std::vector<size_t> boundaries = findExpressionBoundaries(input, pos);

    for (size_t end : boundaries) {
      if (end <= pos)
        continue;

      std::string exprText = input.substr(pos, end - pos);

      // Try to match as a sub-expression first
      auto subMatch = tryMatchExpressionAt(input, pos);
      if (subMatch) {
        std::vector<MatchedValue> newArgs = arguments;
        newArgs.push_back(
            std::make_shared<ExpressionMatch>(std::move(*subMatch)));
        matchRecursive(node->expressionChild.get(), input,
                       pos + subMatch->matchedText.size(), newArgs, matches);
      }

      // Also try as a literal value
      auto literalValue = tryParseLiteral(exprText);
      if (literalValue) {
        std::vector<MatchedValue> newArgs = arguments;
        newArgs.push_back(*literalValue);
        matchRecursive(node->expressionChild.get(), input, end, newArgs,
                       matches);
      }

      // Try as a plain identifier/string
      std::vector<MatchedValue> newArgs = arguments;
      newArgs.push_back(exprText);
      matchRecursive(node->expressionChild.get(), input, end, newArgs, matches);
    }
  }

  // Try expression capture child ({expression:name} - lazy, greedy, caller's
  // scope)
  if (node->expressionCaptureChild) {
    // For expression captures, find the end of the expression (usually : or end
    // of line) This is greedy - capture as much as possible
    size_t end = input.find(':', pos);
    if (end == std::string::npos) {
      end = input.size();
    }

    // Trim trailing whitespace from captured expression
    while (end > pos && std::isspace(input[end - 1])) {
      end--;
    }

    std::string captured = input.substr(pos, end - pos);
    std::vector<MatchedValue> newArgs = arguments;
    newArgs.push_back(
        captured); // Store as string (lazy - will be re-parsed later)
    matchRecursive(node->expressionCaptureChild.get(), input, end, newArgs,
                   matches);
  }

  // Try word capture child ({word:name} - non-greedy, single identifier)
  if (node->wordCaptureChild) {
    // For word captures, match only a single identifier (non-greedy)
    size_t end = pos;

    // Skip leading whitespace
    while (end < input.size() && std::isspace(input[end])) {
      end++;
    }
    size_t wordStart = end;

    // Match identifier characters: alphanumeric and underscore
    while (end < input.size() &&
           (std::isalnum(input[end]) || input[end] == '_')) {
      end++;
    }

    if (end > wordStart) {
      std::string word = input.substr(wordStart, end - wordStart);
      std::vector<MatchedValue> newArgs = arguments;
      newArgs.push_back(word); // Store as string literal
      matchRecursive(node->wordCaptureChild.get(), input, end, newArgs,
                     matches);
    }
  }
}

std::optional<ExpressionMatch>
PatternTree::tryMatchExpressionAt(const std::string &input, size_t pos) {
  // Try to match any expression pattern at this position
  // This delegates to matchExpression which handles recursive matching

  if (expressionPatterns.empty()) {
    return std::nullopt;
  }

  // Use matchExpression to find the best expression match
  auto treeMatch = matchExpression(input, pos);

  if (!treeMatch || !treeMatch->pattern) {
    return std::nullopt;
  }

  // Convert TreePatternMatch to ExpressionMatch
  ExpressionMatch result;
  result.pattern = treeMatch->pattern;
  result.matchedText = input.substr(pos, treeMatch->consumedLength - pos);
  result.arguments = treeMatch->arguments;

  return result;
}

std::vector<size_t>
PatternTree::findExpressionBoundaries(const std::string &input, size_t start) {
  std::vector<size_t> boundaries;

  // Try progressively longer substrings
  for (size_t end = start + 1; end <= input.size(); end++) {
    // Check if this could be a valid expression boundary
    if (end == input.size()) {
      boundaries.push_back(end);
    } else if (isExpressionBoundary(input[end])) {
      boundaries.push_back(end);
    }
  }

  // Return in reverse order (longest first) for greedy matching
  std::reverse(boundaries.begin(), boundaries.end());
  return boundaries;
}

bool PatternTree::isExpressionBoundary(char character) const {
  // Expression boundaries: whitespace, operators, punctuation
  return std::isspace(character) || character == ':' || character == ',' ||
         character == ')' || character == ']';
}

std::optional<MatchedValue>
PatternTree::tryParseLiteral(const std::string &text) {
  if (text.empty())
    return std::nullopt;

  // Try to parse as integer
  bool isNumber = true;
  bool hasDot = false;
  for (size_t charIndex = 0; charIndex < text.size(); charIndex++) {
    char character = text[charIndex];
    if (character == '-' && charIndex == 0)
      continue;
    if (character == '.' && !hasDot) {
      hasDot = true;
      continue;
    }
    if (!std::isdigit(character)) {
      isNumber = false;
      break;
    }
  }

  if (isNumber && !text.empty() && text != "-" && text != ".") {
    if (hasDot) {
      return std::stod(text);
    } else {
      return std::stoll(text);
    }
  }

  // Try to parse as quoted string
  if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
      text.front() == text.back()) {
    return text.substr(1, text.size() - 2);
  }

  return std::nullopt;
}

} // namespace tbx
