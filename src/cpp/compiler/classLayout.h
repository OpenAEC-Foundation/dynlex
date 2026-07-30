#pragma once
#include <cstdint>
#include <vector>

namespace llvm {
class DataLayout;
class LLVMContext;
class StructType;
class Type;
} // namespace llvm

struct LLVMClassLayout {
	std::vector<unsigned> fieldIndices;
	std::vector<uint64_t> fieldOffsets;
	uint64_t allocationSize = 0;
	uint64_t abiAlignment = 0;
};

LLVMClassLayout layoutLLVMClass(
	llvm::LLVMContext &context, const llvm::DataLayout &dataLayout, llvm::StructType &structType,
	const std::vector<llvm::Type *> &fieldTypes, const std::vector<uint64_t> &requestedFieldAlignments,
	uint64_t requestedClassAlignment
);
