#pragma once
#include "parseContext.h"
#include <memory>
#include <string>

namespace llvm {
class TargetMachine;
}

struct ParseContext;

std::unique_ptr<llvm::TargetMachine> createWASMTargetMachine(ParseContext &context, std::string &errorMessage);
bool emitWASMModule(ParseContext &context);
