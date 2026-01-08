#include "compiler/patternResolver.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// Helper to trim whitespace
static std::string trim(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// Helper to check if a word appears as a standalone identifier in a string
// e.g., "val" appears in "var + val" but not in "value"
static bool containsIdentifier(const std::string &text,
                               const std::string &word) {
  if (word.empty())
    return false;

  size_t pos = 0;
  while ((pos = text.find(word, pos)) != std::string::npos) {
    // Check if the character before is not alphanumeric/underscore (or at
    // start)
    bool validStart =
        (pos == 0) || (!std::isalnum(text[pos - 1]) && text[pos - 1] != '_');

    // Check if the character after is not alphanumeric/underscore (or at end)
    size_t endPos = pos + word.size();
    bool validEnd = (endPos >= text.size()) ||
                    (!std::isalnum(text[endPos]) && text[endPos] != '_');

    if (validStart && validEnd) {
      return true;
    }
    pos++;
  }
  return false;
}

// Extract the identifier portion from a pattern word
// e.g., "number%" -> "number", "var's" -> "var", "count" -> "count"
static std::string extractIdentifierFromWord(const std::string &word) {
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

PatternType patternTypeFromPrefix(const std::string &prefix) {
  if (prefix == "effect")
    return PatternType::Effect;
  if (prefix == "expression")
    return PatternType::Expression;
  if (prefix == "section")
    return PatternType::Section;
  if (prefix == "condition")
    return PatternType::Expression; // Conditions are expressions
  // Default to Effect if unknown
  return PatternType::Effect;
}

std::string patternTypeToString(PatternType type) {
  switch (type) {
  case PatternType::Effect:
    return "effect";
  case PatternType::Expression:
    return "expression";
  case PatternType::Section:
    return "section";
  }
  return "unknown";
}

// Expand [option1|option2] alternatives into multiple pattern strings
std::vector<std::string> expandAlternatives(const std::string &patternText) {
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

// ============================================================================
// SectionPatternResolver Core Implementation
// ============================================================================

SectionPatternResolver::SectionPatternResolver() = default;

bool SectionPatternResolver::resolve(Section *root) {
  if (!root) {
    diagnosticsData.emplace_back("Cannot resolve null section");
    return false;
  }

  // Clear previous state
  patternDefinitionsData.clear();
  patternMatchesData.clear();
  allLinesData.clear();
  allSectionsData.clear();
  lineToPatternData.clear();
  lineToMatchData.clear();
  diagnosticsData.clear();

  // Collect all code lines and sections
  collectCodeLines(root, allLinesData);
  collectSections(root, allSectionsData);

  // Extract all pattern definitions
  for (CodeLine *line : allLinesData) {
    if (line->isPatternDefinition) {
      auto patterns = extractPatternDefinitions(line);
      for (auto &pattern : patterns) {
        // If this line already has a pattern, it means it's a multi-pattern
        // line The first one is kept in lineToPatternData for backward
        // compatibility if needed, but they all go into patternDefinitionsData
        if (lineToPatternData.find(line) == lineToPatternData.end()) {
          lineToPatternData[line] = pattern.get();
        }
        patternDefinitionsData.push_back(std::move(pattern));
      }
    }
  }

  // Phase 1: Resolve definition lines and other obvious lines
  for (CodeLine *line : allLinesData) {
    if (line->isPatternDefinition) {
      line->isResolved = true;
    }

    // Also resolve lines that are obviously markers or directives
    if (isSingleWordWithSection(line) || isPatternBodyDirective(line->text)) {
      line->isResolved = true;
    }
  }

  // Run the resolution algorithm iteratively
  int maxIterations = 100; // Prevent infinite loops
  int iteration = 0;

  // Phase 1: Resolve single-word pattern definitions
  resolveSingleWordPatterns();

  // Iterate Phases 2, 3, and 4 until convergence
  while (true) {
    bool progress = false;

    // Phase 2: Match pattern references to definitions
    if (resolvePatternReferences()) {
      progress = true;
    }

    // Phase 3: Resolve sections when all lines are resolved
    if (resolveSections()) {
      progress = true;
    }

    // Phase 4: Propagate variables from resolved pattern calls
    // When a body line matches a pattern, the arguments become variables
    if (propagateVariablesFromCalls()) {
      progress = true;
    }

    // Check if all patterns are resolved
    bool allResolved = true;
    for (CodeLine *line : allLinesData) {
      if (!line->isResolved) {
        allResolved = false;
        break;
      }
    }
    iteration++;
    if (iteration == maxIterations || !progress) {
      diagnosticsData.emplace_back("Cannot resolve all patterns");
      break;
    }

    if (allResolved) {
      break;
    }
  }

  // Check for unresolved patterns
  for (CodeLine *line : allLinesData) {
    if (!line->isResolved) {
      std::string filePath = line->filePath;
      int lineNum = line->lineNumber;

      diagnosticsData.emplace_back("Unresolved pattern: " + line->text,
                                   filePath, lineNum, line->startColumn,
                                   lineNum, line->endColumn);
    }
  }

  return diagnosticsData.empty();
}

void SectionPatternResolver::collectCodeLines(Section *section,
                                              std::vector<CodeLine *> &lines) {
  if (!section)
    return;

  for (auto &line : section->lines) {
    lines.push_back(&line);
    if (line.childSection) {
      collectCodeLines(line.childSection.get(), lines);
    }
  }
}

void SectionPatternResolver::collectSections(Section *section,
                                             std::vector<Section *> &sections) {
  if (!section)
    return;

  sections.push_back(section);
  for (auto &line : section->lines) {
    if (line.childSection) {
      collectSections(line.childSection.get(), sections);
    }
  }
}

void SectionPatternResolver::resolveSingleWordPatterns() {
  // Phase 1: Resolve single-word pattern definitions
  // These are patterns like "execute" or "get" that have no variables

  // First, resolve all pattern definitions from the patternDefinitionsData list
  // A pattern definition is considered resolved if it is well-formed
  for (auto &pattern : patternDefinitionsData) {
    pattern->sourceLine->isResolved = true;

    // Single-word patterns resolve immediately, and their bodies too
    if (pattern->isSingleWord()) {
      if (pattern->body) {
        pattern->body->isResolved = true;
      }
    }
  }

  // Also resolve single-word section definitions (like "execute:" or "get:")
  // These are not pattern definitions but they are single words with child
  // sections
  for (CodeLine *line : allLinesData) {
    if (line->isResolved) {
      continue;
    }

    // Check if this is a single-word section definition (like "execute:")
    if (isSingleWordWithSection(line)) {
      line->isResolved = true;
      if (line->childSection) {
        // Don't automatically resolve the child section here
        // It will be resolved when all its lines are resolved
      }
    }

    // Also resolve intrinsic calls immediately
    // @intrinsic(...) and return @intrinsic(...) are special syntax
    if (isIntrinsicCall(line->text)) {
      line->isResolved = true;
    }

    // Resolve pattern body directives (priority:, execute:, get:, etc.)
    // These are special sections within pattern bodies that don't need matching
    if (isPatternBodyDirective(line->text)) {
      line->isResolved = true;
    }

    // Resolve lines inside "patterns:" sections
    // These are syntax alternatives (like "val1 != val2") and should
    // auto-resolve
    if (isInsidePatternsSection(line)) {
      line->isResolved = true;
    }
  }
}

bool SectionPatternResolver::resolvePatternReferences() {
  bool progress = false;

  // Rebuild pattern trees with currently resolved patterns
  buildPatternTrees();

  for (CodeLine *line : allLinesData) {
    // Skip already resolved lines
    if (line->isResolved) {
      continue;
    }

    // Skip pattern definitions (they resolve differently)
    if (line->isPatternDefinition) {
      continue;
    }

    // Try to match this reference using tree-based matching
    PatternMatch *match = matchWithTree(line);

    if (match) {
      line->isResolved = true;
      progress = true;

      // Add arguments to the parent section's resolved variables
      Section *parent = nullptr;
      for (Section *section : allSectionsData) {
        for (auto &secLine : section->lines) {
          if (&secLine == line) {
            parent = section;
            break;
          }
        }
        if (parent)
          break;
      }

      if (parent) {
        for (const auto &[name, info] : match->arguments) {
          parent->resolvedVariables[name] = info.value;
        }
      }
    }
  }

  return progress;
}

bool SectionPatternResolver::resolveSections() {
  bool progress = false;

  for (Section *section : allSectionsData) {
    // Skip already resolved sections
    if (section->isResolved) {
      continue;
    }

    // Check if all lines in this section are resolved
    if (section->allLinesResolved()) {
      section->isResolved = true;
      progress = true;

      // Find any pattern definition that has this section as its body
      // and mark it as resolved
      for (auto &pattern : patternDefinitionsData) {
        if (pattern->body == section && !pattern->sourceLine->isResolved) {
          pattern->sourceLine->isResolved = true;
        }
      }
    }
  }

  return progress;
}

bool SectionPatternResolver::propagateVariablesFromCalls() {
  bool progress = false;

  // For each pattern definition, look at its body's resolved pattern calls
  for (auto &pattern : patternDefinitionsData) {
    if (!pattern->body)
      continue;

    // Get the original words from this pattern's text
    std::vector<std::string> originalWords =
        parsePatternWords(pattern->originalText);

    // Collect new variables found from pattern calls
    std::vector<std::string> newVariables;

    // Check each line in the body
    for (const auto &line : pattern->body->lines) {
      // Skip section headers like "execute:", "get:"
      std::string lineText = line.text;
      size_t start = lineText.find_first_not_of(" \t");
      if (start == std::string::npos)
        continue;
      lineText = lineText.substr(start);

      // Skip if this is a section header
      if (!lineText.empty() && lineText.back() == ':') {
        std::string header = lineText.substr(0, lineText.size() - 1);
        if (header == "execute" || header == "get" || header == "check" ||
            header == "patterns" || header == "priority") {
          // Recurse into child section
          if (line.childSection) {
            for (const auto &childLine : line.childSection->lines) {
              // Find if this line has a pattern match
              auto it =
                  lineToMatchData.find(const_cast<CodeLine *>(&childLine));
              if (it != lineToMatchData.end()) {
                PatternMatch *match = it->second;
                // Check each argument
                for (const auto &[varName, info] : match->arguments) {
                  // If the value is a string, check if it's one of our pattern
                  // words
                  if (std::holds_alternative<std::string>(info.value)) {
                    const std::string &argStr =
                        std::get<std::string>(info.value);
                    // Check if any pattern word's identifier appears in this
                    // argument
                    for (const auto &word : originalWords) {
                      std::string identifier = extractIdentifierFromWord(word);
                      if (identifier.empty())
                        continue;
                      if (containsIdentifier(argStr, identifier)) {
                        // This identifier is used as an argument to a pattern
                        // call So it should be a variable
                        bool alreadyVar = false;
                        for (const auto &v : pattern->variables) {
                          if (v == identifier) {
                            alreadyVar = true;
                            break;
                          }
                        }
                        if (!alreadyVar) {
                          bool alreadyNew = false;
                          for (const auto &v : newVariables) {
                            if (v == identifier) {
                              alreadyNew = true;
                              break;
                            }
                          }
                          if (!alreadyNew) {
                            newVariables.push_back(identifier);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          continue;
        }
      }

      // Direct line (not inside a subsection)
      auto it = lineToMatchData.find(const_cast<CodeLine *>(&line));
      if (it != lineToMatchData.end()) {
        PatternMatch *match = it->second;
        for (const auto &[varName, info] : match->arguments) {
          if (std::holds_alternative<std::string>(info.value)) {
            const std::string &argStr = std::get<std::string>(info.value);
            // Check if any pattern word's identifier appears in this argument
            for (const auto &word : originalWords) {
              std::string identifier = extractIdentifierFromWord(word);
              if (identifier.empty())
                continue;
              if (containsIdentifier(argStr, identifier)) {
                bool alreadyVar = false;
                for (const auto &v : pattern->variables) {
                  if (v == identifier) {
                    alreadyVar = true;
                    break;
                  }
                }
                if (!alreadyVar) {
                  bool alreadyNew = false;
                  for (const auto &v : newVariables) {
                    if (v == identifier) {
                      alreadyNew = true;
                      break;
                    }
                  }
                  if (!alreadyNew) {
                    newVariables.push_back(identifier);
                  }
                }
              }
            }
          }
        }
      }
    }

    // If we found new variables, update the pattern
    if (!newVariables.empty()) {
      for (const auto &var : newVariables) {
        pattern->variables.push_back(var);
      }
      // Rebuild the pattern string with the new variables
      pattern->pattern = createPatternString(originalWords, pattern->variables);
      progress = true;
    }
  }

  return progress;
}

void SectionPatternResolver::printResults() const {
  std::cout << "Pattern Definitions:\n";
  for (const auto &pattern : patternDefinitionsData) {
    pattern->print(2);
    std::cout << "\n";
  }

  std::cout << "Pattern References:\n";
  for (const auto &match : patternMatchesData) {
    // Find the source line for this match
    for (const auto &[line, m] : lineToMatchData) {
      if (m == match.get()) {
        std::cout << "  - \"" << line->getPatternText() << "\"\n";
        match->print(6);
        std::cout << "\n";
        break;
      }
    }
  }
}

} // namespace tbx
