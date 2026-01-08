#include "compiler/codeGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// Trim whitespace from both ends of a string (local helper)
static std::string trimIntrinsic(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// ============================================================================
// Intrinsic Handling Implementation
// ============================================================================

llvm::Value *SectionCodeGenerator::generateIntrinsic(
    const std::string &text,
    const std::unordered_map<std::string, llvm::Value *> &localVars) {
  std::string name;
  std::vector<std::string> args;

  if (!parseIntrinsic(text, name, args)) {
    return nullptr;
  }

  if (name == "add" || name == "sub" || name == "mul" || name == "div") {
    if (args.size() < 2)
      return nullptr;

    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;

    if (name == "add") {
      return builder->CreateAdd(left, right, "addtmp");
    } else if (name == "sub") {
      return builder->CreateSub(left, right, "subtmp");
    } else if (name == "mul") {
      return builder->CreateMul(left, right, "multmp");
    } else if (name == "div") {
      return builder->CreateSDiv(left, right, "divtmp");
    }
  } else if (name == "print") {
    if (args.size() < 1)
      return nullptr;

    llvm::Value *val = generateExpression(args[0], localVars);
    if (!val)
      return nullptr;

    if (!fmtInt) {
      fmtInt = builder->CreateGlobalStringPtr("%lld\n", ".str.int");
    }
    if (!fmtFloat) {
      fmtFloat = builder->CreateGlobalStringPtr("%f\n", ".str.float");
    }
    if (!fmtStr) {
      fmtStr = builder->CreateGlobalStringPtr("%s\n", ".str.str");
    }

    llvm::Value *fmt;
    if (val->getType()->isIntegerTy()) {
      fmt = fmtInt;
    } else if (val->getType()->isFloatingPointTy()) {
      fmt = fmtFloat;
    } else {
      fmt = fmtStr;
    }

    builder->CreateCall(printfFunc, {fmt, val});
    return nullptr;
  } else if (name == "store") {
    if (args.size() < 2)
      return nullptr;

    std::string varName = trimIntrinsic(args[0]);
    llvm::Value *val = generateExpression(args[1], localVars);
    if (!val)
      return nullptr;

    auto it = namedValues.find(varName);
    if (it != namedValues.end()) {
      builder->CreateStore(val, it->second);
    } else {
      // Create new variable
      llvm::AllocaInst *alloca =
          createEntryAlloca(currentFunction, varName, val->getType());
      builder->CreateStore(val, alloca);
      namedValues[varName] = alloca;
    }
    return nullptr;
  } else if (name == "load") {
    if (args.size() < 1)
      return nullptr;

    std::string varName = trimIntrinsic(args[0]);
    auto it = namedValues.find(varName);
    if (it != namedValues.end()) {
      return builder->CreateLoad(it->second->getAllocatedType(), it->second,
                                 varName);
    }
    return nullptr;
  } else if (name == "return") {
    if (args.empty()) {
      return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
    }
    return generateExpression(args[0], localVars);
  } else if (name == "cmp_lt") {
    if (args.size() < 2)
      return nullptr;
    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;
    return builder->CreateICmpSLT(left, right, "cmptmp");
  } else if (name == "cmp_gt") {
    if (args.size() < 2)
      return nullptr;
    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;
    return builder->CreateICmpSGT(left, right, "cmptmp");
  } else if (name == "cmp_eq") {
    if (args.size() < 2)
      return nullptr;
    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;
    return builder->CreateICmpEQ(left, right, "eqtmp");
  } else if (name == "cmp_neq") {
    if (args.size() < 2)
      return nullptr;
    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;
    return builder->CreateICmpNE(left, right, "netmp");
  } else if (name == "cmp_lte") {
    if (args.size() < 2)
      return nullptr;
    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;
    return builder->CreateICmpSLE(left, right, "cmptmp");
  } else if (name == "cmp_gte") {
    if (args.size() < 2)
      return nullptr;
    llvm::Value *left = generateExpression(args[0], localVars);
    llvm::Value *right = generateExpression(args[1], localVars);
    if (!left || !right)
      return nullptr;
    return builder->CreateICmpSGE(left, right, "cmptmp");
  } else if (name == "frame") {
    if (args.empty()) {
      diagnosticsData.emplace_back(
          "@intrinsic(\"frame\", depth) requires depth argument");
      return nullptr;
    }
    llvm::Value *idxVal = generateExpression(args[0], localVars);
    if (idxVal && llvm::isa<llvm::ConstantInt>(idxVal)) {
      auto *constIdx = llvm::cast<llvm::ConstantInt>(idxVal);
      int64_t depth = constIdx->getSExtValue();
      if (depth >= 0 && depth < (int64_t)callStack.size()) {
        // Return a representation of the frame (opaque pointer or index)
        size_t targetIdx = callStack.size() - 1 - depth;
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                      targetIdx);
      }
    }
    return nullptr;
  } else if (name == "section") {
    // args[0] = frame, args[1] = sectioncount
    if (args.size() < 2) {
      diagnosticsData.emplace_back(
          "@intrinsic(\"section\", frame, name) requires arguments");
      return nullptr;
    }
    llvm::Value *frameVal = generateExpression(args[0], localVars);
    llvm::Value *sectionCountVal = generateExpression(args[1], localVars);

    if (frameVal && llvm::isa<llvm::ConstantInt>(frameVal)) {
      size_t frameIdx = llvm::cast<llvm::ConstantInt>(frameVal)->getZExtValue();
      if (frameIdx < callStack.size()) {
        const auto &frame = callStack[frameIdx];
        if (sectionCountVal && llvm::isa<llvm::ConstantInt>(sectionCountVal)) {
          int64_t count =
              llvm::cast<llvm::ConstantInt>(sectionCountVal)->getSExtValue();

          if (count == -1) {
            // Child section of the callsite
            if (frame.callSite && frame.callSite->childSection) {
              // Return an opaque pointer or index to the section
              return llvm::ConstantInt::get(
                  llvm::Type::getInt64Ty(*context),
                  (uint64_t)frame.callSite->childSection.get());
            }
          } else if (count >= 0) {
            // Current section (0) or parent sections (1+)
            Section *current = (frame.callSite && frame.callSite->childSection)
                                   ? frame.callSite->childSection->parent
                                   : nullptr;
            for (int64_t parentIndex = 0; parentIndex < count && current;
                 parentIndex++) {
              current = current->parent;
            }
            if (current) {
              return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                            (uint64_t)current);
            }
          }
        }
      }
    }
    return nullptr;
  } else if (name == "execute") {
    if (args.empty()) {
      diagnosticsData.emplace_back(
          "@intrinsic(\"execute\", section) requires section argument");
      return nullptr;
    }
    llvm::Value *sectionVal = generateExpression(args[0], localVars);
    if (sectionVal && llvm::isa<llvm::ConstantInt>(sectionVal)) {
      Section *section =
          (Section *)llvm::cast<llvm::ConstantInt>(sectionVal)->getZExtValue();
      if (section) {
        // Recursive generation of the section
        // We need to pass the resolver here.
        // For now, assume this is handled via a stored resolver or passed-in
        // state. generateSection(section, resolver);
      }
    }
    return nullptr;
  } else if (name == "loop_while") {
    if (args.size() < 2)
      return nullptr;
    // Basic while loop: cond, body_section
    // This should generate LLVM branch instructions
    return nullptr;
  } else if (name == "if") {
    // Implementation for if intrinsic
    return nullptr;
  } else if (name == "evaluate") {
    if (args.empty())
      return nullptr;
    // Evaluation of lazy expressions
    return generateExpression(args[0], localVars);
  }

  return nullptr;
}

