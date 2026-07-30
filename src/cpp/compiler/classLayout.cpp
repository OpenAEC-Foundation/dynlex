#include "classLayout.h"
#include "compilerUtils.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>

namespace {

uint64_t fixedAllocationSize(const llvm::DataLayout &dataLayout, llvm::Type *type) {
	llvm::TypeSize allocationSize = dataLayout.getTypeAllocSize(type);
	requireCompilerInvariant(!allocationSize.isScalable(), "class layout requires fixed-size fields");
	return allocationSize.getFixedValue();
}

} // namespace

LLVMClassLayout layoutLLVMClass(
	llvm::LLVMContext &context, const llvm::DataLayout &dataLayout, llvm::StructType &structType,
	const std::vector<llvm::Type *> &fieldTypes, const std::vector<uint64_t> &requestedFieldAlignments,
	uint64_t requestedClassAlignment
) {
	requireCompilerInvariant(fieldTypes.size() == requestedFieldAlignments.size(), "class layout field count mismatch");

	LLVMClassLayout result;
	result.fieldIndices.reserve(fieldTypes.size());
	result.fieldOffsets.reserve(fieldTypes.size());
	std::vector<llvm::Type *> llvmFields;
	llvmFields.reserve(fieldTypes.size());
	uint64_t offset = 0;
	uint64_t requestedStructAlignment = std::max<uint64_t>(requestedClassAlignment, 1);

	for (size_t fieldIndex = 0; fieldIndex < fieldTypes.size(); fieldIndex++) {
		llvm::Type *fieldType = fieldTypes[fieldIndex];
		uint64_t naturalFieldAlignment = dataLayout.getABITypeAlign(fieldType).value();
		uint64_t fieldAlignment = std::max(naturalFieldAlignment, requestedFieldAlignments[fieldIndex]);
		uint64_t naturalFieldOffset = llvm::alignTo(offset, naturalFieldAlignment);
		uint64_t fieldOffset = llvm::alignTo(offset, fieldAlignment);
		if (fieldOffset != naturalFieldOffset)
			llvmFields.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(context), fieldOffset - offset));
		result.fieldIndices.push_back(llvmFields.size());
		result.fieldOffsets.push_back(fieldOffset);
		llvmFields.push_back(fieldType);
		offset = fieldOffset + fixedAllocationSize(dataLayout, fieldType);
		requestedStructAlignment = std::max(requestedStructAlignment, fieldAlignment);
	}

	llvm::StructType *naturalStructType = llvm::StructType::get(context, llvmFields);
	uint64_t naturalStructAlignment = dataLayout.getABITypeAlign(naturalStructType).value();
	uint64_t naturalAllocationSize = fixedAllocationSize(dataLayout, naturalStructType);
	result.abiAlignment = std::max(naturalStructAlignment, requestedStructAlignment);
	result.allocationSize = llvm::alignTo(naturalAllocationSize, result.abiAlignment);
	if (result.allocationSize != naturalAllocationSize) {
		requireCompilerInvariant(result.allocationSize > offset, "class tail padding does not follow its fields");
		llvmFields.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(context), result.allocationSize - offset));
	}
	structType.setBody(llvmFields);

	const llvm::StructLayout *layout = dataLayout.getStructLayout(&structType);
	requireCompilerInvariant(
		fixedAllocationSize(dataLayout, &structType) == result.allocationSize,
		"LLVM class allocation size differs from the DynLex layout"
	);
	requireCompilerInvariant(
		dataLayout.getABITypeAlign(&structType).value() <= result.abiAlignment,
		"LLVM class ABI alignment exceeds the DynLex layout"
	);
	for (size_t fieldIndex = 0; fieldIndex < result.fieldOffsets.size(); fieldIndex++) {
		requireCompilerInvariant(
			layout->getElementOffset(result.fieldIndices[fieldIndex]) == result.fieldOffsets[fieldIndex],
			"LLVM class field offset differs from the DynLex layout"
		);
	}
	return result;
}
