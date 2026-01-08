#pragma once

#include "compiler/typeInference.hpp"

#include <llvm/IR/Function.h>

#include <string>
#include <vector>

namespace tbx {

/**
 * CodegenPattern - Extended pattern information for code generation
 * Contains LLVM-specific data built on top of TypedPattern
 */
struct CodegenPattern {
  TypedPattern *typedPattern;              // The typed pattern from Step 4
  std::string functionName;                // Generated LLVM function name
  llvm::Function *llvmFunction = nullptr;  // The generated LLVM function
  std::vector<std::string> parameterNames; // Ordered parameter names
};

} // namespace tbx
