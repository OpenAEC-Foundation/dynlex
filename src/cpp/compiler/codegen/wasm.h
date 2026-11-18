#pragma once
#include "parseContext.h"
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace llvm {
class GlobalValue;
class LLVMContext;
class Module;
class TargetMachine;
}

struct ModulePartitionCloneWork;

struct WASMObjectPartition {
	std::vector<const llvm::GlobalValue *> definitions;
	bool containsAssembly = false;
};

std::unique_ptr<llvm::TargetMachine> createWASMTargetMachine(ParseContext &context, std::string &errorMessage);
std::string wasmOutputPath(const ParseContext::Options &options);
std::unique_ptr<llvm::Module> optimizeWASMModule(
	ParseContext &context, llvm::LLVMContext &llvmContext, const std::string &identifier, std::string_view generatedIR
);
std::vector<WASMObjectPartition> prepareWASMObjectPartitions(llvm::Module &module);
std::unique_ptr<llvm::Module> cloneWASMObjectPartition(
	const llvm::Module &module, const WASMObjectPartition &partition, ModulePartitionCloneWork &work
);
bool emitWASMObject(ParseContext &context, llvm::Module &module, const std::string &objectPath);
bool linkWASMObjects(ParseContext &context, std::span<const std::string> objectPaths);
bool emitWASMModule(ParseContext &context);
