#include "compiler/sectionAnalyzer.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace tbx {

// ============================================================================
// CodeLine Implementation
// ============================================================================

CodeLine::CodeLine(const std::string &lineText, int lineNum, std::string file,
                   int startCol, int endCol)
    : text(lineText), lineNumber(lineNum), filePath(file),
      startColumn(startCol), endColumn(endCol) {

  std::string textToCheck = text;

  // Check for "private " prefix
  if (textToCheck.size() >= 8 && textToCheck.substr(0, 8) == "private ") {
    isPrivate = true;
    textToCheck = textToCheck.substr(8);
  }

  // Pattern definitions start with "section ", "effect ", "expression ", or
  // "condition "
  const std::vector<std::pair<std::string, PatternType>> patternPrefixes = {
      {"section ", PatternType::Section},
      {"effect ", PatternType::Effect},
      {"expression ", PatternType::Expression},
      {"condition ", PatternType::Expression}};

  // Also check for bare type keywords with just a colon (e.g., "expression:")
  const std::vector<std::pair<std::string, PatternType>> bareKeywords = {
      {"section:", PatternType::Section},
      {"effect:", PatternType::Effect},
      {"expression:", PatternType::Expression},
      {"condition:", PatternType::Expression}};

  for (const auto &[prefix, pType] : patternPrefixes) {
    if (textToCheck.size() >= prefix.size() &&
        textToCheck.substr(0, prefix.size()) == prefix) {
      isPatternDefinition = true;
      type = pType;
      break;
    }
  }

  // Check for bare keywords (like "expression:")
  if (!isPatternDefinition) {
    for (const auto &[keyword, pType] : bareKeywords) {
      if (textToCheck == keyword) {
        isPatternDefinition = true;
        type = pType;
        break;
      }
    }
  }
}

std::string CodeLine::getPatternText() const {
  std::string result = text;

  // Skip "private " if present
  if (isPrivate && result.size() >= 8 && result.substr(0, 8) == "private ") {
    result = result.substr(8);
  }

  // Skip type prefix
  const std::vector<std::string> prefixes = {"section ", "effect ",
                                             "expression ", "condition "};
  for (const auto &prefix : prefixes) {
    if (result.size() >= prefix.size() &&
        result.substr(0, prefix.size()) == prefix) {
      result = result.substr(prefix.size());
      break;
    }
  }

  // Remove trailing ":" if present
  if (!result.empty() && result.back() == ':') {
    result = result.substr(0, result.size() - 1);
  }

  return result;
}

std::unique_ptr<CodeLine> CodeLine::clone() const {
  auto result = std::make_unique<CodeLine>();
  result->text = text;
  result->isPatternDefinition = isPatternDefinition;
  result->isPrivate = isPrivate;
  result->type = type;
  result->isResolved = isResolved;
  result->lineNumber = lineNumber;
  result->filePath = filePath;
  result->startColumn = startColumn;
  result->endColumn = endColumn;
  // Note: childSection is NOT cloned as it's owned by the original
  return result;
}

// ============================================================================
// Section Implementation
// ============================================================================

void Section::addLine(CodeLine line) { lines.push_back(std::move(line)); }

bool Section::allLinesResolved() const {
  for (const auto &line : lines) {
    if (!line.isResolved) {
      return false;
    }
  }
  return true;
}

void Section::print(int depth) const {
  std::string indent(depth * 2, ' ');

  for (const auto &line : lines) {
    std::cout << indent << "CodeLine: \"" << line.text << "\"";

    if (line.isPatternDefinition) {
      std::cout << " [pattern definition]";
    } else {
      std::cout << " [pattern reference]";
    }

    if (line.isResolved) {
      std::cout << " (resolved)";
    }

    std::cout << "\n";

    if (line.childSection) {
      std::cout << indent << "  Section:\n";
      line.childSection->print(depth + 2);
    }
  }
}

// ============================================================================
// SectionAnalyzer Implementation
// ============================================================================

std::string SectionAnalyzer::trim(const std::string &str) {
  size_t start = 0;
  while (start < str.size() && (str[start] == ' ' || str[start] == '\t')) {
    start++;
  }

  if (start == str.size()) {
    return "";
  }

  size_t end = str.size();

  // Find if there is a comment in the line and strip it
  size_t commentPos = str.find('#');
  if (commentPos != std::string::npos) {
    end = commentPos;
  }

  while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' ||
                         str[end - 1] == '\r' || str[end - 1] == '\n')) {
    end--;
  }

  if (end <= start)
    return "";

  return str.substr(start, end - start);
}

int SectionAnalyzer::calculateIndent(const std::string &line, int &startCol) {
  int indent = 0;
  startCol = 0;
  for (char c : line) {
    if (c == ' ') {
      indent++;
      startCol++;
    } else if (c == '\t') {
      // Treat tabs as 4 spaces
      indent += 4;
      startCol++;
    } else {
      break;
    }
  }
  return indent;
}

bool SectionAnalyzer::isComment(const std::string &line) {
  std::string trimmedText = trim(line);
  return !trimmedText.empty() && trimmedText[0] == '#';
}

