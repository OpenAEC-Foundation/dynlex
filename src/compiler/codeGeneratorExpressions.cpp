#include "compiler/codeGenerator.hpp"
#include "compiler/expressionMatch.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// Trim whitespace from both ends of a string (local helper)
static std::string trimExpr(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// ============================================================================
// Expression Generation Implementation
// ============================================================================

llvm::Value *SectionCodeGenerator::generateExpression(
    const std::string &arg,
    const std::unordered_map<std::string, llvm::Value *> &localVars) {
  std::string trimmed = trimExpr(arg);
  if (trimmed.empty())
    return nullptr;

  if (trimmed.find("@intrinsic(") != std::string::npos) {
    return generateIntrinsic(trimmed, localVars);
  }

  bool isInt = !trimmed.empty();
  for (size_t charIndex = 0; charIndex < trimmed.size(); charIndex++) {
    char character = trimmed[charIndex];
    if (character == '-' && charIndex == 0)
      continue;
    if (!std::isdigit(character)) {
      isInt = false;
      break;
    }
  }

  if (isInt) {
    int64_t val = std::stoll(trimmed);
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), val, true);
  }

  bool isFloat = !trimmed.empty();
  bool hasDot = false;
  for (size_t charIndex = 0; charIndex < trimmed.size(); charIndex++) {
    char character = trimmed[charIndex];
    if (character == '-' && charIndex == 0)
      continue;
    if (character == '.' && !hasDot) {
      hasDot = true;
      continue;
    }
    if (!std::isdigit(character)) {
      isFloat = false;
      break;
    }
  }

  if (isFloat && hasDot) {
    double val = std::stod(trimmed);
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), val);
  }

  if (trimmed.size() >= 2 &&
      (trimmed.front() == '"' || trimmed.front() == '\'')) {
    std::string strVal = trimmed.substr(1, trimmed.size() - 2);
    return builder->CreateGlobalStringPtr(strVal);
  }

  auto localIt = localVars.find(trimmed);
  if (localIt != localVars.end()) {
    return localIt->second;
  }

  auto namedIt = namedValues.find(trimmed);
  if (namedIt != namedValues.end()) {
    return builder->CreateLoad(namedIt->second->getAllocatedType(),
                               namedIt->second, trimmed);
  }

  // Try matching against expression patterns
  if (resolverRef) {
    auto exprMatch = resolverRef->getExpressionTree().matchExpression(trimmed);
    if (exprMatch && exprMatch->pattern) {
      // Find the corresponding codegen pattern
      auto codegenIt = patternToCodegen.find(exprMatch->pattern);
      if (codegenIt != patternToCodegen.end() &&
          codegenIt->second->llvmFunction) {
        CodegenPattern *codegenPattern = codegenIt->second;

        // Build arguments from the match
        std::vector<llvm::Value *> args;
        bool allArgsValid = true;

        for (size_t argIndex = 0; argIndex < exprMatch->arguments.size();
             argIndex++) {
          const auto &matchedArg = exprMatch->arguments[argIndex];
          llvm::Value *argVal = nullptr;

          // Convert MatchedValue to LLVM value
          if (std::holds_alternative<int64_t>(matchedArg)) {
            argVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                            std::get<int64_t>(matchedArg));
          } else if (std::holds_alternative<double>(matchedArg)) {
            argVal = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                           std::get<double>(matchedArg));
          } else if (std::holds_alternative<std::string>(matchedArg)) {
            // Recursively evaluate the string argument
            std::string argStr = std::get<std::string>(matchedArg);
            argVal = generateExpression(argStr, localVars);
          } else if (std::holds_alternative<std::shared_ptr<ExpressionMatch>>(
                         matchedArg)) {
            // Nested expression match - recursively evaluate
            auto nestedMatch =
                std::get<std::shared_ptr<ExpressionMatch>>(matchedArg);
            if (nestedMatch) {
              argVal = generateExpression(nestedMatch->matchedText, localVars);
            }
          }

          if (!argVal) {
            allArgsValid = false;
            break;
          }

          // Type conversion if needed
          llvm::Type *expectedType =
              codegenPattern->llvmFunction->getFunctionType()->getParamType(
                  argIndex);
          if (argVal->getType() != expectedType) {
            if (argVal->getType()->isIntegerTy() &&
                expectedType->isIntegerTy()) {
              if (argVal->getType()->getIntegerBitWidth() <
                  expectedType->getIntegerBitWidth()) {
                argVal = builder->CreateZExt(argVal, expectedType, "zext");
              } else {
                argVal = builder->CreateTrunc(argVal, expectedType, "trunc");
              }
            } else if (argVal->getType()->isDoubleTy() &&
                       expectedType->isIntegerTy()) {
              argVal = builder->CreateFPToSI(argVal, expectedType, "fptosi");
            } else if (argVal->getType()->isIntegerTy() &&
                       expectedType->isDoubleTy()) {
              argVal = builder->CreateSIToFP(argVal, expectedType, "sitofp");
            } else {
              allArgsValid = false;
              break;
            }
          }

          args.push_back(argVal);
        }

        if (allArgsValid &&
            args.size() == codegenPattern->llvmFunction->getFunctionType()
                               ->getNumParams()) {
          return builder->CreateCall(codegenPattern->llvmFunction, args);
        }
      }
    }
  }

  return nullptr;
}

