#include "compiler/codeGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace tbx {

// ============================================================================
// Type Handling Implementation
// ============================================================================

void SectionCodeGenerator::runTypeInference(SectionPatternResolver &resolver) {
  typeInference = std::make_unique<TypeInference>();
  typeInference->infer(resolver);

  // Build codegen patterns from typed patterns
  for (const auto &typedPattern : typeInference->typedPatterns()) {
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
}

llvm::Type *SectionCodeGenerator::typeToLlvm(InferredType type) {
  switch (type) {
  case InferredType::I64:
    return llvm::Type::getInt64Ty(*context);
  case InferredType::F64:
    return llvm::Type::getDoubleTy(*context);
  case InferredType::String:
    return llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
  case InferredType::I1:
    return llvm::Type::getInt1Ty(*context);
  case InferredType::Void:
    return llvm::Type::getVoidTy(*context);
  case InferredType::Unknown:
    return llvm::Type::getInt64Ty(*context); // Default to i64
  }
  return llvm::Type::getInt64Ty(*context);
}

InferredType SectionCodeGenerator::inferReturnTypeFromPattern(
    const ResolvedPattern *pattern) {
  if (!pattern)
    return InferredType::Void;

  switch (pattern->type) {
  case PatternType::Effect:
    return InferredType::Void;
  case PatternType::Expression:
    return InferredType::I64; // Default expressions return i64 (including
                              // booleans)
  case PatternType::Section:
    return InferredType::Void;
  }
  return InferredType::Void;
}

} // namespace tbx
