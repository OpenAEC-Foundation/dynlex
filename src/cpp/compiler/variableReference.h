#pragma once
#include "dependentTypeConstraint.h"
#include "range.h"
#include "type.h"
#include "typeConstraint.h"
#include <string>
#include <vector>

namespace llvm {
class AllocaInst;
}

struct PatternDefinition;

struct DependentVariableTypeConstraint {
	PatternDefinition *definition{};
	size_t pathIndex{};
	TypeConstraintTemplate constraint;
};

struct VariableReference {
	Range range;
	std::string name;
	// Present when this source reference used {type:name} syntax.
	std::string declaredTypeConstraintName;
	Range declaredTypeConstraintRange;
	TypeConstraint declaredTypeConstraint;
	DataType declaredType;
	bool hasDependentTypeConstraint = false;
	std::vector<DependentVariableTypeConstraint> dependentTypeConstraints;
	VariableReference *definition{};
	// stack allocation for this variable (set during codegen, only for definitions)
	llvm::AllocaInst *alloca{};
	VariableReference(Range range, const std::string &name) : range(range), name(name) {}
	bool isDefinition() const { return definition == nullptr; }
};