std::string SectionCodeGenerator::extractArgument(const std::string &text,
                                                  size_t &pos) {
  if (pos >= text.size())
    return "";

  while (pos < text.size() && std::isspace(text[pos])) {
    pos++;
  }

  if (pos >= text.size())
    return "";

  size_t start = pos;

  if (text[pos] == '"' || text[pos] == '\'') {
    char quote = text[pos];
    pos++;
    while (pos < text.size() && text[pos] != quote) {
      pos++;
    }
    if (pos < text.size())
      pos++;
    return text.substr(start, pos - start);
  }

  if (text.substr(pos, 10) == "@intrinsic") {
    pos += 10;
    if (pos < text.size() && text[pos] == '(') {
      int depth = 1;
      pos++;
      while (pos < text.size() && depth > 0) {
        if (text[pos] == '(')
          depth++;
        else if (text[pos] == ')')
          depth--;
        pos++;
      }
      return text.substr(start, pos - start);
    }
  }

  while (pos < text.size() && !std::isspace(text[pos])) {
    pos++;
  }

  return text.substr(start, pos - start);
}

std::string
SectionCodeGenerator::extractArgumentUntil(const std::string &text, size_t &pos,
                                           const std::string &untilWord) {
  if (pos >= text.size())
    return "";

  while (pos < text.size() && std::isspace(text[pos])) {
    pos++;
  }

  if (pos >= text.size())
    return "";

  size_t start = pos;

  if (text[pos] == '"' || text[pos] == '\'') {
    char quote = text[pos];
    pos++;
    while (pos < text.size() && text[pos] != quote) {
      pos++;
    }
    if (pos < text.size())
      pos++;
    return text.substr(start, pos - start);
  }

  if (text.substr(pos, 10) == "@intrinsic") {
    pos += 10;
    if (pos < text.size() && text[pos] == '(') {
      int depth = 1;
      pos++;
      while (pos < text.size() && depth > 0) {
        if (text[pos] == '(')
          depth++;
        else if (text[pos] == ')')
          depth--;
        pos++;
      }
      return text.substr(start, pos - start);
    }
  }

  while (pos < text.size()) {
    size_t wsStart = pos;
    while (pos < text.size() && std::isspace(text[pos])) {
      pos++;
    }

    if (text.compare(pos, untilWord.size(), untilWord) == 0) {
      size_t endCheck = pos + untilWord.size();
      if (endCheck == text.size() || std::isspace(text[endCheck])) {
        pos = wsStart;
        return trimExpr(text.substr(start, wsStart - start));
      }
    }

    if (pos < text.size() && !std::isspace(text[pos])) {
      if (text[pos] == '"' || text[pos] == '\'') {
        char quote = text[pos];
        pos++;
        while (pos < text.size() && text[pos] != quote) {
          pos++;
        }
        if (pos < text.size())
          pos++;
      } else if (text.substr(pos, 10) == "@intrinsic") {
        pos += 10;
        if (pos < text.size() && text[pos] == '(') {
          int depth = 1;
          pos++;
          while (pos < text.size() && depth > 0) {
            if (text[pos] == '(')
              depth++;
            else if (text[pos] == ')')
              depth--;
            pos++;
          }
        }
      } else {
        while (pos < text.size() && !std::isspace(text[pos])) {
          pos++;
        }
      }
    }
  }

  return trimExpr(text.substr(start));
}

llvm::AllocaInst *SectionCodeGenerator::createEntryAlloca(
    llvm::Function *function, const std::string &name, llvm::Type *type) {
  llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(),
                               function->getEntryBlock().begin());
  return tmpBuilder.CreateAlloca(type, nullptr, name);
}

} // namespace tbx