bool SectionAnalyzer::isPatternDefinition(const std::string &line) {
  std::string textToCheck = line;
  if (textToCheck.size() >= 8 && textToCheck.substr(0, 8) == "private ") {
    textToCheck = textToCheck.substr(8);
  }

  const std::vector<std::string> patternPrefixes = {
      "section ", "effect ", "expression ", "condition "};

  for (const auto &prefix : patternPrefixes) {
    if (textToCheck.size() >= prefix.size() &&
        textToCheck.substr(0, prefix.size()) == prefix) {
      return true;
    }
  }
  return false;
}

bool SectionAnalyzer::endsWithColon(const std::string &line) {
  std::string trimmedText = trim(line);
  return !trimmedText.empty() && trimmedText.back() == ':';
}

std::vector<SectionAnalyzer::SourceLine>
SectionAnalyzer::splitLines(const std::string &source,
                            const std::map<int, SourceLocation> &sourceMap) {
  std::vector<SourceLine> lines;
  std::istringstream stream(source);
  std::string line;
  int currentLineNumber = 0;

  while (std::getline(stream, line)) {
    currentLineNumber++;

    SourceLine sl;
    sl.lineNumber = currentLineNumber;
    int startCol = 0;
    sl.indentLevel = calculateIndent(line, startCol);
    sl.text = trim(line);
    sl.isEmpty = sl.text.empty() || isComment(sl.text);

    if (!sl.isEmpty) {
      sl.startColumn = startCol;
      // Calculate where the text ends in the line, but before comments if any.
      // sl.text is already trimmed (no leading spaces, no comments, no trailing
      // whitespace)
      sl.endColumn = startCol + sl.text.length();
    } else {
      sl.startColumn = 0;
      sl.endColumn = 0;
    }

    // Look up original source location
    auto it = sourceMap.find(currentLineNumber);
    if (it != sourceMap.end()) {
      sl.filePath = it->second.filePath;
      sl.originalLine = it->second.lineNumber;
    } else {
      sl.filePath = "";
      sl.originalLine = currentLineNumber;
    }

    lines.push_back(sl);
  }

  return lines;
}

void SectionAnalyzer::buildSection(Section &section,
                                   const std::vector<SourceLine> &lines,
                                   size_t &index, int parentIndent) {
  // Track the indent level of this section (set when we see the first line)
  int sectionIndent = -1;

  while (index < lines.size()) {
    const SourceLine &line = lines[index];

    // Skip empty lines and comments
    if (line.isEmpty) {
      index++;
      continue;
    }

    // If indent is less than or equal to parent, we're done with this section
    if (line.indentLevel <= parentIndent) {
      return;
    }

    // If this is the first line of the section, establish the section's indent
    // level
    if (sectionIndent < 0) {
      sectionIndent = line.indentLevel;
      section.indentLevel = sectionIndent;
    }

    // If indent is less than section indent (but more than parent), it's an
    // error
    if (line.indentLevel < sectionIndent) {
      diagnosticsData.emplace_back(
          "Inconsistent indentation: expected indent " +
              std::to_string(sectionIndent) + " but got " +
              std::to_string(line.indentLevel),
          line.filePath, line.originalLine);
      // Try to recover by treating it as end of section
      return;
    }

    // If indent is greater than section indent, this line is unexpectedly
    // indented (should only happen if previous line didn't end with ":")
    if (line.indentLevel > sectionIndent) {
      // This shouldn't happen if code is well-formed, but handle it gracefully
      // by treating it as belonging to the previous line's child section
      if (!section.lines.empty()) {
        CodeLine &prevLine = section.lines.back();
        if (!prevLine.childSection) {
          prevLine.childSection = std::make_unique<Section>();
          prevLine.childSection->parent = &section;
        }
        buildSection(*prevLine.childSection, lines, index, sectionIndent);
        continue;
      }
    }

    // This line is at the section's indent level - it's a sibling line
    // Create code line for this source line
    CodeLine codeLine(line.text, line.originalLine, line.filePath,
                      line.startColumn, line.endColumn);

    // Check if this line ends with ":" - it will have a child section
    bool hasChild = endsWithColon(line.text);

    index++;

    // If line ends with ":", look ahead for child section
    if (hasChild && index < lines.size()) {
      // Find next non-empty line
      size_t nextIndex = index;
      while (nextIndex < lines.size() && lines[nextIndex].isEmpty) {
        nextIndex++;
      }

      // If next line has greater indent, it's a child section
      if (nextIndex < lines.size() &&
          lines[nextIndex].indentLevel > line.indentLevel) {
        codeLine.childSection = std::make_unique<Section>();
        codeLine.childSection->parent = &section;

        // Adjust endColumn to exclude the trailing colon for the pattern itself
        // if the pattern line has a child section.
        if (codeLine.endColumn > codeLine.startColumn &&
            codeLine.text.back() == ':') {
          codeLine.endColumn--;
        }

        // Skip empty lines
        index = nextIndex;

        // Build the child section
        buildSection(*codeLine.childSection, lines, index, line.indentLevel);
      }
    }

    section.addLine(std::move(codeLine));
  }
}

std::unique_ptr<Section>
SectionAnalyzer::analyze(const std::string &source,
                         const std::map<int, SourceLocation> &sourceMap) {
  diagnosticsData.clear();

  // Split source into lines with indent information
  std::vector<SourceLine> lines = splitLines(source, sourceMap);

  // Create root section
  auto root = std::make_unique<Section>();
  root->indentLevel = -1; // Root is at "virtual" indent -1

  // Build the section tree
  size_t index = 0;
  buildSection(*root, lines, index, -1);

  return root;
}

} // namespace tbx
