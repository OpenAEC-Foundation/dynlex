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
	Type declaredType; // Kind::Undeduced if not specified
};

struct ClassInstantiation {
	std::vector<Type> fieldTypes;
	llvm::StructType *llvmStructType = nullptr;
};

struct ClassDefinition {
	std::vector<std::string> patternNames;
	std::vector<FieldDefinition> fields;
	std::vector<ClassInstantiation> instantiations;
	int alignment = 0; // Struct alignment in bytes (0 = natural)
	Range range;

	// Find or create instantiation for given field types. Returns index.
	int getOrCreateInstantiation(const std::vector<Type> &fieldTypes) {
		for (int i = 0; i < (int)instantiations.size(); i++) {
			if (instantiations[i].fieldTypes == fieldTypes)
				return i;
		}
		instantiations.push_back({fieldTypes});
		return (int)instantiations.size() - 1;
	}
};
