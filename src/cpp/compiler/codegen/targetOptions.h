#pragma once

#include "parseContext.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetOptions.h"

llvm::CodeGenOptLevel codeGenerationOptimizationLevel(const ParseContext::Options &options);
llvm::TargetOptions llvmTargetOptions(const ParseContext::Options &options);
