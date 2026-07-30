#include "type.h"
#include "classDefinition.h"
#include "classLayout.h"
#include "compilerUtils.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/MathExtras.h"
#include <set>

namespace {

uint64_t fixedAllocationSize(const llvm::DataLayout &dataLayout, llvm::Type *type) {
	llvm::TypeSize allocationSize = dataLayout.getTypeAllocSize(type);
	requireCompilerInvariant(!allocationSize.isScalable(), "class layout requires fixed-size fields");
	return allocationSize.getFixedValue();
}

using ClassInstanceKey = std::pair<const ClassDefinition *, int>;

bool typeHasManagedLifecycle(const DataType &type, std::set<ClassInstanceKey> &visited) {
	if (type.kind == DataType::Kind::Array)
		return type.arrayElementType && typeHasManagedLifecycle(*type.arrayElementType, visited);
	if (type.kind != DataType::Kind::Class || type.isPointer())
		return false;
	requireCompilerInvariant(
		type.classDefinition && type.classInstIndex >= 0, "managed lifecycle requires a concrete class type"
	);
	const ClassDefinition &definition = *type.classDefinition;
	requireCompilerInvariant(
		type.classInstIndex < static_cast<int>(definition.instantiations.size()),
		"managed lifecycle references a missing class instantiation"
	);
	requireCompilerInvariant(
		static_cast<bool>(definition.retainSection) == static_cast<bool>(definition.releaseSection),
		"managed class has an incomplete lifecycle"
	);
	if (definition.retainSection)
		return true;
	if (!visited.insert({&definition, type.classInstIndex}).second)
		return false;
	for (const DataType &fieldType : definition.instantiations[type.classInstIndex].fieldTypes) {
		if (typeHasManagedLifecycle(fieldType, visited))
			return true;
	}
	return false;
}

} // namespace

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
	for (int pointerLevel = 0; pointerLevel < pointerDepth; pointerLevel++)
		result += "*";
	return result;
}

uint64_t DataType::getByteSize(const llvm::DataLayout &dataLayout, llvm::LLVMContext &llvmContext) const {
	requireCompilerInvariant(isRuntimeValueType(), "byte size requires a concrete runtime value type");
	llvm::Type *llvmType = toLLVM(llvmContext, dataLayout);
	requireCompilerInvariant(llvmType->isSized(), "byte size requires a sized LLVM type");
	return fixedAllocationSize(dataLayout, llvmType);
}

uint64_t DataType::getABIAlignment(const llvm::DataLayout &dataLayout, llvm::LLVMContext &llvmContext) const {
	llvm::Type *llvmType = toLLVM(llvmContext, dataLayout);
	if (kind == Kind::Class && pointerDepth == 0) {
		requireCompilerInvariant(
			classDefinition && classInstIndex >= 0 && classInstIndex < static_cast<int>(classDefinition->instantiations.size()),
			"class alignment requires a concrete class instantiation"
		);
		uint64_t alignment = classDefinition->instantiations[classInstIndex].llvmABIAlignment;
		requireCompilerInvariant(alignment > 0, "class layout is missing its ABI alignment");
		return alignment;
	}
	if (kind == Kind::Array && pointerDepth == 0) {
		requireCompilerInvariant(static_cast<bool>(arrayElementType), "array alignment requires an element type");
		return arrayElementType->getABIAlignment(dataLayout, llvmContext);
	}
	return dataLayout.getABITypeAlign(llvmType).value();
}

llvm::Type *DataType::toLLVM(llvm::LLVMContext &ctx, const llvm::DataLayout &dataLayout) const {
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
		return llvm::ArrayType::get(arrayElementType->toLLVM(ctx, dataLayout), arraySize);
	case Kind::Vector:
		requireCompilerInvariant(arrayElementType && arraySize > 0, "Vector type must have element type and size");
		return llvm::FixedVectorType::get(arrayElementType->toLLVM(ctx, dataLayout), arraySize);
	case Kind::Matrix: {
		requireCompilerInvariant(
			arrayElementType && arraySize > 0 && matrixRowCount > 0, "Matrix type must have element type and dimensions"
		);
		llvm::Type *rowVectorType = llvm::FixedVectorType::get(arrayElementType->toLLVM(ctx, dataLayout), arraySize);
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
			std::vector<uint64_t> fieldAlignments;
			fieldAlignments.reserve(inst.fieldTypes.size());
			requireCompilerInvariant(
				inst.fieldTypes.size() == classDefinition->fields.size(),
				"class instantiation field count differs from its definition"
			);
			for (size_t fieldIndex = 0; fieldIndex < inst.fieldTypes.size(); fieldIndex++) {
				llvm::Type *fieldType = inst.fieldTypes[fieldIndex].toLLVM(ctx, dataLayout);
				uint64_t fieldAlignment = inst.fieldTypes[fieldIndex].getABIAlignment(dataLayout, ctx);
				fieldAlignment = std::max<uint64_t>(fieldAlignment, classDefinition->fields[fieldIndex].alignment);
				llvmFields.push_back(fieldType);
				fieldAlignments.push_back(fieldAlignment);
			}
			LLVMClassLayout layout =
				layoutLLVMClass(ctx, dataLayout, *inst.llvmStructType, llvmFields, fieldAlignments, classDefinition->alignment);
			inst.llvmFieldIndices = std::move(layout.fieldIndices);
			inst.llvmABIAlignment = layout.abiAlignment;
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

bool typeHasManagedLifecycle(const DataType &type) {
	std::set<ClassInstanceKey> visited;
	return typeHasManagedLifecycle(type, visited);
}
