#include "compiler/typeInference.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>

namespace tbx {

// ============================================================================
// Helper functions
// ============================================================================

std::string typeToString(InferredType type) {
  switch (type) {
  case InferredType::Void:
    return "void";
  case InferredType::I1:
    return "i1";
  case InferredType::I64:
    return "i64";
  case InferredType::F64:
    return "f64";
  case InferredType::String:
    return "i8*";
  case InferredType::Unknown:
    return "unknown";
  }
  return "unknown";
}

// ============================================================================
// TypedValue Implementation
// ============================================================================

TypedValue TypedValue::fromInt(int64_t val) {
  TypedValue tv;
  tv.type = InferredType::I64;
  tv.value = val;
  tv.isLiteral = true;
  return tv;
}

TypedValue TypedValue::fromDouble(double val) {
  TypedValue tv;
  tv.type = InferredType::F64;
  tv.value = val;
  tv.isLiteral = true;
  return tv;
}

TypedValue TypedValue::fromString(const std::string &val) {
  TypedValue tv;
  tv.type = InferredType::String;
  tv.value = val;
  tv.isLiteral = true;
  return tv;
}

TypedValue TypedValue::fromVariable(const std::string &name,
                                    InferredType type) {
  TypedValue tv;
  tv.type = type;
  tv.variableName = name;
  tv.isLiteral = false;
  return tv;
}

void TypedValue::print(int indent) const {
  std::string pad(indent, ' ');
  if (isLiteral) {
    std::cout << pad << typeToString(type) << " = ";
    if (std::holds_alternative<int64_t>(value)) {
      std::cout << std::get<int64_t>(value);
    } else if (std::holds_alternative<double>(value)) {
      std::cout << std::get<double>(value);
    } else if (std::holds_alternative<std::string>(value)) {
      std::cout << "\"" << std::get<std::string>(value) << "\"";
    }
  } else {
    std::cout << pad << variableName << ": " << typeToString(type);
  }
}

// ============================================================================
// TypedParameter Implementation
// ============================================================================

void TypedParameter::print(int indent) const {
  std::string pad(indent, ' ');
  std::cout << pad << name << ": " << typeToString(type);
}

// ============================================================================
// TypedPattern Implementation
// ============================================================================

void TypedPattern::print(int indent) const {
  std::string pad(indent, ' ');
  std::cout << pad << patternTypeToString(pattern->type) << " \""
            << pattern->pattern << "\":\n";

  for (const auto &[name, type] : parameterTypes) {
    std::cout << pad << "    " << name << ": " << typeToString(type) << "\n";
  }

  std::cout << pad << "    returns: " << typeToString(returnType) << "\n";

  if (pattern->body && !pattern->body->lines.empty()) {
    std::cout << pad << "    body:";
    for (const auto &line : pattern->body->lines) {
      std::cout << " " << line.text;
    }
    std::cout << "\n";
  }
}

// ============================================================================
// TypedCall Implementation
// ============================================================================

void TypedCall::print(int indent) const {
  std::string pad(indent, ' ');

  for (const auto &[name, typedVal] : typedArguments) {
    std::cout << pad << name << ": " << typeToString(typedVal.type);
    if (typedVal.isLiteral) {
      std::cout << " = ";
      if (std::holds_alternative<int64_t>(typedVal.value)) {
        std::cout << std::get<int64_t>(typedVal.value);
      } else if (std::holds_alternative<double>(typedVal.value)) {
        std::cout << std::get<double>(typedVal.value);
      } else if (std::holds_alternative<std::string>(typedVal.value)) {
        std::cout << "\"" << std::get<std::string>(typedVal.value) << "\"";
      }
    }
    std::cout << "\n";
  }

  std::cout << pad << "result: " << typeToString(resultType) << "\n";
}

// ============================================================================
// IntrinsicInfo Implementation
// ============================================================================

InferredType IntrinsicInfo::getReturnType(
    const std::map<std::string, InferredType> &argTypes) const {
  // Arithmetic intrinsics return the same type as their operands
  if (name == "add" || name == "sub" || name == "mul" || name == "div" ||
      name == "mod") {
    // Find the type of the first numeric argument
    for (const auto &arg : arguments) {
      auto it = argTypes.find(arg);
      if (it != argTypes.end()) {
        if (it->second == InferredType::I64 ||
            it->second == InferredType::F64) {
          return it->second;
        }
      }
    }
    // Default to i64 for arithmetic
    return InferredType::I64;
  }

  // Comparison intrinsics return boolean
  if (name == "cmp_eq" || name == "cmp_ne" || name == "cmp_lt" ||
      name == "cmp_gt" || name == "cmp_le" || name == "cmp_ge" ||
      name == "eq" || name == "ne" || name == "lt" || name == "gt" ||
      name == "le" || name == "ge") {
    return InferredType::I1;
  }

  // Print returns void
  if (name == "print") {
    return InferredType::Void;
  }

  // Store returns void
  if (name == "store") {
    return InferredType::Void;
  }

  // Load returns the type of the variable
  if (name == "load") {
    if (!arguments.empty()) {
      auto it = argTypes.find(arguments[0]);
      if (it != argTypes.end()) {
        return it->second;
      }
    }
    // Default to i64 for load
    return InferredType::I64;
  }

  // Return passes through the type of its argument
  if (name == "return") {
    if (!arguments.empty()) {
      auto it = argTypes.find(arguments[0]);
      if (it != argTypes.end()) {
        return it->second;
      }
    }
    return InferredType::Unknown;
  }

  // Unknown intrinsic
  return InferredType::Unknown;
}

InferredType IntrinsicInfo::getArgumentType(
    size_t index, const std::map<std::string, InferredType> &knownTypes) const {
  // Arithmetic intrinsics expect numeric types
  if (name == "add" || name == "sub" || name == "mul" || name == "div" ||
      name == "mod") {
    // Check if any argument has a known type, use that
    for (const auto &arg : arguments) {
      auto it = knownTypes.find(arg);
      if (it != knownTypes.end() && (it->second == InferredType::I64 ||
                                     it->second == InferredType::F64)) {
        return it->second;
      }
    }
    // Default to i64
    return InferredType::I64;
  }

  // Comparison intrinsics expect numeric types
  if (name == "cmp_eq" || name == "cmp_ne" || name == "cmp_lt" ||
      name == "cmp_gt" || name == "cmp_le" || name == "cmp_ge" ||
      name == "eq" || name == "ne" || name == "lt" || name == "gt" ||
      name == "le" || name == "ge") {
    return InferredType::I64;
  }

  // Print can take any type
  if (name == "print") {
    return InferredType::Unknown; // Accepts any type
  }

  // Store: first arg is variable name, second is value
  if (name == "store") {
    if (index == 1 && arguments.size() > 1) {
      // The value being stored
      auto it = knownTypes.find(arguments[1]);
      if (it != knownTypes.end()) {
        return it->second;
      }
    }
    return InferredType::Unknown;
  }

  // Load: expects a variable name
  if (name == "load") {
    return InferredType::Unknown;
  }

  // Return: expects a value
  if (name == "return") {
    if (!arguments.empty()) {
      auto it = knownTypes.find(arguments[0]);
      if (it != knownTypes.end()) {
        return it->second;
      }
    }
    return InferredType::Unknown;
  }

  return InferredType::Unknown;
}

// ============================================================================
// TypeInference Implementation
// ============================================================================

TypeInference::TypeInference() = default;

bool TypeInference::infer(const SectionPatternResolver &resolver) {
  // Clear previous state
  typedPatternsData.clear();
  typedCallsData.clear();
  patternToTypedData.clear();
  diagnosticsData.clear();

  // Phase 1: Infer types for all pattern definitions
  for (const auto &pattern : resolver.patternDefinitions()) {
    auto typed = inferPatternTypes(pattern.get());
    if (typed) {
      patternToTypedData[pattern.get()] = typed.get();
      typedPatternsData.push_back(std::move(typed));
    }
  }

  // Phase 2: Infer types for all pattern calls
  for (const auto &match : resolver.patternMatches()) {
    auto typed = inferCallTypes(match.get());
    if (typed) {
      typedCallsData.push_back(std::move(typed));
    }
  }

  // Check for unresolved types
  for (const auto &typedPattern : typedPatternsData) {
    for (const auto &[name, type] : typedPattern->parameterTypes) {
      if (type == InferredType::Unknown) {
        // Get pattern definition location
        std::string filePath = typedPattern->pattern->sourceLine->filePath;
        int lineNum = typedPattern->pattern->sourceLine->lineNumber;

        diagnosticsData.emplace_back(
            "Could not infer type for parameter '" + name + "' in pattern \"" +
                typedPattern->pattern->pattern + "\"",
            filePath, lineNum, 0, DiagnosticSeverity::Warning);
      }
    }
    if (typedPattern->returnType == InferredType::Unknown &&
        typedPattern->pattern->type == PatternType::Expression) {
      // Get pattern definition location
      std::string filePath = typedPattern->pattern->sourceLine->filePath;
      int lineNum = typedPattern->pattern->sourceLine->lineNumber;

      diagnosticsData.emplace_back(
          "Could not infer return type for expression \"" +
              typedPattern->pattern->pattern + "\"",
          filePath, lineNum, 0, DiagnosticSeverity::Warning);
    }
  }

  bool hasError = false;
  for (const auto &diag : diagnosticsData) {
    if (diag.severity == DiagnosticSeverity::Error) {
      hasError = true;
      break;
    }
  }
  return !hasError;
}

std::unique_ptr<TypedPattern>
TypeInference::inferPatternTypes(ResolvedPattern *pattern) {
  if (!pattern) {
    return nullptr;
  }

  auto typed = std::make_unique<TypedPattern>();
  typed->pattern = pattern;

  // Initialize all parameters as Unknown
  for (const auto &var : pattern->variables) {
    typed->parameterTypes[var] = InferredType::Unknown;
  }

  // Analyze the body section for intrinsic calls
  if (pattern->body) {
    for (const auto &line : pattern->body->lines) {
      auto intrinsics = parseIntrinsics(line.text);

      for (const auto &intrinsic : intrinsics) {
        typed->bodyIntrinsics.push_back(intrinsic.name);

        // Infer argument types from intrinsic usage
        for (size_t argIndex = 0; argIndex < intrinsic.arguments.size();
             argIndex++) {
          const std::string &arg = intrinsic.arguments[argIndex];

          // Check if this argument is a pattern variable
          auto it = typed->parameterTypes.find(arg);
          if (it != typed->parameterTypes.end()) {
            // Get expected type from intrinsic
            InferredType expectedType =
                intrinsic.getArgumentType(argIndex, typed->parameterTypes);

            // Update the parameter type if we can infer it
            if (expectedType != InferredType::Unknown) {
              if (it->second == InferredType::Unknown) {
                it->second = expectedType;
              } else if (it->second != expectedType) {
                // Type conflict - try to unify
                InferredType unified = unifyTypes(it->second, expectedType);
                if (unified == InferredType::Unknown) {
                  // Get location from pattern definition
                  std::string filePath = pattern->sourceLine->filePath;
                  int lineNum = pattern->sourceLine->lineNumber;

                  diagnosticsData.emplace_back(
                      "Type conflict for parameter '" + arg +
                          "' in pattern \"" + pattern->pattern +
                          "\": " + "expected " + typeToString(expectedType) +
                          " but previously inferred " +
                          typeToString(it->second),
                      filePath, lineNum);
                } else {
                  it->second = unified;
                }
              }
            }
          }
        }

        // Infer return type if this is a return statement
        if (intrinsic.hasReturn) {
          InferredType retType = intrinsic.getReturnType(typed->parameterTypes);
          if (retType != InferredType::Unknown) {
            if (typed->returnType == InferredType::Unknown) {
              typed->returnType = retType;
            } else if (typed->returnType != retType) {
              // Multiple return types - try to unify
              InferredType unified = unifyTypes(typed->returnType, retType);
              if (unified == InferredType::Unknown) {
                // Get location from pattern definition
                std::string filePath = pattern->sourceLine->filePath;
                int lineNum = pattern->sourceLine->lineNumber;

                diagnosticsData.emplace_back(
                    "Multiple return types in pattern \"" + pattern->pattern +
                        "\": " + typeToString(typed->returnType) + " and " +
                        typeToString(retType),
                    filePath, lineNum);
              } else {
                typed->returnType = unified;
              }
            }
          }
        }
      }

      // Also check child sections (for nested "execute:", "get:", etc.)
      if (line.childSection) {
        for (const auto &childLine : line.childSection->lines) {
          auto childIntrinsics = parseIntrinsics(childLine.text);

          for (const auto &intrinsic : childIntrinsics) {
            typed->bodyIntrinsics.push_back(intrinsic.name);

            // Infer argument types
            for (size_t argIndex = 0; argIndex < intrinsic.arguments.size();
                 argIndex++) {
              const std::string &arg = intrinsic.arguments[argIndex];
              auto it = typed->parameterTypes.find(arg);
              if (it != typed->parameterTypes.end()) {
                InferredType expectedType =
                    intrinsic.getArgumentType(argIndex, typed->parameterTypes);
                if (expectedType != InferredType::Unknown &&
                    it->second == InferredType::Unknown) {
                  it->second = expectedType;
                }
              }
            }

            // Infer return type
            if (intrinsic.hasReturn) {
              InferredType retType =
                  intrinsic.getReturnType(typed->parameterTypes);
              if (retType != InferredType::Unknown &&
                  typed->returnType == InferredType::Unknown) {
                typed->returnType = retType;
              }
            }
          }
        }
      }
    }
  }

  // Set default return type based on pattern type
  if (typed->returnType == InferredType::Unknown) {
    if (pattern->type == PatternType::Effect) {
      typed->returnType = InferredType::Void;
    }
    // Expression and Section remain Unknown if not inferred
    // Boolean expressions will be typed based on their return value
  }

  return typed;
}

std::unique_ptr<TypedCall> TypeInference::inferCallTypes(PatternMatch *match) {
  if (!match || !match->pattern) {
    return nullptr;
  }

  auto typed = std::make_unique<TypedCall>();
  typed->match = match;

  // Get the typed pattern for this call
  TypedPattern *typedPattern = nullptr;
  auto it = patternToTypedData.find(match->pattern);
  if (it != patternToTypedData.end()) {
    typedPattern = it->second;
  }

  // Type each argument
  for (const auto &[name, info] : match->arguments) {
    TypedValue tv = resolvedToTyped(info.value, name);

    // If we have a typed pattern, check the expected type
    if (typedPattern) {
      auto paramIt = typedPattern->parameterTypes.find(name);
      if (paramIt != typedPattern->parameterTypes.end()) {
        InferredType expected = paramIt->second;

        // If the value type is unknown but expected is known, use expected
        if (tv.type == InferredType::Unknown &&
            expected != InferredType::Unknown) {
          tv.type = expected;
        }
        // If both are known, check compatibility
        else if (tv.type != InferredType::Unknown &&
                 expected != InferredType::Unknown) {
          if (!isCompatible(expected, tv.type)) {
            // Get location from match
            std::string filePath = match->pattern->sourceLine->filePath;
            int lineNum = match->pattern->sourceLine->lineNumber;

            // Try to find the specific line of the call if possible
            // The match object doesn't store the call site directly,
            // but we can pass it if we update TypedCall to store it

            diagnosticsData.emplace_back(
                "Type mismatch for argument '" + name + "': expected " +
                    typeToString(expected) + " but got " +
                    typeToString(tv.type),
                filePath, lineNum);
          }
        }
      }
    }

    typed->typedArguments[name] = tv;
  }

  // Set result type from typed pattern
  if (typedPattern) {
    typed->resultType = typedPattern->returnType;
  }

  return typed;
}

std::vector<IntrinsicInfo>
TypeInference::parseIntrinsics(const std::string &text) {
  std::vector<IntrinsicInfo> result;

  // Find all @intrinsic calls in the text
  size_t pos = 0;
  while ((pos = text.find("@intrinsic(", pos)) != std::string::npos) {
    // Check if preceded by "return"
    bool hasReturn = false;
    if (pos >= 7) {
      std::string prefix = text.substr(pos - 7, 7);
      // Trim leading whitespace
      size_t start = prefix.find_first_not_of(" \t");
      if (start != std::string::npos) {
        prefix = prefix.substr(start);
      }
      if (prefix.find("return") != std::string::npos) {
        hasReturn = true;
      }
    }

    IntrinsicInfo info = parseSingleIntrinsic(text, pos);
    info.hasReturn = hasReturn;
    result.push_back(info);

    pos++; // Move past current position to find next
  }

  return result;
}

IntrinsicInfo TypeInference::parseSingleIntrinsic(const std::string &text,
                                                  size_t startPos) {
  IntrinsicInfo info;

  // Skip "@intrinsic("
  size_t pos = startPos + 11; // Length of "@intrinsic("

  // Find the closing parenthesis, accounting for nested parens
  int parenDepth = 1;
  size_t endPos = pos;
  while (endPos < text.size() && parenDepth > 0) {
    if (text[endPos] == '(')
      parenDepth++;
    else if (text[endPos] == ')')
      parenDepth--;
    endPos++;
  }
  endPos--; // Back to the closing paren

  // Extract the content between parentheses
  std::string content = text.substr(pos, endPos - pos);

  // Parse the intrinsic name (first argument, should be a string)
  size_t nameStart = content.find('"');
  if (nameStart != std::string::npos) {
    size_t nameEnd = content.find('"', nameStart + 1);
    if (nameEnd != std::string::npos) {
      info.name = content.substr(nameStart + 1, nameEnd - nameStart - 1);

      // Parse the remaining arguments
      pos = nameEnd + 1;

      // Skip to comma or end
      while (pos < content.size()) {
        // Skip whitespace and commas
        while (pos < content.size() &&
               (content[pos] == ' ' || content[pos] == ',' ||
                content[pos] == '\t')) {
          pos++;
        }

        if (pos >= content.size())
          break;

        // Read the argument
        std::string arg;
        bool inString = false;
        char stringChar = '\0';

        while (pos < content.size()) {
          char character = content[pos];

          if (inString) {
            arg += character;
            if (character == stringChar) {
              inString = false;
            }
            pos++;
          } else if (character == '"' || character == '\'') {
            inString = true;
            stringChar = character;
            arg += character;
            pos++;
          } else if (character == ',' || character == ')') {
            break;
          } else if (character == ' ' || character == '\t') {
            pos++;
          } else {
            arg += character;
            pos++;
          }
        }

        if (!arg.empty()) {
          // Remove surrounding quotes if present
          if (arg.size() >= 2 && (arg.front() == '"' || arg.front() == '\'') &&
              arg.back() == arg.front()) {
            arg = arg.substr(1, arg.size() - 2);
          }
          info.arguments.push_back(arg);
        }
      }
    }
  }

  return info;
}

InferredType TypeInference::inferValueType(const ResolvedValue &value) {
  if (std::holds_alternative<int64_t>(value)) {
    return InferredType::I64;
  } else if (std::holds_alternative<double>(value)) {
    return InferredType::F64;
  } else if (std::holds_alternative<std::string>(value)) {
    const std::string &str = std::get<std::string>(value);

    // Check if it's a numeric string
    bool isNumber = true;
    bool hasDot = false;
    for (size_t charIndex = 0; charIndex < str.size(); charIndex++) {
      char character = str[charIndex];
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

    if (isNumber && !str.empty()) {
      return hasDot ? InferredType::F64 : InferredType::I64;
    }

    // Check if it's a quoted string
    if (str.size() >= 2 && (str.front() == '"' || str.front() == '\'') &&
        str.back() == str.front()) {
      return InferredType::String;
    }

    // Otherwise, it's likely an identifier/variable
    return InferredType::Unknown;
  } else if (std::holds_alternative<std::shared_ptr<Section>>(value)) {
    // Nested section - type depends on the section content
    return InferredType::Unknown;
  }

  return InferredType::Unknown;
}

TypedValue TypeInference::resolvedToTyped(const ResolvedValue &value,
                                          const std::string &varName) {
  TypedValue tv;
  tv.variableName = varName;

  if (std::holds_alternative<int64_t>(value)) {
    tv.type = InferredType::I64;
    tv.value = std::get<int64_t>(value);
    tv.isLiteral = true;
  } else if (std::holds_alternative<double>(value)) {
    tv.type = InferredType::F64;
    tv.value = std::get<double>(value);
    tv.isLiteral = true;
  } else if (std::holds_alternative<std::string>(value)) {
    const std::string &str = std::get<std::string>(value);

    // Check if it's a numeric string
    bool isNumber = true;
    bool hasDot = false;
    for (size_t charIndex = 0; charIndex < str.size(); charIndex++) {
      char character = str[charIndex];
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

    if (isNumber && !str.empty()) {
      if (hasDot) {
        tv.type = InferredType::F64;
        tv.value = std::stod(str);
      } else {
        tv.type = InferredType::I64;
        tv.value = std::stoll(str);
      }
      tv.isLiteral = true;
    } else if (str.size() >= 2 && (str.front() == '"' || str.front() == '\'') &&
               str.back() == str.front()) {
      // Quoted string
      tv.type = InferredType::String;
      tv.value = str.substr(1, str.size() - 2);
      tv.isLiteral = true;
    } else {
      // Variable reference or expression
      tv.type = InferredType::Unknown;
      tv.value = str;
      tv.isLiteral = false;
    }
  } else if (std::holds_alternative<std::shared_ptr<Section>>(value)) {
    tv.type = InferredType::Unknown;
    tv.isLiteral = false;
  }

  return tv;
}

bool TypeInference::isCompatible(InferredType expected,
                                 InferredType actual) const {
  if (expected == actual)
    return true;
  if (expected == InferredType::Unknown || actual == InferredType::Unknown)
    return true;

  // i64 and f64 are compatible in some contexts
  if ((expected == InferredType::I64 && actual == InferredType::F64) ||
      (expected == InferredType::F64 && actual == InferredType::I64)) {
    return true; // Allow implicit conversion
  }

  return false;
}

InferredType TypeInference::unifyTypes(InferredType t1, InferredType t2) const {
  if (t1 == t2)
    return t1;
  if (t1 == InferredType::Unknown)
    return t2;
  if (t2 == InferredType::Unknown)
    return t1;

  // Numeric types can be unified to the wider type
  if ((t1 == InferredType::I64 && t2 == InferredType::F64) ||
      (t1 == InferredType::F64 && t2 == InferredType::I64)) {
    return InferredType::F64; // Widen to f64
  }

  // Incompatible types
  return InferredType::Unknown;
}

void TypeInference::printResults() const {
  std::cout << "Typed Patterns:\n";
  for (const auto &typed : typedPatternsData) {
    typed->print(2);
    std::cout << "\n";
  }

  std::cout << "Typed Calls:\n";
  for (const auto &typed : typedCallsData) {
    std::cout << "  Call: \"";
    // Find the original text from the match
    if (typed->match && typed->match->pattern) {
      // Print a reconstructed call representation
      std::cout << typed->match->pattern->pattern;
    }
    std::cout << "\"\n";
    typed->print(4);
    std::cout << "\n";
  }

  bool hasWarnings = false;
  for (const auto &diag : diagnosticsData) {
    if (diag.severity == DiagnosticSeverity::Warning) {
      hasWarnings = true;
      break;
    }
  }

  if (hasWarnings) {
    std::cout << "Warnings:\n";
    for (const auto &diag : diagnosticsData) {
      if (diag.severity == DiagnosticSeverity::Warning) {
        std::cout << "  - " << diag.toString() << "\n";
      }
    }
  }

  bool hasErrors = false;
  for (const auto &diag : diagnosticsData) {
    if (diag.severity == DiagnosticSeverity::Error) {
      hasErrors = true;
      break;
    }
  }

  if (hasErrors) {
    std::cout << "Errors:\n";
    for (const auto &diag : diagnosticsData) {
      if (diag.severity == DiagnosticSeverity::Error) {
        std::cout << "  - " << diag.toString() << "\n";
      }
    }
  }
}

} // namespace tbx
