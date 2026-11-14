#pragma once
#include "parseContext.h"

bool initializeTargetLayout(ParseContext &context);
void optimizeLLVMModule(ParseContext &context, llvm::Module &module, bool prepareThinLTO = false);
bool generateCode(ParseContext &context, const std::function<bool(ParseContext &)> &nativeEmitter = {});
