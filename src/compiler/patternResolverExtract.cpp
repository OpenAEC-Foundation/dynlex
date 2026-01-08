#include "compiler/patternResolver.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// Helper to trim whitespace
static std::string trimExtract(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// ============================================================================
// Pattern Extraction Implementation
// ============================================================================

std::vector<std::unique_ptr<ResolvedPattern>>
SectionPatternResolver::extractPatternDefinitions(CodeLine *line) {
  std::vector<std::unique_ptr<ResolvedPattern>> patterns;
  if (!line || !line->isPatternDefinition) {
    return patterns;
  }

  // Check for "patterns:" subsection
  Section *body = line->childSection.get();
  if (body) {
    for (const auto &bodyLine : body->lines) {
      std::string text = bodyLine.text;
      size_t start = text.find_first_not_of(" \t");
      if (start != std::string::npos) {
        text = text.substr(start);
      }
      if (text == "patterns:") {
        if (bodyLine.childSection) {
          for (const auto &altLine : bodyLine.childSection->lines) {
            // Create a temporary pattern definition
            auto pattern = std::make_unique<ResolvedPattern>();
            pattern->sourceLine = const_cast<CodeLine *>(&altLine);
            pattern->body = line->childSection.get();
            pattern->isPrivate = line->isPrivate;
            pattern->type = line->type;
            pattern->originalText = altLine.text;

            std::vector<std::string> words = parsePatternWords(altLine.text);
            pattern->variables =
                identifyVariablesFromBody(words, pattern->body);

            // Support {expression:name} in patterns: blocks
            for (const auto &word : words) {
              if (word.size() >= 3 && word[0] == '{' && word.back() == '}') {
                std::string inner = word.substr(1, word.size() - 2);
                size_t colonPos = inner.find(':');
                std::string varName = (colonPos != std::string::npos)
                                          ? inner.substr(colonPos + 1)
                                          : inner;

                bool alreadyVar = false;
                for (const auto &v : pattern->variables) {
                  if (v == varName) {
                    alreadyVar = true;
                    break;
                  }
                }
                if (!alreadyVar)
                  pattern->variables.push_back(varName);
              }
            }

            pattern->pattern = createPatternString(words, pattern->variables);
            patterns.push_back(std::move(pattern));
          }
        }
      }
    }
  }

  // Always include the pattern from the definition line itself if it has text
  std::string mainText = line->getPatternText();
  if (!mainText.empty()) {
    auto mainPattern = extractPatternDefinition(line);
    if (mainPattern) {
      patterns.push_back(std::move(mainPattern));
    }
  }

  return patterns;
}

std::unique_ptr<ResolvedPattern>
SectionPatternResolver::extractPatternDefinition(CodeLine *line) {
  if (!line || !line->isPatternDefinition) {
    return nullptr;
  }

  auto pattern = std::make_unique<ResolvedPattern>();
  pattern->sourceLine = line;
  pattern->body = line->childSection.get();
  pattern->isPrivate = line->isPrivate;
  pattern->type = line->type;

  // Get the pattern text (without type prefixes or "private ")
  std::string text = line->getPatternText();
  pattern->originalText = text;

  // Parse the pattern into words
  std::vector<std::string> words = parsePatternWords(text);

  // Identify which words are variables by analyzing @intrinsic usage in body
  pattern->variables = identifyVariablesFromBody(words, pattern->body);

  // Also look for expressions captured via {expression:name} in the pattern
  // text
  for (const auto &word : words) {
    if (word.size() >= 3 && word[0] == '{' && word.back() == '}') {
      std::string inner = word.substr(1, word.size() - 2);
      size_t colonPos = inner.find(':');
      std::string varName =
          (colonPos != std::string::npos) ? inner.substr(colonPos + 1) : inner;

      bool alreadyVar = false;
      for (const auto &v : pattern->variables) {
        if (v == varName) {
          alreadyVar = true;
          break;
        }
      }
      if (!alreadyVar) {
        pattern->variables.push_back(varName);
      }
    }
  }

  // Create the pattern string with $ for variable slots
  pattern->pattern = createPatternString(words, pattern->variables);

  return pattern;
}

std::vector<std::string>
SectionPatternResolver::parsePatternWords(const std::string &text) {
  std::vector<std::string> words;
  std::string current;
  bool inQuotes = false;
  char quoteChar = '\0';

  for (size_t charIndex = 0; charIndex < text.size(); charIndex++) {
    char character = text[charIndex];

    if (inQuotes) {
      current += character;
      if (character == quoteChar) {
        inQuotes = false;
        words.push_back(current);
        current.clear();
      }
    } else if (character == '"') {
      // Double quotes always start/end quoted strings
      if (!current.empty()) {
        words.push_back(current);
        current.clear();
      }
      inQuotes = true;
      quoteChar = character;
      current += character;
    } else if (character == '\'') {
      // Single quote: could be possessive (var's) or quote start ('hello')
      // Check if next char is alphanumeric (possessive like var's, don't,
      // isn't) or if we're at start/after space (quote start like 'hello')
      bool isPossessive = false;
      if (!current.empty() && charIndex + 1 < text.size()) {
        char nextChar = text[charIndex + 1];
        // Possessive: 's, 't (don't, isn't), etc.
        if (std::isalpha(nextChar)) {
          isPossessive = true;
        }
      }

      if (isPossessive) {
        // Treat as part of the current word, but split for pattern matching
        // Push current word without the apostrophe suffix
        if (!current.empty()) {
          words.push_back(current);
          current.clear();
        }
        // Start new word with apostrophe
        current += character;
      } else {
        // Quote start
        if (!current.empty()) {
          words.push_back(current);
          current.clear();
        }
        inQuotes = true;
        quoteChar = character;
        current += character;
      }
    } else if (std::isspace(character)) {
      if (!current.empty()) {
        words.push_back(current);
        current.clear();
      }
    } else {
      current += character;
    }
  }

  if (!current.empty()) {
    words.push_back(current);
  }

  return words;
}

// Helper to extract intrinsic arguments from a line
static std::vector<std::string> extractIntrinsicArgs(const std::string &line) {
  std::vector<std::string> args;

  size_t pos = line.find("@intrinsic(");
  if (pos == std::string::npos)
    return args;

  // Find the opening paren
  pos += 10; // length of "@intrinsic"
  if (pos >= line.size() || line[pos] != '(')
    return args;
  pos++; // skip '('

  // Find matching closing paren
  int depth = 1;
  size_t start = pos;
  size_t end = pos;
  while (end < line.size() && depth > 0) {
    if (line[end] == '(')
      depth++;
    else if (line[end] == ')')
      depth--;
    end++;
  }
  if (depth != 0)
    return args;
  end--; // point to ')'

  // Parse comma-separated arguments
  std::string content = line.substr(start, end - start);
  std::string current;
  bool inQuotes = false;
  char quoteChar = '\0';
  int parenDepth = 0;

  for (size_t charIndex = 0; charIndex < content.size(); charIndex++) {
    char character = content[charIndex];

    if (inQuotes) {
      current += character;
      if (character == quoteChar) {
        inQuotes = false;
      }
    } else if (character == '"' || character == '\'') {
      inQuotes = true;
      quoteChar = character;
      current += character;
    } else if (character == '(') {
      parenDepth++;
      current += character;
    } else if (character == ')') {
      parenDepth--;
      current += character;
    } else if (character == ',' && parenDepth == 0) {
      // Trim and add
      size_t startIdx = current.find_first_not_of(" \t");
      size_t endIdx = current.find_last_not_of(" \t");
      if (startIdx != std::string::npos) {
        args.push_back(current.substr(startIdx, endIdx - startIdx + 1));
      }
      current.clear();
    } else {
      current += character;
    }
  }

  // Add last argument
  if (!current.empty()) {
    size_t startIdx = current.find_first_not_of(" \t");
    size_t endIdx = current.find_last_not_of(" \t");
    if (startIdx != std::string::npos) {
      args.push_back(current.substr(startIdx, endIdx - startIdx + 1));
    }
  }

  // Skip first argument (intrinsic name in quotes)
  if (!args.empty()) {
    args.erase(args.begin());
  }

  return args;
}

// Helper to collect all @intrinsic arguments from a section recursively
static void collectIntrinsicArgsFromSection(Section *section,
                                            std::vector<std::string> &allArgs) {
  if (!section)
    return;

  for (const auto &line : section->lines) {
    auto args = extractIntrinsicArgs(line.text);
    for (const auto &arg : args) {
      allArgs.push_back(arg);
    }

    // Recurse into child sections
    if (line.childSection) {
      collectIntrinsicArgsFromSection(line.childSection.get(), allArgs);
    }
  }
}

// Extract the identifier portion from a pattern word
// e.g., "number%" -> "number", "var's" -> "var", "count" -> "count"
static std::string extractIdentifier(const std::string &word) {
  std::string result;
  for (char character : word) {
    if (std::isalnum(character) || character == '_') {
      result += character;
    } else {
      // Stop at first non-identifier character
      break;
    }
  }
  return result;
}

// Check if word contains identifier at the start (for patterns like "number%")
static bool startsWithIdentifier(const std::string &word,
                                 const std::string &identifier) {
  if (identifier.empty() || word.size() < identifier.size()) {
    return false;
  }
  // Check if word starts with the identifier
  if (word.substr(0, identifier.size()) != identifier) {
    return false;
  }
  // Check that identifier is followed by non-alphanumeric or end
  if (word.size() > identifier.size()) {
    char nextChar = word[identifier.size()];
    return !std::isalnum(nextChar) && nextChar != '_';
  }
  return true;
}

std::vector<std::string> SectionPatternResolver::identifyVariables(
    const std::vector<std::string> &words) {
  // This version is called without body context - return empty
  // The actual variable deduction happens in identifyVariablesFromBody
  (void)words;
  return {};
}

std::vector<std::string> SectionPatternResolver::identifyVariablesFromBody(
    const std::vector<std::string> &patternWords, Section *body) {
  std::vector<std::string> variables;

  // Special case: if there's only one word, it's the pattern name (literal),
  // not a variable
  if (patternWords.size() == 1 &&
      !(patternWords[0].size() >= 3 && patternWords[0][0] == '{' &&
        patternWords[0].back() == '}')) {
    return variables;
  }

  // Collect all @intrinsic arguments from the body
  std::vector<std::string> intrinsicArgs;
  collectIntrinsicArgsFromSection(body, intrinsicArgs);

  // A word from the pattern is a variable if:
  // 1. It's a braced variable {word} or {type:name} - always a variable (typed
  // capture)
  // 2. It appears as an intrinsic argument
  for (const auto &word : patternWords) {
    // Check for braced variables: {word} or {type:name} indicates capture,
    // ALWAYS a variable
    if (word.size() >= 3 && word[0] == '{' && word.back() == '}') {
      std::string inner = word.substr(1, word.size() - 2);

      // Check for typed capture: {type:name}
      size_t colonPos = inner.find(':');
      std::string varName;
      if (colonPos != std::string::npos) {
        // Typed capture - extract name after colon
        varName = inner.substr(colonPos + 1);
      } else {
        // Legacy syntax {name} - use the whole inner part
        varName = inner;
      }

      // Check we haven't already added it
      bool alreadyAdded = false;
      for (const auto &v : variables) {
        if (v == varName) {
          alreadyAdded = true;
          break;
        }
      }
      if (!alreadyAdded) {
        variables.push_back(varName);
      }
      continue;
    }

    // Skip quoted strings - they are literals in the pattern
    if (!word.empty() && (word[0] == '"' || word[0] == '\'')) {
      continue;
    }

    // Skip pure operators/punctuation
    bool hasAlnum = false;
    for (char character : word) {
      if (std::isalnum(character) || character == '_') {
        hasAlnum = true;
        break;
      }
    }
    if (!hasAlnum) {
      continue;
    }

    // Extract the identifier part from this word (e.g., "number" from
    // "number%")
    std::string identifier = extractIdentifier(word);
    if (identifier.empty()) {
      continue;
    }

    // Check if this identifier appears as an intrinsic argument
    for (const auto &arg : intrinsicArgs) {
      // The argument must be the identifier itself (not a literal)
      // Literals: numbers, quoted strings
      if (arg == identifier) {
        // Check we haven't already added it
        bool alreadyAdded = false;
        for (const auto &v : variables) {
          if (v == identifier) {
            alreadyAdded = true;
            break;
          }
        }
        if (!alreadyAdded) {
          variables.push_back(identifier);
        }
        break;
      }
    }
  }

  return variables;
}

std::string SectionPatternResolver::createPatternString(
    const std::vector<std::string> &words,
    const std::vector<std::string> &variables) {
  std::string result;

  for (const auto &word : words) {
    // Don't add space before words starting with apostrophe
    // (possessive/contraction)
    if (!result.empty() && !word.empty() && word[0] != '\'') {
      result += " ";
    }

    // Check if this is a braced variable {foo} or {type:name}
    if (word.size() >= 3 && word[0] == '{' && word.back() == '}') {
      std::string inner = word.substr(1, word.size() - 2);

      // Check for typed capture: {type:name}
      size_t colonPos = inner.find(':');
      std::string varName;
      if (colonPos != std::string::npos) {
        // Typed capture - extract name after colon
        varName = inner.substr(colonPos + 1);
      } else {
        // Legacy syntax {name} - use the whole inner part
        varName = inner;
      }

      // Check if the variable name is in our variables list
      bool isVar = false;
      for (const auto &var : variables) {
        if (varName == var) {
          isVar = true;
          break;
        }
      }
      if (isVar) {
        // Keep the full braced syntax for typed captures
        // The pattern tree parser will handle it
        result += word;
      } else {
        result += word;
      }
      continue;
    }

    // Check if this word is a variable or contains a variable at the start
    bool foundVar = false;
    for (const auto &var : variables) {
      if (word == var) {
        // Exact match - replace entire word with $
        result += "$";
        foundVar = true;
        break;
      } else if (startsWithIdentifier(word, var)) {
        // Partial match (e.g., "number%" with var "number") - replace
        // identifier with $
        result += "$";
        result += word.substr(var.size()); // Append the suffix (e.g., "%")
        foundVar = true;
        break;
      }
    }

    if (!foundVar) {
      result += word;
    }
  }

  return result;
}

bool SectionPatternResolver::isIntrinsicCall(const std::string &text) const {
  // Check if the text is an intrinsic call like @intrinsic("name", ...)
  std::string trimmed = text;
  // Remove leading/trailing whitespace
  size_t start = trimmed.find_first_not_of(" \t");
  if (start == std::string::npos)
    return false;
  trimmed = trimmed.substr(start);

  // Only match direct intrinsic calls, not all return statements
  return trimmed.rfind("@intrinsic(", 0) == 0 ||
         trimmed.rfind("return @intrinsic(", 0) == 0;
}

bool SectionPatternResolver::isPatternBodyDirective(
    const std::string &text) const {
  // Structural directives are specific keywords within pattern definitions
  // that introduce metadata or sub-sections (e.g., "get:", "execute:",
  // "patterns:", "priority:", "when parsed:").
  // These are NOT regular code lines - only known pattern body keywords.
  std::string trimmed = trimExtract(text);
  if (trimmed.empty()) {
    return false;
  }

  // Check for known pattern body directives
  // These must be exact matches at the start of the line
  static const std::vector<std::string> knownDirectives = {
      "get:",         "execute:",        "patterns:", "priority:",
      "priority ", // priority can have arguments like "priority before ..."
      "when parsed:", "when triggered:", "check:"};

  for (const auto &directive : knownDirectives) {
    if (trimmed.rfind(directive, 0) == 0) {
      return true;
    }
  }
  return false;
}

bool SectionPatternResolver::isSingleWordWithSection(
    const CodeLine *line) const {
  if (!line || !line->hasChildSection()) {
    return false;
  }

  // Get pattern text without trailing colon
  std::string text = line->getPatternText();

  // Check if it's a single word (no spaces) or a known structural marker
  return text.find(' ') == std::string::npos && !text.empty();
}

bool SectionPatternResolver::isInsidePatternsSection(CodeLine *line) const {
  if (!line)
    return false;

  // Find the parent section of this line
  for (Section *section : allSectionsData) {
    for (const auto &secLine : section->lines) {
      if (&secLine == line) {
        // Found the parent section, now check if this section's parent line is
        // "patterns:"
        if (section->parent) {
          // Find the parent line that owns this section
          for (auto &parentLine : section->parent->lines) {
            if (parentLine.childSection.get() == section) {
              // Check if the parent line is "patterns:"
              std::string parentText = parentLine.getPatternText();
              // Trim whitespace
              size_t start = parentText.find_first_not_of(" \t");
              if (start != std::string::npos) {
                parentText = parentText.substr(start);
              }
              return parentText == "patterns";
            }
          }
        }
        return false;
      }
    }
  }
  return false;
}

} // namespace tbx
