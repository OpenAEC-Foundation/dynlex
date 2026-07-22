#pragma once
#include "parseContext.h"
#include <memory>

namespace llvm {
class TargetMachine;
}

std::unique_ptr<llvm::TargetMachine> createNativeTargetMachine(ParseContext &context, std::string &errorMessage);

// Emit native executable from the LLVM module
// Returns true on success, false on error (errors added to context.diagnostics)
bool emitNativeExecutable(ParseContext &context);
