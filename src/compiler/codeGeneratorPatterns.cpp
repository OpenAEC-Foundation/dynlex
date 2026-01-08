#include "compiler/codeGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// Trim whitespace from both ends of a string (local helper)
static std::string trimPatterns(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// ============================================================================
// Pattern Function Generation Implementation
// ============================================================================

void SectionCodeGenerator::declarePatternFunction(
    CodegenPattern &codegenPattern) {
  TypedPattern *typed = codegenPattern.typedPattern;
  if (!typed || !typed->pattern || !typed->pattern->body) {
    return;
  }

  ResolvedPattern *pattern = typed->pattern;

  // Skip single-word patterns that are just section markers (like "execute:",
  // "get:")
  if (pattern->isSingleWord() && pattern->body->lines.empty()) {
    return;
  }

  // Create parameter types
  std::vector<llvm::Type *> paramTypes;
  for (const auto &varName : codegenPattern.parameterNames) {
    auto it = typed->parameterTypes.find(varName);
    InferredType paramType =
        (it != typed->parameterTypes.end()) ? it->second : InferredType::I64;
    paramTypes.push_back(typeToLlvm(paramType));
  }

  // Create function type
  llvm::Type *returnType = typeToLlvm(typed->returnType);
  llvm::FunctionType *funcType =
      llvm::FunctionType::get(returnType, paramTypes, false);

  // Create function (declaration only - no body yet)
  llvm::Function *function =
      llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                             codegenPattern.functionName, module.get());
  codegenPattern.llvmFunction = function;

  // Set parameter names
  size_t idx = 0;
  for (auto &arg : function->args()) {
    if (idx < codegenPattern.parameterNames.size()) {
      arg.setName(codegenPattern.parameterNames[idx]);
      idx++;
    }
  }
}

void SectionCodeGenerator::generatePatternFunctionBody(
    CodegenPattern &codegenPattern) {
  TypedPattern *typed = codegenPattern.typedPattern;
  if (!typed || !typed->pattern || !typed->pattern->body) {
    return;
  }

  llvm::Function *function = codegenPattern.llvmFunction;
  if (!function) {
    return; // Function wasn't declared (e.g., skipped section marker)
  }

  ResolvedPattern *pattern = typed->pattern;

  // Create entry block
  llvm::BasicBlock *entry =
      llvm::BasicBlock::Create(*context, "entry", function);
  builder->SetInsertPoint(entry);

  // Create allocas for parameters and add to namedValues
  currentFunction = function;
  namedValues.clear();

  size_t idx = 0;
  for (auto &arg : function->args()) {
    const std::string &name = codegenPattern.parameterNames[idx];
    llvm::AllocaInst *alloca = createEntryAlloca(function, name, arg.getType());
    builder->CreateStore(&arg, alloca);
    namedValues[name] = alloca;
    idx++;
  }

  // Generate code for pattern body
  // First, look for the relevant section (like "execute:" or "get:")
  Section *bodySection = nullptr;
  for (const auto &line : pattern->body->lines) {
    std::string lineText = trimPatterns(line.text);
    // Remove trailing colon if present
    if (!lineText.empty() && lineText.back() == ':') {
      lineText.pop_back();
    }

    // Check for known section types
    if (lineText == "execute" || lineText == "get" || lineText == "check") {
      if (line.childSection) {
        bodySection = line.childSection.get();
        break;
      }
    }
  }

  llvm::Value *result = nullptr;
  if (bodySection) {
    // Generate code for each line in the body section
    for (const auto &line : bodySection->lines) {
      result = generateBodyLine(line.text);
    }
  }

  // Add appropriate return
  if (typed->returnType == InferredType::Void) {
    builder->CreateRetVoid();
  } else {
    llvm::Type *expectedRetType = typeToLlvm(typed->returnType);
    bool hasValidResult = result && !result->getType()->isVoidTy();

    if (hasValidResult) {
      // Check if result type matches expected return type
      if (result->getType() != expectedRetType) {
        // Handle i1 to i64 conversion for comparison results
        if (result->getType()->isIntegerTy(1) &&
            expectedRetType->isIntegerTy(64)) {
          result = builder->CreateZExt(result, expectedRetType, "zext_cmp");
        }
        // Handle i64 to i1 conversion (truncate)
        else if (result->getType()->isIntegerTy(64) &&
                 expectedRetType->isIntegerTy(1)) {
          result = builder->CreateTrunc(result, expectedRetType, "trunc_bool");
        }
        // Handle pointer to integer (invalid - use default)
        else if (result->getType()->isPointerTy() &&
                 expectedRetType->isIntegerTy()) {
          hasValidResult = false;
        }
      }
    }

    if (hasValidResult) {
      builder->CreateRet(result);
    } else {
      // Default return value
      builder->CreateRet(llvm::ConstantInt::get(expectedRetType, 0));
    }
  }

  currentFunction = nullptr;
}

llvm::Value *SectionCodeGenerator::generatePatternCall(PatternMatch *match) {
  if (!match || !match->pattern)
    return nullptr;

  auto it = patternToCodegen.find(match->pattern);
  if (it == patternToCodegen.end())
    return nullptr;

  CodegenPattern *codegenPattern = it->second;
  if (!codegenPattern->llvmFunction)
    return nullptr;

  std::vector<llvm::Value *> args;
  for (const auto &paramName : codegenPattern->parameterNames) {
    auto argIt = match->arguments.find(paramName);
    if (argIt != match->arguments.end()) {
      const ResolvedValue &val = argIt->second.value;

      if (std::holds_alternative<int64_t>(val)) {
        args.push_back(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                              std::get<int64_t>(val), true));
      } else if (std::holds_alternative<double>(val)) {
        args.push_back(llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                             std::get<double>(val)));
      } else if (std::holds_alternative<std::string>(val)) {
        const std::string &str = std::get<std::string>(val);

        bool isInt = !str.empty();
        for (size_t charIndex = 0; charIndex < str.size(); charIndex++) {
          char character = str[charIndex];
          if (character == '-' && charIndex == 0)
            continue;
          if (!std::isdigit(character)) {
            isInt = false;
            break;
          }
        }

        if (isInt) {
          args.push_back(llvm::ConstantInt::get(
              llvm::Type::getInt64Ty(*context), std::stoll(str), true));
        } else {
          auto varIt = namedValues.find(str);
          if (varIt != namedValues.end()) {
            args.push_back(builder->CreateLoad(
                varIt->second->getAllocatedType(), varIt->second, str));
          } else {
            args.push_back(
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
          }
        }
      } else {
        args.push_back(
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
      }
    } else {
      args.push_back(
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
    }
  }

  return builder->CreateCall(codegenPattern->llvmFunction, args);
}

} // namespace tbx
