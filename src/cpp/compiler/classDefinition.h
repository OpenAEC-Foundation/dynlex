#pragma once
#include "range.h"
#include "type.h"
#include <string>
#include <vector>

namespace llvm {
class StructType;
}

struct FieldDefinition {
	std::string name;
	Range range;
	DataType declaredType; // Any if untyped, Unresolved (with typeExpression) if type specified
};

struct ClassInstantiation {
	std::vector<DataType> fieldTypes = {};
	std::vector<unsigned> llvmFieldIndices = {}; // fieldIdx → LLVM struct element index (accounts for padding)
	int byteSize = 0;							 // Total struct size in bytes (computed in toLLVM)
	llvm::StructType *llvmStructType = nullptr;
};

struct ClassDefinition {
	std::vector<std::string> patternNames;
	std::vector<FieldDefinition> fields;
	std::vector<ClassInstantiation> instantiations;
	int alignment = 0; // Struct alignment in bytes (0 = natural)
	Range range;

	static int classFieldStorageSize(const DataType &fieldType) {
		if (fieldType.isPointer())
			return 8;
		if (fieldType.kind == DataType::Kind::Bool)
			return 1;
		if (fieldType.kind == DataType::Kind::Array || fieldType.kind == DataType::Kind::Vector ||
			fieldType.kind == DataType::Kind::Matrix || fieldType.kind == DataType::Kind::Class) {
			int byteSize = fieldType.getByteSize();
			return byteSize > 0 ? byteSize : 1;
		}
		if (fieldType.numericSize > 0)
			return fieldType.numericSize;
		return 1;
	}

	static int computeClassByteSize(const std::vector<DataType> &fieldTypes) {
		int offset = 0;
		for (const DataType &fieldType : fieldTypes) {
			int fieldSize = classFieldStorageSize(fieldType);
			int fieldAlign = fieldSize;
			int padding = (fieldAlign - (offset % fieldAlign)) % fieldAlign;
			offset += padding + fieldSize;
		}
		return offset;
	}

	// Find or create instantiation for given field types. Returns index.
	int getOrCreateInstantiation(const std::vector<DataType> &fieldTypes) {
		for (int i = 0; i < (int)instantiations.size(); i++) {
			if (instantiations[i].fieldTypes == fieldTypes)
				return i;
		}
		ClassInstantiation inst;
		inst.fieldTypes = fieldTypes;
		inst.byteSize = computeClassByteSize(fieldTypes);
		instantiations.push_back(inst);
		return (int)instantiations.size() - 1;
	}
};
