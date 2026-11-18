#pragma once
#include "parseContext.h"
#include <memory>
#include <string>

namespace llvm {
class TargetMachine;
}

struct ParseContext;

std::unique_ptr<llvm::TargetMachine> createWASMTargetMachine(ParseContext &context, std::string &errorMessage);
std::string wasmOutputPath(const ParseContext::Options &options);
bool emitWASMObject(ParseContext &context, const std::string &objectPath);
bool linkWASMObject(ParseContext &context, const std::string &objectPath);
bool emitWASMModule(ParseContext &context);
