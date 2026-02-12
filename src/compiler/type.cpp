#include "type.h"
#include "classDefinition.h"
#include "compilerUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

llvm::Type *Type::toLLVM(llvm::LLVMContext &ctx) const {
	// Any pointer type maps to opaque ptr
	if (pointerDepth > 0)
		return llvm::PointerType::getUnqual(ctx);

	switch (kind) {
	case Kind::Void:
		return llvm::Type::getVoidTy(ctx);
	case Kind::Bool:
		return llvm::Type::getInt1Ty(ctx);
	case Kind::Integer:
		switch (byteSize) {
		case 1:
			return llvm::Type::getInt8Ty(ctx);
		case 2:
			return llvm::Type::getInt16Ty(ctx);
		case 4:
			return llvm::Type::getInt32Ty(ctx);
		case 8:
			return llvm::Type::getInt64Ty(ctx);
		default:
			ASSERT_UNREACHABLE("Integer type must have a valid byteSize (1/2/4/8) before codegen");
		}
	case Kind::Float:
		switch (byteSize) {
		case 4:
			return llvm::Type::getFloatTy(ctx);
		case 8:
			return llvm::Type::getDoubleTy(ctx);
		default:
			ASSERT_UNREACHABLE("Float type must have a valid byteSize (4/8) before codegen");
		}
	case Kind::Class: {
		assert(classDefinition && classInstIndex >= 0 && "Class type must have classDefinition and instantiation index");
		ClassInstantiation &inst = classDefinition->instantiations[classInstIndex];
		if (!inst.llvmStructType) {
			std::vector<llvm::Type *> fieldTypes;
			for (const Type &ft : inst.fieldTypes)
				fieldTypes.push_back(ft.toLLVM(ctx));
			inst.llvmStructType = llvm::StructType::create(ctx, fieldTypes, "class");
		}
		return inst.llvmStructType;
	}
	case Kind::Numeric:
		ASSERT_UNREACHABLE("Numeric type must be resolved to Integer or Float before codegen");
	case Kind::TypeReference:
		ASSERT_UNREACHABLE("TypeReference is compile-time only, cannot be converted to LLVM type");
	case Kind::Undeduced:
		ASSERT_UNREACHABLE("Undeduced type must be resolved before codegen");
	}
	ASSERT_UNREACHABLE("Unknown type kind");
}
