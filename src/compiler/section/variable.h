#pragma once
#include "range.h"
#include "type.h"
#include <string>
#include <vector>
struct VariableReference;
struct Variable {
	Variable(const std::string &name, VariableReference *definition, bool isGlobal = false)
		: name(name), definition(definition), isGlobal(isGlobal) {}
	std::string name;
	Type type;
	// the first reference to this variable (the definition point)
	VariableReference *definition;
	// whether this variable is global (module-level) or local (function-level)
	bool isGlobal;
};