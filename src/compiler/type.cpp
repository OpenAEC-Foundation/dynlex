#include "type.h"
#include "classDefinition.h"
#include "compilerUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

int DataType::getByteSize() const {
	if (pointerDepth > 0)
		return 8;
	switch (kind) {
	case Kind::Bool:
		return 1;
	case Kind::Int:
	case Kind::Float:
		return numericSize;
	case Kind::Class:
		if (classDefinition && classInstIndex >= 0)
			return classDefinition->instantiations[classInstIndex].byteSize;
		return 0;
	default:
		return 0;
	}
}

llvm::Type *DataType::toLLVM(llvm::LLVMContext &ctx) const {
	// Any pointer type maps to opaque ptr
	if (pointerDepth > 0)
		return llvm::PointerType::getUnqual(ctx);

	switch (kind) {
	case Kind::Void:
		return llvm::Type::getVoidTy(ctx);
	case Kind::Bool:
		return llvm::Type::getInt1Ty(ctx);
	case Kind::Float:
		switch (numericSize) {
		case 4:
			return llvm::Type::getFloatTy(ctx);
		case 8:
			return llvm::Type::getDoubleTy(ctx);
		default:
			ASSERT_UNREACHABLE("Float type must have a valid numericSize (4/8) before codegen");
		}
	case Kind::Int:
		switch (numericSize) {
		case 1:
			return llvm::Type::getInt8Ty(ctx);
		case 2:
			return llvm::Type::getInt16Ty(ctx);
		case 4:
			return llvm::Type::getInt32Ty(ctx);
		case 8:
			return llvm::Type::getInt64Ty(ctx);
		default:
			ASSERT_UNREACHABLE("Integer type must have a valid numericSize (1/2/4/8) before codegen");
		}
	case Kind::Class: {
		assert(classDefinition && classInstIndex >= 0 && "Class type must have classDefinition and instantiation index");
		ClassInstantiation &inst = classDefinition->instantiations[classInstIndex];
		if (!inst.llvmStructType) {
			std::vector<llvm::Type *> llvmFields;
			inst.llvmFieldIndices.clear();
			int offset = 0;
			for (size_t i = 0; i < inst.fieldTypes.size(); i++) {
				// Align field to its natural alignment
				int fieldSize = inst.fieldTypes[i].isPointer() ? 8 : inst.fieldTypes[i].numericSize;
				if (!fieldSize)
					fieldSize = 1; // bool
				int fieldAlign = fieldSize;
				int padding = (fieldAlign - (offset % fieldAlign)) % fieldAlign;
				if (padding > 0) {
					llvmFields.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx), padding));
					offset += padding;
				}

				inst.llvmFieldIndices.push_back(llvmFields.size());
				llvmFields.push_back(inst.fieldTypes[i].toLLVM(ctx));
				offset += fieldSize;
			}
			inst.byteSize = offset;
			inst.llvmStructType = llvm::StructType::create(ctx, llvmFields, "class");
		}
		return inst.llvmStructType;
	}
	case Kind::Type:
		ASSERT_UNREACHABLE("Type is compile-time only, cannot be converted to LLVM type");
	case Kind::Unresolved:
		ASSERT_UNREACHABLE("Unresolved type must be resolved before codegen");
	case Kind::Any:
		ASSERT_UNREACHABLE("Any type must be resolved before codegen");
	}
	ASSERT_UNREACHABLE("Unknown type kind");
}
