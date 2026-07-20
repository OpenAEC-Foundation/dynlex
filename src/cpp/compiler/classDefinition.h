#pragma once
#include "range.h"
#include "type.h"
#include <map>
#include <set>
#include <string>
#include <tuple>
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
	std::vector<unsigned> llvmFieldIndices = {};
	llvm::StructType *llvmStructType = nullptr;
};

struct ClassDefinition {
	std::vector<std::string> patternNames;
	std::vector<FieldDefinition> fields;
	std::vector<ClassInstantiation> instantiations;
	// Canonical instantiation selected for a structural set of bound class-pattern arguments.
	std::map<std::string, int> instantiationIndicesByRequest;
	int alignment = 0; // Struct alignment in bytes (0 = natural)
	Range range;

	static bool typeStructurallyRefines(
		const DataType &candidate, const DataType &base,
		std::set<std::tuple<ClassDefinition *, int, int>> &visitedClassInstantiations
	) {
		if (candidate == base)
			return true;
		if (candidate.kind != base.kind || candidate.numericSize != base.numericSize ||
			candidate.pointerDepth != base.pointerDepth || candidate.referencedKind != base.referencedKind ||
			candidate.arraySize != base.arraySize || candidate.matrixRowCount != base.matrixRowCount)
			return false;
		if (candidate.kind == DataType::Kind::Array || candidate.kind == DataType::Kind::Vector ||
			candidate.kind == DataType::Kind::Matrix) {
			return candidate.arrayElementType && base.arrayElementType &&
				   typeStructurallyRefines(*candidate.arrayElementType, *base.arrayElementType, visitedClassInstantiations);
		}
		if (candidate.kind != DataType::Kind::Class || candidate.classDefinition != base.classDefinition ||
			!candidate.classDefinition)
			return false;
		if (base.classInstIndex == -1)
			return candidate.classInstIndex >= 0;
		if (candidate.classInstIndex < 0 || base.classInstIndex < 0)
			return false;
		if (candidate.classInstIndex >= static_cast<int>(candidate.classDefinition->instantiations.size()) ||
			base.classInstIndex >= static_cast<int>(base.classDefinition->instantiations.size()))
			crashCompilerBug("class type refinement refers to a missing instantiation");
		auto comparison = std::make_tuple(candidate.classDefinition, candidate.classInstIndex, base.classInstIndex);
		if (!visitedClassInstantiations.insert(comparison).second)
			return true;
		const std::vector<DataType> &candidateFields =
			candidate.classDefinition->instantiations[candidate.classInstIndex].fieldTypes;
		const std::vector<DataType> &baseFields = base.classDefinition->instantiations[base.classInstIndex].fieldTypes;
		if (candidateFields.size() != baseFields.size())
			crashCompilerBug("class instantiations disagree on field count");
		for (size_t index = 0; index < candidateFields.size(); index++) {
			if (!typeStructurallyRefines(candidateFields[index], baseFields[index], visitedClassInstantiations))
				return false;
		}
		return true;
	}

	static bool typeStructurallyRefines(const DataType &candidate, const DataType &base) {
		std::set<std::tuple<ClassDefinition *, int, int>> visitedClassInstantiations;
		return typeStructurallyRefines(candidate, base, visitedClassInstantiations);
	}

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
