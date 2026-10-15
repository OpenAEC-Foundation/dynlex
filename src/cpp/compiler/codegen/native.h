#pragma once
#include "parseContext.h"
#include <memory>

namespace llvm {
class TargetMachine;
class Module;
} // namespace llvm

std::unique_ptr<llvm::TargetMachine> createNativeTargetMachine(ParseContext &context, std::string &errorMessage);

// Emit a native object file from the LLVM module.
// Returns true on success, false on error (errors added to context.diagnostics).
bool emitNativeObject(ParseContext &context);

// Emit a native executable from the LLVM module.
// Returns true on success, false on error (errors added to context.diagnostics).
bool emitNativeExecutable(ParseContext &context);

// Emit one independently linkable module with the selected target configuration.
bool emitNativeObjectModule(ParseContext &context, llvm::Module &module, const std::string &objectPath);

// Link supplied objects using the same native linker and runtime handling.
bool emitNativeExecutableWithObjects(
	ParseContext &context, const std::vector<std::string> &objectPaths, const std::vector<std::string> &libraries
);
