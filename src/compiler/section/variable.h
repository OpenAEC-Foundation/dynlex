#pragma once
#include "range.h"
#include <string>
#include <vector>
struct VariableReference;
struct Variable {
	Variable(std::string name, VariableReference *definition) : name(name), definition(definition) {}
	std::string name;
	// the first reference to this variable (the definition point)
	VariableReference *definition;
};