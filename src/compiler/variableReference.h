#pragma once
#include "range.h"
#include <string>

namespace llvm {
class AllocaInst;
}

struct VariableReference {
	Range range;
	std::string name;
	VariableReference *definition{};
	// stack allocation for this variable (set during codegen, only for definitions)
	llvm::AllocaInst *alloca{};
	VariableReference(Range range, const std::string &name) : range(range), name(name) {}
	bool isDefinition() const { return definition == nullptr; }
};