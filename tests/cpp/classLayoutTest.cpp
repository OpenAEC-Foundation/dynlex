#include "compiler/classLayout.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
	if (condition)
		return;
	std::cerr << message << '\n';
	std::exit(1);
}

} // namespace

int main() {
	llvm::LLVMContext context;
	llvm::DataLayout windows32Layout("e-p:32:32-i64:64-n8:16:32-a:0:32-S32");

	LLVMClassLayout windowsByteLayout = layoutLLVMClass(context, windows32Layout, {llvm::Type::getInt8Ty(context)}, {0}, 0);
	expect(windowsByteLayout.abiAlignment == 1, "preferred aggregate alignment was mistaken for ABI alignment");
	expect(windowsByteLayout.allocationSize == 1, "preferred aggregate alignment changed allocation size");

	llvm::DataLayout aggregateAlignedLayout("e-p:32:32-i64:64-n8:16:32-a:32:32-S32");
	LLVMClassLayout byteLayout = layoutLLVMClass(context, aggregateAlignedLayout, {llvm::Type::getInt8Ty(context)}, {0}, 0);
	expect(byteLayout.abiAlignment == 4, "aggregate ABI alignment was not preserved");
	expect(byteLayout.allocationSize == 4, "aggregate allocation padding was not preserved");
	expect(byteLayout.fieldOffsets == std::vector<uint64_t>{0}, "byte class field offset changed");

	LLVMClassLayout nestedLayout = layoutLLVMClass(
		context, aggregateAlignedLayout, {llvm::Type::getInt8Ty(context), byteLayout.structType}, {0, byteLayout.abiAlignment},
		0
	);
	expect(nestedLayout.abiAlignment == 4, "nested class lost aggregate ABI alignment");
	expect(nestedLayout.allocationSize == 8, "nested class allocation size omitted aggregate padding");
	expect(nestedLayout.fieldOffsets == std::vector<uint64_t>({0, 4}), "nested class field offset is incorrect");

	LLVMClassLayout explicitlyAlignedLayout =
		layoutLLVMClass(context, aggregateAlignedLayout, {llvm::Type::getInt8Ty(context)}, {0}, 8);
	expect(explicitlyAlignedLayout.abiAlignment == 8, "explicit class alignment was not preserved");
	expect(explicitlyAlignedLayout.allocationSize == 8, "explicit class tail padding was not materialized");
	expect(byteLayout.structType->isLiteral(), "class layout should use a structural LLVM type");
	expect(
		layoutLLVMClass(context, aggregateAlignedLayout, {llvm::Type::getInt8Ty(context)}, {0}, 0).structType ==
			byteLayout.structType,
		"equivalent class layouts should share the same LLVM type"
	);
	return 0;
}