bool SectionCodeGenerator::parseIntrinsic(const std::string &text,
                                          std::string &name,
                                          std::vector<std::string> &args) {
  std::string trimmed = trimIntrinsic(text);

  size_t returnPos = trimmed.find("return ");
  if (returnPos == 0) {
    trimmed = trimIntrinsic(trimmed.substr(7));
  }

  size_t start = trimmed.find("@intrinsic(");
  if (start == std::string::npos) {
    return false;
  }

  size_t openParen = start + 10;
  int parenDepth = 1;
  size_t closeParen = openParen + 1;

  while (closeParen < trimmed.size() && parenDepth > 0) {
    if (trimmed[closeParen] == '(')
      parenDepth++;
    else if (trimmed[closeParen] == ')')
      parenDepth--;
    closeParen++;
  }
  closeParen--;

  if (parenDepth != 0) {
    return false;
  }

  std::string content =
      trimmed.substr(openParen + 1, closeParen - openParen - 1);

  args.clear();
  std::string currentArg;
  int depth = 0;
  bool inQuotes = false;
  char quoteChar = '\0';

  for (size_t charIndex = 0; charIndex < content.size(); charIndex++) {
    char character = content[charIndex];

    if (inQuotes) {
      currentArg += character;
      if (character == quoteChar) {
        inQuotes = false;
      }
    } else if (character == '"' || character == '\'') {
      inQuotes = true;
      quoteChar = character;
      currentArg += character;
    } else if (character == '(') {
      depth++;
      currentArg += character;
    } else if (character == ')') {
      depth--;
      currentArg += character;
    } else if (character == ',' && depth == 0) {
      args.push_back(trimIntrinsic(currentArg));
      currentArg.clear();
    } else {
      currentArg += character;
    }
  }
  if (!currentArg.empty()) {
    args.push_back(trimIntrinsic(currentArg));
  }

  if (args.empty()) {
    return false;
  }

  name = args[0];
  if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'')) {
    name = name.substr(1, name.size() - 2);
  }

  args.erase(args.begin());

  return true;
}

} // namespace tbx
