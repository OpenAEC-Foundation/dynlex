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

	// Find or create instantiation for given field types. Returns index.
	int getOrCreateInstantiation(const std::vector<DataType> &fieldTypes) {
		for (int i = 0; i < (int)instantiations.size(); i++) {
			if (instantiations[i].fieldTypes == fieldTypes)
				return i;
		}
		instantiations.push_back({fieldTypes});
		return (int)instantiations.size() - 1;
	}
};
