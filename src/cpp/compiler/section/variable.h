#pragma once
#include "range.h"
#include "type.h"
#include "typeConstraint.h"
#include "variableReference.h"
#include <algorithm>
#include <string>
#include <vector>
struct Variable {
	Variable(const std::string &name, VariableReference *definition, bool isGlobal = false)
		: name(name), definition(definition), isGlobal(isGlobal) {}
	std::string name;
	DataType type;
	TypeConstraint declaredTypeConstraint;
	DataType declaredType;
	std::vector<VariableReference *> declaredTypeConstraintReferences;
	Range typeOriginRange;
	std::string typeOriginFloatLiteralReplacement;
	// the first reference to this variable (the definition point)
	VariableReference *definition;
	// whether this variable is global (module-level) or local (function-level)
	bool isGlobal;

	void addDeclaredTypeConstraintReference(VariableReference *reference) {
		if (std::find(declaredTypeConstraintReferences.begin(), declaredTypeConstraintReferences.end(), reference) ==
			declaredTypeConstraintReferences.end())
			declaredTypeConstraintReferences.push_back(reference);
	}
	bool hasDeclaredTypeConstraint() const { return !declaredTypeConstraintReferences.empty(); }
	VariableReference *firstDeclaredTypeConstraintReference() const {
		return hasDeclaredTypeConstraint() ? declaredTypeConstraintReferences.front() : nullptr;
	}
};
