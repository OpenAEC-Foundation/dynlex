#pragma once
#include "range.h"
#include <string>
struct VariableReference {
	Range range;
	std::string name;
	VariableReference *definition{};
	VariableReference(Range range, const std::string &name) : range(range), name(name) {}
};