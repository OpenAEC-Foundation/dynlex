#pragma once
#include "range.h"
#include "type.h"
#include <string>
#include <string_view>
#include <vector>

namespace llvm {
class StructType;
}

struct Section;

inline constexpr std::string_view managedLifecycleParameterName = "\x1fmanaged_lifecycle_value";

struct FieldDefinition {
	std::string name;
	Range range;
	DataType declaredType;	// Any if untyped, Unresolved (with typeExpression) if type specified
	unsigned alignment = 0; // Required byte alignment before this field (0 = natural)
};

struct ClassInstantiation {
	std::vector<DataType> fieldTypes = {};
	std::vector<unsigned> llvmFieldIndices = {};
	llvm::StructType *llvmStructType = nullptr;
	uint64_t llvmABIAlignment = 0;
};

struct ClassDefinition {
	std::vector<std::string> patternNames;
	std::vector<FieldDefinition> fields;
	std::vector<ClassInstantiation> instantiations;
	Section *retainSection{};
	Section *releaseSection{};
	unsigned alignment = 0; // Minimum struct alignment in bytes (0 = natural)
	Range alignmentRange;
	Range range;

	// Find or create instantiation for given field types. Returns index.
	int getOrCreateInstantiation(const std::vector<DataType> &fieldTypes) {
		for (int i = 0; i < (int)instantiations.size(); i++) {
			if (instantiations[i].fieldTypes == fieldTypes)
				return i;
		}
		ClassInstantiation inst;
		inst.fieldTypes = fieldTypes;
		instantiations.push_back(inst);
		return (int)instantiations.size() - 1;
	}
};
