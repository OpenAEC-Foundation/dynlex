#include "type.h"
#include "classDefinition.h"
#include "compilerUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

std::string DataType::toString() const {
	std::string result;
	switch (kind) {
	case Kind::Void:
		result += "void";
		break;
	case Kind::Bool:
		result += "bool";
		break;
	case Kind::Int:
		result += "i" + std::to_string(numericSize * 8);
		break;
	case Kind::Float:
		result += "f" + std::to_string(numericSize * 8);
		break;
	case Kind::Array:
		result += "array[" + std::to_string(arraySize) + "]";
		if (arrayElementType)
			result += " of " + arrayElementType->toString();
		break;
	case Kind::Vector:
		result += "vector[" + std::to_string(arraySize) + "]";
		if (arrayElementType)
			result += " of " + arrayElementType->toString();
		break;
	case Kind::Matrix:
		result += "matrix[" + std::to_string(matrixRowCount) + "x" + std::to_string(arraySize) + "]";
		if (arrayElementType)
			result += " of " + arrayElementType->toString();
		break;
	case Kind::Class:
		result += (classDefinition && !classDefinition->patternNames.empty()) ? classDefinition->patternNames[0] : "class";
		break;
	case Kind::Type:
		result += "type";
		break;
	case Kind::Constraint:
		result += "constraint";
		break;
	case Kind::Any:
	case Kind::Unresolved:
		return result + "unresolved(" + (typeExpression ? std::string("expr") : std::string("?")) + ")";
	}
	for (int i = 0; i < pointerDepth; i++)
		result += "*";
	return result;
}

int DataType::getByteSize() const {
	if (pointerDepth > 0)
		return 8;
	switch (kind) {
	case Kind::Bool:
		return 1;
	case Kind::Int:
	case Kind::Float:
		return numericSize;
	case Kind::Array:
		return arrayElementType ? arrayElementType->getByteSize() * arraySize : 0;
	case Kind::Vector:
		return arrayElementType ? arrayElementType->getByteSize() * arraySize : 0;
	case Kind::Matrix:
		return arrayElementType ? arrayElementType->getByteSize() * arraySize * matrixRowCount : 0;
	case Kind::Class:
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
			crashCompilerBug("Float type must have a valid numericSize (4/8) before codegen");
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
			crashCompilerBug("Integer type must have a valid numericSize (1/2/4/8) before codegen");
		}
	case Kind::Array:
		requireCompilerInvariant(arrayElementType && arraySize >= 0, "Array type must have element type and size");
		return llvm::ArrayType::get(arrayElementType->toLLVM(ctx), arraySize);
	case Kind::Vector:
		requireCompilerInvariant(arrayElementType && arraySize > 0, "Vector type must have element type and size");
		return llvm::FixedVectorType::get(arrayElementType->toLLVM(ctx), arraySize);
	case Kind::Matrix: {
		requireCompilerInvariant(
			arrayElementType && arraySize > 0 && matrixRowCount > 0, "Matrix type must have element type and dimensions"
		);
		llvm::Type *rowVectorType = llvm::FixedVectorType::get(arrayElementType->toLLVM(ctx), arraySize);
		return llvm::ArrayType::get(rowVectorType, matrixRowCount);
	}
	case Kind::Class: {
		requireCompilerInvariant(
			classDefinition && classInstIndex >= 0, "Class type must have classDefinition and instantiation index"
		);
		ClassInstantiation &inst = classDefinition->instantiations[classInstIndex];
		if (!inst.llvmStructType) {
			inst.llvmStructType = llvm::StructType::create(ctx, "class");
			std::vector<llvm::Type *> llvmFields;
			llvmFields.reserve(inst.fieldTypes.size());
			inst.llvmFieldIndices.clear();
			inst.llvmFieldIndices.reserve(inst.fieldTypes.size());
			for (const DataType &fieldType : inst.fieldTypes) {
				inst.llvmFieldIndices.push_back(static_cast<unsigned>(llvmFields.size()));
				llvmFields.push_back(fieldType.toLLVM(ctx));
			}
			inst.llvmStructType->setBody(llvmFields);
		}
		return inst.llvmStructType;
	}
	case Kind::Type:
		crashCompilerBug("Type is compile-time only, cannot be converted to LLVM type");
	case Kind::Constraint:
		crashCompilerBug("Constraint is compile-time only, cannot be converted to LLVM type");
	case Kind::Unresolved:
		crashCompilerBug("Unresolved type must be resolved before codegen");
	case Kind::Any:
		crashCompilerBug("Any type must be resolved before codegen");
	}
	crashCompilerBug("Unknown type kind");
}
