#pragma once
#include "range.h"
#include "type.h"
#include <string>
#include <vector>
struct VariableReference;
struct Variable {
	Variable(const std::string &name, VariableReference *definition) : name(name), definition(definition) {}
	std::string name;
	Type type;
	// the first reference to this variable (the definition point)
	VariableReference *definition;
};