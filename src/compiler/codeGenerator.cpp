#include "compiler/codeGenerator.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>

namespace tbx {

// Trim whitespace from both ends of a string
static std::string trim(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// ============================================================================
// SectionCodeGenerator Core Implementation
// ============================================================================

SectionCodeGenerator::SectionCodeGenerator(const std::string &moduleName) {
  context = std::make_unique<llvm::LLVMContext>();
  module = std::make_unique<llvm::Module>(moduleName, *context);
  builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

bool SectionCodeGenerator::generate(SectionPatternResolver &resolver,
                                    Section *root) {
  if (!root) {
    diagnosticsData.emplace_back("Cannot generate code from null section");
    return false;
  }

  // Clear previous state
  codegenPatterns.clear();
  patternToCodegen.clear();
  namedValues.clear();
  diagnosticsData.clear();
  resolverRef = &resolver;

  // Run type inference internally
  runTypeInference(resolver);

  // Generate external declarations (printf, etc.)
  generateExternalDeclarations();

  // Two-pass function generation:
  // Pass 1: Declare all functions (so they can call each other)
  for (auto &codegenPattern : codegenPatterns) {
    declarePatternFunction(*codegenPattern);
  }

  // Pass 2: Generate function bodies
  for (auto &codegenPattern : codegenPatterns) {
    generatePatternFunctionBody(*codegenPattern);
  }

  // Generate main function from top-level code
  generateMain(root, resolver);

  // Verify the module
  std::string verifyError;
  llvm::raw_string_ostream verifyStream(verifyError);
  if (llvm::verifyModule(*module, &verifyStream)) {
    diagnosticsData.emplace_back("Module verification failed: " + verifyError);
    return false;
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

bool SectionCodeGenerator::generate(const TypeInference &typeInferenceResult,
                                    SectionPatternResolver &resolver,
                                    Section *root) {
  if (!root) {
    diagnosticsData.emplace_back("Cannot generate code from null section");
    return false;
  }

  // Clear previous state
  codegenPatterns.clear();
  patternToCodegen.clear();
  namedValues.clear();
  diagnosticsData.clear();
  resolverRef = &resolver;

  // Use provided type inference results
  for (const auto &typedPattern : typeInferenceResult.typedPatterns()) {
    auto codegen = std::make_unique<CodegenPattern>();
    codegen->typedPattern = typedPattern.get();

    // Generate function name
    std::string baseName;
    if (typedPattern->pattern) {
      switch (typedPattern->pattern->type) {
      case PatternType::Effect:
        baseName = "effect_";
        break;
      case PatternType::Expression:
        baseName = "expr_";
        break;
      case PatternType::Section:
        baseName = "section_";
        break;
      }

      std::string cleanName;
      for (char character : typedPattern->pattern->pattern) {
        if (std::isalnum(character)) {
          cleanName += character;
        } else if (character == ' ' && !cleanName.empty() &&
                   cleanName.back() != '_') {
          cleanName += '_';
        }
      }
      codegen->functionName = baseName + cleanName;

      // Collect parameter names in order
      for (const auto &var : typedPattern->pattern->variables) {
        codegen->parameterNames.push_back(var);
      }

      patternToCodegen[typedPattern->pattern] = codegen.get();
    }

    codegenPatterns.push_back(std::move(codegen));
  }

  // Generate external declarations
  generateExternalDeclarations();

  // Two-pass function generation:
  // Pass 1: Declare all functions (so they can call each other)
  for (auto &codegenPattern : codegenPatterns) {
    declarePatternFunction(*codegenPattern);
  }

  // Pass 2: Generate function bodies
  for (auto &codegenPattern : codegenPatterns) {
    generatePatternFunctionBody(*codegenPattern);
  }

  // Generate main
  generateMain(root, resolver);

  // Verify module
  std::string verifyError;
  llvm::raw_string_ostream verifyStream(verifyError);
  if (llvm::verifyModule(*module, &verifyStream)) {
    diagnosticsData.emplace_back("Module verification failed: " + verifyError);
    return false;
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

bool SectionCodeGenerator::writeIr(const std::string &filename) {
  std::error_code ec;
  llvm::raw_fd_ostream out(filename, ec, llvm::sys::fs::OF_None);
  if (ec) {
    diagnosticsData.emplace_back("Cannot open file: " + filename);
    return false;
  }
  module->print(out, nullptr);
  return true;
}

void SectionCodeGenerator::printIr() { module->print(llvm::outs(), nullptr); }

// ============================================================================
// External Declarations
// ============================================================================

void SectionCodeGenerator::generateExternalDeclarations() {
  // Declare printf
  llvm::FunctionType *printfType = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(*context),
      {llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0)},
      true // vararg
  );
  printfFunc = module->getOrInsertFunction("printf", printfType);

  // Note: format strings will be created lazily when needed
  // They require an insertion point, which is set when generating main()
  fmtInt = nullptr;
  fmtFloat = nullptr;
  fmtStr = nullptr;
}

// ============================================================================
// Main Code Generation
// ============================================================================

void SectionCodeGenerator::generateMain(Section *root,
                                        SectionPatternResolver &resolver) {
  // Create main function
  llvm::FunctionType *mainType =
      llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), {}, false);

  llvm::Function *mainFunc = llvm::Function::Create(
      mainType, llvm::Function::ExternalLinkage, "main", module.get());

  llvm::BasicBlock *entry =
      llvm::BasicBlock::Create(*context, "entry", mainFunc);
  builder->SetInsertPoint(entry);
  currentFunction = mainFunc;
  namedValues.clear();

  // Setup initial frame on call stack
  FrameContext mainFrame;
  mainFrame.callSite = nullptr; // Root
  callStack.push_back(mainFrame);

  // Generate code for top-level pattern references
  for (const auto &line : root->lines) {
    // Skip pattern definitions (they become functions)
    if (line.isPatternDefinition) {
      continue;
    }

    // Try to find a matching pattern for this line
    generateCodeLine(const_cast<CodeLine *>(&line), resolver);
  }

  callStack.pop_back();

  // Return 0 from main
  builder->CreateRet(
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
  currentFunction = nullptr;
}

llvm::Value *
SectionCodeGenerator::generateCodeLine(CodeLine *line,
                                       SectionPatternResolver &resolver) {
  if (!line)
    return nullptr;

  std::string text = trim(line->text);
  if (text.empty())
    return nullptr;

  // Check if this is a direct intrinsic call
  if (text.find("@intrinsic(") != std::string::npos) {
    return generateIntrinsic(text, {});
  }

  // Parse the line into words (used by pattern matching and fallback)
  // Handle quoted strings as single tokens
  std::vector<std::string> lineWords;
  {
    size_t pos = 0;
    while (pos < text.size()) {
      // Skip whitespace
      while (pos < text.size() && std::isspace(text[pos])) {
        pos++;
      }
      if (pos >= text.size())
        break;

      std::string token;
      if (text[pos] == '"' || text[pos] == '\'') {
        // Quoted string - capture until closing quote
        char quote = text[pos];
        token += text[pos++];
        while (pos < text.size() && text[pos] != quote) {
          token += text[pos++];
        }
        if (pos < text.size()) {
          token += text[pos++]; // Include closing quote
        }
      } else {
        // Regular word - capture until whitespace
        while (pos < text.size() && !std::isspace(text[pos])) {
          token += text[pos++];
        }
      }
      if (!token.empty()) {
        lineWords.push_back(token);
      }
    }
  }

  // Check if the resolver already matched this line to a pattern
  PatternMatch *match = resolver.getPatternMatch(line);
  if (match && match->pattern) {
    // Find the corresponding codegen pattern
    for (auto &codegenPattern : codegenPatterns) {
      if (!codegenPattern->llvmFunction)
        continue;

      TypedPattern *typed = codegenPattern->typedPattern;
      if (!typed || !typed->pattern)
        continue;

      if (typed->pattern == match->pattern) {
        // Build arguments from match
        std::vector<llvm::Value *> args;
        for (const auto &paramName : codegenPattern->parameterNames) {
          auto argIt = match->arguments.find(paramName);
          if (argIt != match->arguments.end()) {
            const auto &argInfo = argIt->second;
            llvm::Value *argVal = nullptr;

            // Convert the matched value to LLVM value
            if (std::holds_alternative<int64_t>(argInfo.value)) {
              argVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                              std::get<int64_t>(argInfo.value));
            } else if (std::holds_alternative<double>(argInfo.value)) {
              argVal = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                             std::get<double>(argInfo.value));
            } else if (std::holds_alternative<std::string>(argInfo.value)) {
              std::string argStr = std::get<std::string>(argInfo.value);
              // Try to evaluate as expression first
              argVal = generateExpression(argStr, {});
              // Note: if generateExpression returns nullptr, we cannot create
              // a string constant because pattern functions expect typed values
              // Fall through to word-based matching which handles this case
            }

            if (argVal) {
              // Type conversion if needed
              llvm::Type *expectedType =
                  codegenPattern->llvmFunction->getFunctionType()->getParamType(
                      args.size());
              if (argVal->getType() != expectedType) {
                if (argVal->getType()->isIntegerTy() &&
                    expectedType->isIntegerTy()) {
                  if (argVal->getType()->getIntegerBitWidth() <
                      expectedType->getIntegerBitWidth()) {
                    argVal = builder->CreateZExt(argVal, expectedType, "zext");
                  } else {
                    argVal =
                        builder->CreateTrunc(argVal, expectedType, "trunc");
                  }
                } else if (argVal->getType()->isDoubleTy() &&
                           expectedType->isIntegerTy()) {
                  argVal =
                      builder->CreateFPToSI(argVal, expectedType, "fptosi");
                } else if (argVal->getType()->isIntegerTy() &&
                           expectedType->isDoubleTy()) {
                  argVal =
                      builder->CreateSIToFP(argVal, expectedType, "sitofp");
                } else {
                  // Type incompatible, skip to fallback matching
                  argVal = nullptr;
                }
              }
              if (argVal) {
                args.push_back(argVal);
              }
            }
          }
        }

        if (args.size() == codegenPattern->parameterNames.size()) {
          return builder->CreateCall(codegenPattern->llvmFunction, args);
        }
      }
    }
  }

  // Fallback: Try to parse as a pattern call by matching the line text
  for (auto &codegenPattern : codegenPatterns) {
    if (!codegenPattern->llvmFunction)
      continue;

    TypedPattern *typed = codegenPattern->typedPattern;
    if (!typed || !typed->pattern)
      continue;

    ResolvedPattern *pattern = typed->pattern;

    // Special case: check if the line text exactly matches the pattern's
    // original text
    if (pattern->originalText == text) {
      llvm::FunctionType *funcType =
          codegenPattern->llvmFunction->getFunctionType();
      if (funcType->getNumParams() == 0) {
        return builder->CreateCall(codegenPattern->llvmFunction, {});
      } else {
        std::vector<llvm::Value *> defaultArgs;
        for (unsigned paramIndex = 0; paramIndex < funcType->getNumParams();
             paramIndex++) {
          llvm::Type *paramType = funcType->getParamType(paramIndex);
          if (paramType->isIntegerTy()) {
            defaultArgs.push_back(llvm::ConstantInt::get(paramType, 0));
          } else if (paramType->isDoubleTy()) {
            defaultArgs.push_back(llvm::ConstantFP::get(paramType, 0.0));
          } else {
            defaultArgs.push_back(llvm::Constant::getNullValue(paramType));
          }
        }
        return builder->CreateCall(codegenPattern->llvmFunction, defaultArgs);
      }
    }

    // Parse pattern into words
    std::vector<std::string> patternWords;
    std::string word;
    std::istringstream pss(pattern->pattern);
    while (pss >> word) {
      patternWords.push_back(word);
    }

    // Quick check: same number of words
    if (lineWords.size() != patternWords.size())
      continue;

    // Try to match
    bool matches = true;
    std::vector<llvm::Value *> args;

    for (size_t wordIndex = 0; wordIndex < patternWords.size(); wordIndex++) {
      if (patternWords[wordIndex] == "$") {
        // Variable slot - extract argument
        const std::string &argStr = lineWords[wordIndex];

        // Use generateExpression to properly handle literals, variables, etc.
        llvm::Value *argVal = generateExpression(argStr, {});
        if (argVal) {
          args.push_back(argVal);
        } else {
          // Unknown identifier - pass as string constant (for variable names
          // etc.)
          args.push_back(builder->CreateGlobalStringPtr(argStr));
        }
      } else {
        // Literal word - must match exactly
        if (patternWords[wordIndex] != lineWords[wordIndex]) {
          matches = false;
          break;
        }
      }
    }

    if (matches && args.size() == codegenPattern->parameterNames.size()) {
      // Check type compatibility before calling
      llvm::FunctionType *funcType =
          codegenPattern->llvmFunction->getFunctionType();
      bool typesCompatible = true;

      for (size_t argIndex = 0; argIndex < args.size(); argIndex++) {
        llvm::Type *expectedType = funcType->getParamType(argIndex);
        llvm::Type *actualType = args[argIndex]->getType();

        if (actualType != expectedType) {
          // Try type conversions
          if (actualType->isIntegerTy() && expectedType->isIntegerTy()) {
            // Integer width conversion
            if (actualType->getIntegerBitWidth() <
                expectedType->getIntegerBitWidth()) {
              args[argIndex] =
                  builder->CreateZExt(args[argIndex], expectedType, "zext");
            } else {
              args[argIndex] =
                  builder->CreateTrunc(args[argIndex], expectedType, "trunc");
            }
          } else if (actualType->isPointerTy() && expectedType->isIntegerTy()) {
            typesCompatible = false;
            break;
          } else if (actualType->isIntegerTy() && expectedType->isPointerTy()) {
            typesCompatible = false;
            break;
          } else {
            typesCompatible = false;
            break;
          }
        }
      }

      if (typesCompatible) {
        // Call the pattern function
        return builder->CreateCall(codegenPattern->llvmFunction, args);
      }
    }
  }

  // Fallback: Direct handling for common patterns
  for (auto &codegenPattern : codegenPatterns) {
    if (!codegenPattern->llvmFunction)
      continue;

    TypedPattern *typed = codegenPattern->typedPattern;
    if (!typed || !typed->pattern || !typed->pattern->body)
      continue;

    ResolvedPattern *pattern = typed->pattern;

    std::vector<std::string> patternWords;
    std::istringstream pss(pattern->pattern);
    std::string word;
    while (pss >> word) {
      patternWords.push_back(word);
    }

    if (lineWords.size() != patternWords.size())
      continue;

    bool matches = true;
    std::vector<std::string> argStrings;

    for (size_t wordIndex = 0; wordIndex < patternWords.size() && matches;
         wordIndex++) {
      if (patternWords[wordIndex] == "$") {
        argStrings.push_back(lineWords[wordIndex]);
      } else if (patternWords[wordIndex] != lineWords[wordIndex]) {
        matches = false;
      }
    }

    if (!matches)
      continue;

    for (const auto &bodyLine : pattern->body->lines) {
      std::string lineText = trim(bodyLine.text);
      if (!lineText.empty() && lineText.back() == ':') {
        lineText.pop_back();
      }

      if ((lineText == "execute" || lineText == "get") &&
          bodyLine.childSection) {
        for (const auto &childLine : bodyLine.childSection->lines) {
          std::string childText = trim(childLine.text);

          if (childText.find("@intrinsic(") != std::string::npos) {
            std::string expanded = childText;
            for (size_t patternVarIndex = 0;
                 patternVarIndex < pattern->variables.size() &&
                 patternVarIndex < argStrings.size();
                 patternVarIndex++) {
              const std::string &paramName =
                  pattern->variables[patternVarIndex];
              const std::string &argValue = argStrings[patternVarIndex];

              size_t pos = 0;
              while ((pos = expanded.find(paramName, pos)) !=
                     std::string::npos) {
                bool startOk = (pos == 0 || !std::isalnum(expanded[pos - 1]));
                bool endOk = (pos + paramName.size() >= expanded.size() ||
                              !std::isalnum(expanded[pos + paramName.size()]));
                if (startOk && endOk) {
                  expanded.replace(pos, paramName.size(), argValue);
                  pos += argValue.size();
                } else {
                  pos++;
                }
              }
            }

            llvm::Value *res = generateIntrinsic(expanded, {});
            if (res || expanded.find("\"print\"") != std::string::npos ||
                expanded.find("\"store\"") != std::string::npos) {
              return res;
            }
          }
        }
      }
    }
  }

  return nullptr;
}

llvm::Value *SectionCodeGenerator::generateBodyLine(const std::string &text) {
  std::string trimmed = trim(text);
  if (trimmed.empty())
    return nullptr;

  if (trimmed.find("@intrinsic(") != std::string::npos) {
    std::string check = trimmed;
    if (check.substr(0, 7) == "return ") {
      check = trim(check.substr(7));
    }
    if (check.find("@intrinsic(") == 0) {
      return generateIntrinsic(trimmed, {});
    }
  }

  for (auto &codegenPattern : codegenPatterns) {
    if (!codegenPattern->llvmFunction)
      continue;

    TypedPattern *typed = codegenPattern->typedPattern;
    if (!typed || !typed->pattern)
      continue;

    ResolvedPattern *pattern = typed->pattern;

    if (pattern->originalText == trimmed) {
      llvm::FunctionType *funcType =
          codegenPattern->llvmFunction->getFunctionType();
      if (funcType->getNumParams() == 0) {
        return builder->CreateCall(codegenPattern->llvmFunction, {});
      }
    }
  }

  for (auto &codegenPattern : codegenPatterns) {
    if (!codegenPattern->llvmFunction)
      continue;

    TypedPattern *typed = codegenPattern->typedPattern;
    if (!typed || !typed->pattern)
      continue;

    ResolvedPattern *pattern = typed->pattern;

    if (pattern->pattern == "set $ to $" || pattern->pattern == "return $") {
      continue;
    }

    std::vector<std::string> patternWords;
    std::istringstream pss(pattern->pattern);
    std::string word;
    while (pss >> word) {
      patternWords.push_back(word);
    }

    std::vector<std::string> extractedArgs;
    bool matches = true;
    size_t textPos = 0;

    for (size_t patternWordIndex = 0;
         patternWordIndex < patternWords.size() && matches;
         patternWordIndex++) {
      while (textPos < trimmed.size() && std::isspace(trimmed[textPos])) {
        textPos++;
      }

      if (patternWords[patternWordIndex] == "$") {
        std::string nextLiteral;
        if (patternWordIndex + 1 < patternWords.size() &&
            patternWords[patternWordIndex + 1] != "$") {
          nextLiteral = patternWords[patternWordIndex + 1];
        }

        std::string arg;
        if (nextLiteral.empty()) {
          arg = extractArgument(trimmed, textPos);
        } else {
          arg = extractArgumentUntil(trimmed, textPos, nextLiteral);
        }

        if (arg.empty()) {
          matches = false;
        } else {
          extractedArgs.push_back(arg);
        }
      } else {
        std::string literal = patternWords[patternWordIndex];
        if (trimmed.compare(textPos, literal.size(), literal) == 0) {
          size_t endPos = textPos + literal.size();
          if (endPos == trimmed.size() || std::isspace(trimmed[endPos])) {
            textPos = endPos;
          } else {
            matches = false;
          }
        } else {
          matches = false;
        }
      }
    }

    while (textPos < trimmed.size() && std::isspace(trimmed[textPos])) {
      textPos++;
    }
    if (textPos != trimmed.size()) {
      matches = false;
    }

    if (matches &&
        extractedArgs.size() == codegenPattern->parameterNames.size()) {
      std::vector<llvm::Value *> args;
      bool typesCompatible = true;

      llvm::FunctionType *funcType =
          codegenPattern->llvmFunction->getFunctionType();

      for (size_t argIndex = 0; argIndex < extractedArgs.size(); argIndex++) {
        const auto &argStr = extractedArgs[argIndex];
        llvm::Value *argVal = generateExpression(argStr, {});
        if (!argVal) {
          argVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        }

        llvm::Type *expectedType = funcType->getParamType(argIndex);
        if (argVal->getType() != expectedType) {
          if (argVal->getType()->isIntegerTy() && expectedType->isIntegerTy()) {
            if (argVal->getType()->getIntegerBitWidth() <
                expectedType->getIntegerBitWidth()) {
              argVal = builder->CreateZExt(argVal, expectedType, "zext");
            } else {
              argVal = builder->CreateTrunc(argVal, expectedType, "trunc");
            }
          } else if (argVal->getType()->isPointerTy() &&
                     expectedType->isIntegerTy()) {
            typesCompatible = false;
            break;
          } else {
            typesCompatible = false;
            break;
          }
        }

        args.push_back(argVal);
      }

      if (typesCompatible) {
        return builder->CreateCall(codegenPattern->llvmFunction, args);
      }
    }
  }

  if (trimmed.size() > 6 && trimmed.substr(0, 6) == "print ") {
    std::string argStr = trim(trimmed.substr(6));
    llvm::Value *val = generateExpression(argStr, {});
    if (val) {
      if (!fmtInt) {
        fmtInt = builder->CreateGlobalStringPtr("%lld\n", ".str.int");
      }
      if (!fmtStr) {
        fmtStr = builder->CreateGlobalStringPtr("%s\n", ".str.str");
      }

      llvm::Value *fmt;
      if (val->getType()->isIntegerTy()) {
        fmt = fmtInt;
      } else {
        fmt = fmtStr;
      }
      builder->CreateCall(printfFunc, {fmt, val});
      return nullptr;
    }
  }

  if (trimmed.size() > 4 && trimmed.substr(0, 4) == "set ") {
    size_t toPos = trimmed.find(" to ");
    if (toPos != std::string::npos) {
      std::string varName = trim(trimmed.substr(4, toPos - 4));
      std::string valStr = trim(trimmed.substr(toPos + 4));
      llvm::Value *val = generateExpression(valStr, {});
      if (val) {
        auto it = namedValues.find(varName);
        if (it != namedValues.end()) {
          builder->CreateStore(val, it->second);
        } else {
          llvm::AllocaInst *alloca =
              createEntryAlloca(currentFunction, varName, val->getType());
          builder->CreateStore(val, alloca);
          namedValues[varName] = alloca;
        }
        return nullptr;
      }
    }
  }

  return nullptr;
}

} // namespace tbx
