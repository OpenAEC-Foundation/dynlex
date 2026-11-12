#pragma once
#include <cstddef>
#include <memory>
#include <span>

namespace llvm {
class GlobalValue;
class Module;
} // namespace llvm

struct ModulePartitionCloneWork {
	size_t definitions = 0;
	size_t declarations = 0;
};

// Copy the selected definitions and materialize declaration interfaces only
// when their bodies, initializers, attributes or metadata refer to them.
std::unique_ptr<llvm::Module> cloneModulePartition(
	const llvm::Module &source, std::span<const llvm::GlobalValue *const> definitions, ModulePartitionCloneWork &work
);
