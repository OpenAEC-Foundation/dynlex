#pragma once

#include "bindingResolution.h"
#include "section.h"
#include <compare>

// A declaration can occur in several simultaneously active flex expansions.
// References retain the body that owns their lexical storage even after binding
// resolution transfers them into a nested expansion.
struct VariableStorageKey {
	VariableReference *definition{};
	const InstantiatedSectionBody *body{};

	auto operator<=>(const VariableStorageKey &) const = default;
};

inline VariableStorageKey variableStorageKey(VariableReference *reference, const InstantiatedSectionBody *body) {
	VariableReference *definition = normalizeBindingReference(reference);
	requireCompilerInvariant(definition != nullptr, "variable storage requires a declaration");
	for (const InstantiatedSectionBody *scope = body; scope; scope = scope->parentBody) {
		auto declared = scope->sourceSection->variableDefinitions.find(definition->name);
		if (declared != scope->sourceSection->variableDefinitions.end() &&
			normalizeBindingReference(declared->second) == definition)
			return {definition, scope};
	}
	return {definition, nullptr};
}

inline VariableStorageKey variableStorageKey(const Expression *expression) {
	requireCompilerInvariant(expression != nullptr, "variable storage requires an expression");
	return variableStorageKey(expression->variable, expression->owningSectionBody);
}
